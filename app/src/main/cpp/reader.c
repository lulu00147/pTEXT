#include "reader.h"
#include "render.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#define MARGIN_X 30
#define MARGIN_TOP 60
#define MARGIN_BOT 50

static char s_data_path[512];
static char s_cur_name[256];
static FileItem s_files[100];
static int32_t s_file_count = 0;

static char *s_mmap_data = NULL;
static int64_t s_mmap_len = 0;
static int32_t s_is_gbk = 0;

static Page s_page_curr;
static Page s_page_next;
static volatile int32_t s_next_ready = 0;
static int64_t s_prefetch_target = -1;

/* 导航历史环形栈：记录正向翻页精确起点，替代启发式反向扫描 */
#define MAX_PAGE_HIST 64
static int64_t s_page_hist[MAX_PAGE_HIST];
static int32_t s_hist_head = 0;
static int32_t s_hist_tail = 0;

typedef struct {
    int64_t off;
    int32_t i, x, max_lines, indent_w, max_x;
    int64_t line_start;
    int32_t para_start;
    int32_t state;
} PageContext;

static PageContext s_ctx;
static Settings s_settings = {2, 1, 1, 0};  /* font_idx, indent_idx, gap_idx, theme=dark */
static Bookmark s_bms[50];
static int32_t s_bm_count = 0;
static uint32_t (*s_decode)(int64_t*) = NULL;

/* 安全边界判断：避免静态分析器报 offset-1 越界 */
#define IS_PARA_START(off) ((off) == 0 ? 1 : \
    (s_mmap_data[(off)-1] == '\n' || \
    s_mmap_data[(off)-1] == '\r'))

static uint32_t gbk_decode_fast(unsigned char b1, unsigned char b2)
{
    if (b1 >= 0xB0 && b1 <= 0xD7 && b2 >= 0xA1 && b2 <= 0xFE)
        return 0x4E00 + (b1 - 0xB0) * 94 + (b2 - 0xA1);
    if (b1 == 0xA1 && b2 >= 0xA1 && b2 <= 0xFE)
        return 0x3000 + (b2 - 0xA1);
    if (b1 == 0xA3 && b2 >= 0xA1 && b2 <= 0xFE)
        return 0xFF00 + (b2 - 0xA1);
    return '?';
}

/* 稳健 UTF-8 解码：拒绝冗余编码、代理对、非法续字节 */
static uint32_t next_char_utf8(int64_t *off)
{
    if (__builtin_expect(*off >= s_mmap_len, 0)) return 0;
    unsigned char c = (unsigned char)s_mmap_data[*off];
    if (__builtin_expect(c < 0x80, 1)) {
        (*off)++;
        return c;
    }
    uint32_t cp;
    int32_t need;
    if     ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; need = 1; }
    else if ((c & 0xF0) == 0xE0)     { cp = c & 0x0F; need = 2; }
    else if ((c & 0xF8) == 0xF0)     { cp = c & 0x07; need = 3; }
    else { (*off)++; return 0xFFFD; }

    /* 冗余编码最短形式检验 */
    if (need == 1 && cp < 2)        { (*off) += need + 1; goto utf8_bad_end; }
    if (need == 2 && cp == 0) {
        /* 检查第一个续字节是否构成冗余编码 (U+0000-U+07FF 只需 1 个续字节) */
        if (__builtin_expect(*off + 1 >= s_mmap_len, 0)) { (*off) += need + 1; goto utf8_bad_end; }
        unsigned char nb = (unsigned char)s_mmap_data[(*off)+1];
        if ((nb & 0xC0) != 0x80 || (nb & 0x3F) < 0x20) { (*off) += need + 1; goto utf8_bad_end; }
    }

    /* 逐字节解码续字节 */
    if (__builtin_expect(*off + need >= s_mmap_len, 0)) { (*off) += need + 1; goto utf8_bad_end; }
    (*off)++;
    for (int32_t j = 0; j < need; j++) {
        unsigned char nb = (unsigned char)s_mmap_data[(*off) + j];
        if ((nb & 0xC0) != 0x80) { (*off) += j + 1; goto utf8_bad_end; }
        cp = (cp << 6) | (nb & 0x3F);
    }
    (*off) += need;

    /* 拒绝代理对、U+FFFE/U+FFFF、超范围 */
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0xFFFD;
    if (cp > 0x10FFFF)                return 0xFFFD;
    if ((cp & 0xFFFE) == 0xFFFE)      return 0xFFFD;

    return cp ? cp : 0xFFFD;

utf8_bad_end:
    if (*off > s_mmap_len) *off = s_mmap_len;
    return 0xFFFD;
}

static uint32_t next_char_gbk(int64_t *off)
{
    if (__builtin_expect(*off >= s_mmap_len, 0)) return 0;
    unsigned char b1 = s_mmap_data[*off];
    if (__builtin_expect(b1 < 0x80, 1)) {
        (*off)++;
        return b1;
    }
    if (__builtin_expect(*off + 1 >= s_mmap_len, 0)) {
        (*off)++;
        return '?';
    }
    (*off) += 2;
    return gbk_decode_fast(b1, s_mmap_data[*off - 1]);
}

/* 前向声明 */
static void load_settings(void);

void rd_init(const char *path)
{
    strncpy(s_data_path, path, sizeof(s_data_path) - 1);
    s_data_path[sizeof(s_data_path) - 1] = '\0';  /* 强制终止 */

    mkdir(s_data_path, 0755);

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/.prog", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/.bm", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/files", s_data_path);
    mkdir(sub, 0755);

    /* 加载持久化设置 */
    load_settings();
}

static int32_t cmp_file_name(const void *a, const void *b)
{
    const FileItem *fa = (const FileItem *)a;
    const FileItem *fb = (const FileItem *)b;
    return strcasecmp(fa->name, fb->name);
}

void rd_scan_dir(void)
{
    s_file_count = 0;
    char dpath[512];
    snprintf(dpath, sizeof(dpath), "%s/files", s_data_path);
    DIR *d = opendir(dpath);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_file_count < 100) {
        if (ent->d_name[0] == '.') continue;
        char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcasecmp(ext, ".txt") != 0) continue;
        FileItem *f = &s_files[s_file_count++];
        snprintf(f->path, sizeof(f->path), "%s/%s", dpath, ent->d_name);
        strncpy(f->name, ent->d_name, sizeof(f->name) - 1);
    }
    closedir(d);
    qsort(s_files, s_file_count, sizeof(FileItem), cmp_file_name);
}

FileItem* rd_get_files(int32_t *count)
{
    *count = s_file_count;
    return s_files;
}

int32_t rd_open_file(const char *path)
{
    if (s_mmap_data) {
        munmap(s_mmap_data, s_mmap_len);
        s_mmap_data = NULL;
    }
    int32_t fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    s_mmap_len = lseek(fd, 0, SEEK_END);
    if (s_mmap_len <= 0) {
        close(fd);
        s_mmap_len = 0;
        return -1;
    }
    s_mmap_data = mmap(NULL, s_mmap_len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (s_mmap_data == MAP_FAILED) {
        s_mmap_data = NULL;
        return -1;
    }
    const char *name = strrchr(path, '/');
    strncpy(s_cur_name, name ? name + 1 : path, sizeof(s_cur_name) - 1);
    s_cur_name[sizeof(s_cur_name) - 1] = '\0';
/* 多字节编码检测：先快速验证 UTF-8 合法性，失败后回退 GBK。
   同时检测 UTF-8 BOM (EF BB BF)。 */
    int64_t start_offset = 0;
    if (s_mmap_len >= 3 &&
        (unsigned char)s_mmap_data[0] == 0xEF &&
        (unsigned char)s_mmap_data[1] == 0xBB &&
        (unsigned char)s_mmap_data[2] == 0xBF) {
        start_offset = 3;
        s_is_gbk = 0;
    } else {
        /* 扫描前 4096 字节：全部合法 UTF-8 序列 → UTF-8；否则 GBK */
        int32_t is_utf8 = 1;
        int64_t limit = s_mmap_len < 4096 ? s_mmap_len : 4096;
        int64_t pos = 0;
        while (pos < limit) {
            unsigned char c = (unsigned char)s_mmap_data[pos];
            if (c < 0x80) { pos++; continue; }
            int32_t need;
            if     ((c & 0xE0) == 0xC0) need = 1;
            else if ((c & 0xF0) == 0xE0) need = 2;
            else if ((c & 0xF8) == 0xF0) need = 3;
            else { is_utf8 = 0; break; }
            for (int32_t k = 1; k <= need && pos + k < limit; k++) {
                if (((unsigned char)s_mmap_data[pos + k] & 0xC0) != 0x80)
                    { is_utf8 = 0; break; }
            }
            if (!is_utf8) break;
            if (pos + need >= limit) break;
            pos += need + 1;
        }
        s_is_gbk = is_utf8 ? 0 : 1;
    }
    s_decode = s_is_gbk ? next_char_gbk : next_char_utf8;

    int64_t saved_offset = start_offset;
    char ppath[512];
    snprintf(ppath, sizeof(ppath), "%s/.prog/%s.bin", s_data_path, s_cur_name);
    FILE *f = fopen(ppath, "rb");
    if (f) {
        fread(&saved_offset, sizeof(int64_t), 1, f);
        fclose(f);
    }

    s_bm_count = 0;
    snprintf(ppath, sizeof(ppath), "%s/.bm/%s.bin", s_data_path, s_cur_name);
    f = fopen(ppath, "rb");
    if (f) {
        s_bm_count = (int32_t)fread(s_bms, sizeof(Bookmark), 50, f);
        fclose(f);
    }

    /* 跳过 BOM 后定位 */
    rd_goto_offset(saved_offset);
    return 0;
}

void rd_close(void)
{
    if (s_mmap_data) {
        munmap(s_mmap_data, s_mmap_len);
        s_mmap_data = NULL;
        s_mmap_len = 0;
    }
    rd_reset_prefetch();
}

int64_t rd_get_file_len(void)
{
    return s_mmap_len;
}

Page* rd_get_page(void)
{
    return &s_page_curr;
}

static void paginate_full(int64_t offset, Page *target)
{
    target->count = 0;
    int64_t off = offset;
    int32_t lh = rd_get_line_h();
    if (lh < 1) lh = 1;
    int32_t max_lines = (r_h() - MARGIN_TOP - MARGIN_BOT) / lh;
    if (max_lines < 1) max_lines = 1;
    int32_t indent_w = rd_get_indent_w();
    int32_t max_x = r_w() - MARGIN_X * 2;
    for (int32_t i = 0; i < max_lines && off < s_mmap_len; i++) {
        Line *l = &target->lines[i];
        l->len = 0;
        int64_t line_start = off;
        int32_t para_start = IS_PARA_START(off);
        int32_t x = para_start ? indent_w : 0;
        while (off < s_mmap_len) {
            int64_t prev_off = off;
            uint32_t cp = s_decode(&off);
            if (cp == 0 || cp == '\n' || cp == '\r') break;
            if (l->len >= MAX_CHARS - 1) break;
            int32_t cw = r_char_advance(cp);
            if (x + cw > max_x && l->len > 0) {
                off = prev_off;
                break;
            }
            l->text[l->len++] = cp;
            x += cw;
        }
        target->count++;
    }
    target->start_offset = offset;
    target->next_offset = off;
}

static void paginate_init(int64_t offset)
{
    s_next_ready = 0;
    s_prefetch_target = offset;
    s_page_next.count = 0;
    s_ctx.off = offset;
    s_ctx.i = 0;
    s_ctx.x = 0;
    s_ctx.line_start = offset;
    s_ctx.para_start = IS_PARA_START(offset);
    s_ctx.max_lines = (r_h() - MARGIN_TOP - MARGIN_BOT) / rd_get_line_h();
    if (s_ctx.max_lines < 1) s_ctx.max_lines = 1;
    s_ctx.indent_w = rd_get_indent_w();
    s_ctx.max_x = r_w() - MARGIN_X * 2;
    s_ctx.state = 1;
}

static void paginate_step(void)
{
    if (s_ctx.state != 1) return;
    for (int32_t step = 0; step < 8 && s_ctx.i < s_ctx.max_lines && s_ctx.off < s_mmap_len; step++) {
        Line *l = &s_page_next.lines[s_ctx.i];
        l->len = 0;
        s_ctx.x = s_ctx.para_start ? s_ctx.indent_w : 0;
        while (s_ctx.off < s_mmap_len) {
            int64_t prev_off = s_ctx.off;
            uint32_t cp = s_decode(&s_ctx.off);
            if (cp == 0 || cp == '\n' || cp == '\r') break;
            if (l->len >= MAX_CHARS - 1) break;
            int32_t cw = r_char_advance(cp);
            if (s_ctx.x + cw > s_ctx.max_x && l->len > 0) {
                s_ctx.off = prev_off;
                break;
            }
            l->text[l->len++] = cp;
            s_ctx.x += cw;
        }
        s_page_next.count++;
        s_ctx.i++;
        s_ctx.line_start = s_ctx.off;
        s_ctx.para_start = IS_PARA_START(s_ctx.off);
    }
    if (s_ctx.i >= s_ctx.max_lines || s_ctx.off >= s_mmap_len) {
        s_page_next.start_offset = s_prefetch_target;
        s_page_next.next_offset = s_ctx.off;
        s_ctx.state = 2;
        s_next_ready = 1;
    }
}

static void save_prog(int64_t offset)
{
    char ppath[512];
    snprintf(ppath, sizeof(ppath), "%s/.prog/%s.bin", s_data_path, s_cur_name);
    FILE *f = fopen(ppath, "wb");
    if (f) {
        fwrite(&offset, sizeof(int64_t), 1, f);
        fclose(f);
    }
}

void rd_goto_offset(int64_t offset)
{
    if (!s_mmap_data) return;
    if (offset < 0) offset = 0;
    if (offset >= s_mmap_len) offset = s_mmap_len - 1;
    paginate_full(offset, &s_page_curr);
    save_prog(offset);
    rd_reset_prefetch();
    /* 排版变更或绝对跳转时，历史偏移失效，重建栈底 */
    s_page_hist[0] = offset;
    s_hist_head = 1;
    s_hist_tail = 0;
    if (s_page_curr.next_offset < s_mmap_len) {
        paginate_init(s_page_curr.next_offset);
    }
}

void rd_next_page(void)
{
    if (!rd_can_next()) return;
    if (s_next_ready && s_prefetch_target == s_page_curr.next_offset) {
        Page tmp = s_page_curr;
        s_page_curr = s_page_next;
        s_page_next = tmp;
        s_next_ready = 0;
        save_prog(s_page_curr.start_offset);
        if (s_page_curr.next_offset < s_mmap_len) {
            paginate_init(s_page_curr.next_offset);
        }
    } else {
        rd_reset_prefetch();
        paginate_full(s_page_curr.next_offset, &s_page_curr);
        save_prog(s_page_curr.start_offset);
        if (s_page_curr.next_offset < s_mmap_len) {
            paginate_init(s_page_curr.next_offset);
        }
    }
    /* 精确记录正向翻页起点 */
    s_page_hist[s_hist_head] = s_page_curr.start_offset;
    s_hist_head = (s_hist_head + 1) % MAX_PAGE_HIST;
    if (s_hist_head == s_hist_tail) {
        s_hist_tail = (s_hist_tail + 1) % MAX_PAGE_HIST;
    }
}

void rd_prev_page(void)
{
    if (!rd_can_prev()) return;
    rd_reset_prefetch();
    /* 优先从历史栈精确弹出上一页偏移 */
    if (s_hist_head != s_hist_tail) {
        s_hist_head = (s_hist_head - 1 + MAX_PAGE_HIST) % MAX_PAGE_HIST;
        int64_t target = s_page_hist[s_hist_head];
        paginate_full(target, &s_page_curr);
        save_prog(s_page_curr.start_offset);
        if (s_page_curr.next_offset < s_mmap_len) {
            paginate_init(s_page_curr.next_offset);
        }
        return;
    }
    /* 栈底边界：向前试探扫描而非粗暴跳到文件头 */
    if (s_page_curr.start_offset > 0) {
        /* 粗略估算前一页的起始偏移 */
        int64_t guessed = s_page_curr.start_offset - (s_mmap_len / 50);
        if (guessed < 0) guessed = 0;
        /* 从猜测点向前扫描，找到包含当前页起始的前一页 */
        int64_t probe = guessed;
        int32_t safety = 0;
        int32_t found = 0;
        Page probe_page;
        while (probe < s_page_curr.start_offset && safety < 200) {
            paginate_full(probe, &probe_page);
            if (probe_page.next_offset >= s_page_curr.start_offset ||
                probe_page.next_offset >= s_mmap_len) {
                s_page_curr = probe_page;
                found = 1;
                break;
            }
            probe = probe_page.next_offset;
            safety++;
        }
        /* 试探扫描失败，安全回退到文件头 */
        if (!found) {
            paginate_full(0, &s_page_curr);
        }
        save_prog(s_page_curr.start_offset);
        if (s_page_curr.next_offset < s_mmap_len) {
            paginate_init(s_page_curr.next_offset);
        }
    }
}

int32_t rd_can_next(void)
{
    return s_page_curr.next_offset < s_mmap_len;
}

int32_t rd_can_prev(void)
{
    return s_page_curr.start_offset > 0;
}

Settings* rd_get_settings(void)
{
    return &s_settings;
}

void rd_set_font_idx(int32_t i)
{
    s_settings.font_idx = i;
    r_set_font_size(18.0f + i * 2.0f);
}

void rd_set_indent_idx(int32_t i)
{
    s_settings.indent_idx = i;
}

void rd_set_gap_idx(int32_t i)
{
    s_settings.gap_idx = i;
}

int32_t rd_get_line_h(void)
{
    return r_font_h() + (int32_t)(r_font_h() * s_settings.gap_idx * 0.5f);
}

int32_t rd_get_indent_w(void)
{
    int32_t adv = r_char_advance(' ');
    return s_settings.indent_idx * 2 * (adv > 0 ? adv : 12);
}

static void save_settings(void)
{
    char spath[512];
    snprintf(spath, sizeof(spath), "%s/.prog/settings.bin", s_data_path);
    FILE *f = fopen(spath, "wb");
    if (f) {
        fwrite(&s_settings, sizeof(Settings), 1, f);
        fclose(f);
    }
}

static void load_settings(void)
{
    char spath[512];
    snprintf(spath, sizeof(spath), "%s/.prog/settings.bin", s_data_path);
    FILE *f = fopen(spath, "rb");
    if (f) {
        /* 获取文件大小，兼容旧版 12 字节 (无 theme 字段) */
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsize >= 12) {
            Settings tmp;
            memset(&tmp, 0, sizeof(tmp));
            size_t rn = fread(&tmp, 1, (size_t)fsize < sizeof(tmp) ? (size_t)fsize : sizeof(tmp), f);
            if (rn >= 12) {
                s_settings.font_idx   = tmp.font_idx;
                s_settings.indent_idx = tmp.indent_idx;
                s_settings.gap_idx    = tmp.gap_idx;
                /* 旧版文件无 theme 字段，保持结构体默认值 0 */
                if (rn >= 16) s_settings.theme = tmp.theme;
            }
        }
        fclose(f);
    }
    /* 应用主题到渲染层 */
    r_set_theme(s_settings.theme);
    r_set_font_size(18.0f + s_settings.font_idx * 2.0f);
}

void rd_set_theme(int32_t t)
{
    s_settings.theme = t ? 1 : 0;
    r_set_theme(s_settings.theme);
    save_settings();
}

void rd_toggle_bookmark(void)
{
    int32_t exist = -1;
    for (int32_t i = 0; i < s_bm_count; i++) {
        if (s_bms[i].offset == s_page_curr.start_offset) {
            exist = i;
            break;
        }
    }
    if (exist >= 0) {
        s_bms[exist] = s_bms[--s_bm_count];
    } else if (s_bm_count < 50) {
        Bookmark *b = &s_bms[s_bm_count++];
        b->offset = s_page_curr.start_offset;
        int64_t poff = b->offset;
        int32_t j = 0;
        while (poff < s_mmap_len && j < 47) {
            uint32_t cp = s_decode(&poff);
            if (cp == 0 || cp == '\n' || cp == '\r') break;
            b->preview[j++] = (cp < 0x7F && cp >= 0x20) ? (char)cp : ' ';
        }
        b->preview[j] = 0;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/.bm/%s.bin", s_data_path, s_cur_name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(s_bms, sizeof(Bookmark), s_bm_count, f);
        fclose(f);
    }
}

Bookmark* rd_get_bookmarks(int32_t *count)
{
    *count = s_bm_count;
    return s_bms;
}

void rd_jump_bookmark(int32_t idx)
{
    if (idx >= 0 && idx < s_bm_count)
        rd_goto_offset(s_bms[idx].offset);
}

void rd_prefetch_step(void)
{
    if (s_ctx.state == 1) paginate_step();
}

void rd_reset_prefetch(void)
{
    s_ctx.state = 0;
    s_next_ready = 0;
    s_prefetch_target = -1;
}

int32_t rd_is_prefetching(void)
{
    return s_ctx.state == 1;
}

void rd_save_progress(void)
{
    if (s_mmap_data && s_page_curr.start_offset >= 0)
        save_prog(s_page_curr.start_offset);
}