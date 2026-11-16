#include "reader.h"
#ifdef PTEXT_HOST_TEST
#include "render_host.h"
#else
#include "render.h"
#endif
#include "encoding.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>

#define MAX_FILES 100
#define MAX_BOOKMARKS 50
#define MAX_PAGE_HIST 64

static int32_t rd_ui(int32_t px)
{
    return (int32_t)(px * r_ui_scale() + 0.5f);
}

#define MARGIN_X   rd_ui(5)
#define MARGIN_TOP rd_ui(53)
#define MARGIN_BOT rd_ui(10)

static char s_data_path[512];
static char s_cur_name[256];
static char s_book_name[256];
static FileItem s_files[MAX_FILES];
static int32_t s_file_count;

static char *s_mmap_data;
static int64_t s_mmap_len;
static TextDecodeFn s_decode;

static Page s_page_a;
static Page s_page_b;
static Page *s_page_curr = &s_page_a;
static Page *s_page_next = &s_page_b;
static volatile int32_t s_next_ready;
static int64_t s_prefetch_target = -1;

static int64_t s_page_hist[MAX_PAGE_HIST];
static int32_t s_hist_count;
static int32_t s_page_no;

static Settings s_settings = {3, 1, 0, 0, 14};
static Bookmark s_bms[MAX_BOOKMARKS];
static int32_t s_bm_count;

typedef struct {
    int64_t off;
    int32_t x;
    int32_t max_x;
    int32_t indent_w;
    int32_t para_start;
} LineCursor;

typedef struct {
    LineCursor cur;
    int32_t i;
    int32_t max_lines;
    int32_t state;
} PageContext;

static PageContext s_ctx;

static void load_settings(void);
static void paginate_full(int64_t offset, Page *target);
static int32_t page_for_offset(int64_t target, int64_t *page_start);

static int32_t clamp_int(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t is_para_start(int64_t off)
{
    if (!s_mmap_data || off <= 0) return 1;
    if (off > s_mmap_len) off = s_mmap_len;
    return s_mmap_data[off - 1] == '\n' || s_mmap_data[off - 1] == '\r';
}

static void close_mapping(void)
{
    if (s_mmap_data) {
        munmap(s_mmap_data, (size_t)s_mmap_len);
        s_mmap_data = NULL;
    }
    s_mmap_len = 0;
}

void rd_init(const char *path)
{
    snprintf(s_data_path, sizeof(s_data_path), "%s", path ? path : "");
    mkdir(s_data_path, 0755);

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/.prog", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/.bm", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/files", s_data_path);
    mkdir(sub, 0755);

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
    while ((ent = readdir(d)) != NULL && s_file_count < MAX_FILES) {
        if (ent->d_name[0] == '.') continue;
        char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcasecmp(ext, ".txt") != 0) continue;

        FileItem *f = &s_files[s_file_count];
        int path_len = snprintf(f->path, sizeof(f->path), "%s/%s", dpath, ent->d_name);
        if (path_len < 0 || (size_t)path_len >= sizeof(f->path)) continue;
        f->bookmarks = 0;

        char bm_path[1024];
        int bm_len = snprintf(bm_path, sizeof(bm_path), "%s/.bm/%s.bin",
                              s_data_path, ent->d_name);
        if (bm_len > 0 && (size_t)bm_len < sizeof(bm_path)) {
            struct stat st;
            if (stat(bm_path, &st) == 0 && st.st_size >= (off_t)sizeof(Bookmark)) {
                int64_t count = (int64_t)st.st_size / (int64_t)sizeof(Bookmark);
                if (count > MAX_BOOKMARKS) count = MAX_BOOKMARKS;
                f->bookmarks = (int32_t)count;
            }
        }

        f->name[0] = '\0';
        char name_path[sizeof(f->path) + 8];
        int np_len = snprintf(name_path, sizeof(name_path), "%s.name", f->path);
        if (np_len > 0 && (size_t)np_len < sizeof(name_path)) {
            FILE *nf = fopen(name_path, "rb");
            if (nf) {
                size_t n = fread(f->name, 1, sizeof(f->name) - 1, nf);
                fclose(nf);
                f->name[n] = '\0';
                while (n > 0 && (f->name[n - 1] == '\n' || f->name[n - 1] == '\r')) {
                    f->name[--n] = '\0';
                }
            }
        }
        if (!f->name[0]) snprintf(f->name, sizeof(f->name), "%s", ent->d_name);
        s_file_count++;
    }
    closedir(d);
    qsort(s_files, (size_t)s_file_count, sizeof(FileItem), cmp_file_name);
}

FileItem* rd_get_files(int32_t *count)
{
    *count = s_file_count;
    return s_files;
}

static void load_book_name(const char *path)
{
    s_book_name[0] = '\0';

    char npath[512];
    int plen = snprintf(npath, sizeof(npath), "%s.name", path);
    if (plen > 0 && (size_t)plen < sizeof(npath)) {
        FILE *f = fopen(npath, "rb");
        if (f) {
            size_t n = fread(s_book_name, 1, sizeof(s_book_name) - 1, f);
            fclose(f);
            s_book_name[n] = '\0';
            while (n > 0 && (s_book_name[n - 1] == '\n' || s_book_name[n - 1] == '\r')) {
                s_book_name[--n] = '\0';
            }
        }
    }

    if (!s_book_name[0]) {
        snprintf(s_book_name, sizeof(s_book_name), "%s", s_cur_name);
    }

    char *ext = strrchr(s_book_name, '.');
    if (ext && strcasecmp(ext, ".txt") == 0) *ext = '\0';
}

int32_t rd_open_file(const char *path)
{
    if (!path) return -1;
    rd_close();

    int32_t fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    int64_t len = (int64_t)lseek(fd, 0, SEEK_END);
    if (len <= 0) {
        close(fd);
        return -1;
    }

    void *map = mmap(NULL, (size_t)len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return -1;

    s_mmap_data = (char *)map;
    s_mmap_len = len;

    const char *name = strrchr(path, '/');
    snprintf(s_cur_name, sizeof(s_cur_name), "%s", name ? name + 1 : path);

    int64_t start_offset = 0;
    if (len >= 3 &&
        (unsigned char)s_mmap_data[0] == 0xEF &&
        (unsigned char)s_mmap_data[1] == 0xBB &&
        (unsigned char)s_mmap_data[2] == 0xBF) {
        start_offset = 3;
        s_decode = enc_decode_utf8;
    } else {
        s_decode = enc_utf8_prefix_ok((const uint8_t *)s_mmap_data, s_mmap_len)
            ? enc_decode_utf8 : enc_decode_gbk;
    }
    load_book_name(path);

    int64_t saved_offset = start_offset;
    int64_t prev_offset = -1;
    char ppath[512];
    snprintf(ppath, sizeof(ppath), "%s/.prog/%s.bin", s_data_path, s_cur_name);
    FILE *f = fopen(ppath, "rb");
    if (f) {
        int64_t values[3];
        size_t n = fread(values, sizeof(values[0]), 3, f);
        if (n >= 1) saved_offset = values[0];
        if (n >= 2) prev_offset = values[1];
        fclose(f);
    }
    if (saved_offset < start_offset) saved_offset = start_offset;
    if (saved_offset >= len) saved_offset = len - 1;

    int32_t has_prev = 0;
    if (prev_offset >= 0 && prev_offset < saved_offset) {
        Page probe;
        paginate_full(prev_offset, &probe);
        if (probe.next_offset == saved_offset) has_prev = 1;
    }

    s_bm_count = 0;
    snprintf(ppath, sizeof(ppath), "%s/.bm/%s.bin", s_data_path, s_cur_name);
    f = fopen(ppath, "rb");
    if (f) {
        size_t n = fread(s_bms, sizeof(Bookmark), MAX_BOOKMARKS, f);
        fclose(f);
        s_bm_count = (int32_t)n;
        for (int32_t i = 0; i < s_bm_count; i++) {
            s_bms[i].preview[sizeof(s_bms[i].preview) - 1] = '\0';
            if (s_bms[i].offset < 0) s_bms[i].offset = 0;
            if (s_bms[i].offset >= len) s_bms[i].offset = len - 1;
        }
    }

    int64_t open_offset = saved_offset;
    int32_t open_page_no = 1;
    if (saved_offset == start_offset) {
        open_page_no = 1;
    } else {
        int64_t page_start = 0;
        open_page_no = page_for_offset(saved_offset, &page_start);
        open_offset = page_start;
        has_prev = 0;
    }

    rd_goto_offset(open_offset);
    s_page_no = open_page_no > 0 ? open_page_no : 1;
    if (has_prev && open_offset == saved_offset) {
        s_page_hist[0] = prev_offset;
        s_page_hist[1] = s_page_curr->start_offset;
        s_hist_count = 2;
    }
    return 0;
}

void rd_close(void)
{
    rd_save_progress();
    close_mapping();
    s_cur_name[0] = '\0';
    s_page_curr->count = 0;
    s_page_curr->start_offset = 0;
    s_page_curr->next_offset = 0;
    s_hist_count = 0;
    s_page_no = 0;
    s_book_name[0] = '\0';
    rd_reset_prefetch();
}

int64_t rd_get_file_len(void)
{
    return s_mmap_len;
}

Page* rd_get_page(void)
{
    return s_page_curr;
}

int32_t rd_get_page_no(void)
{
    return s_page_no;
}

const char* rd_get_book_name(void)
{
    return s_book_name;
}

static int32_t page_max_lines(void)
{
    int32_t lh = rd_get_line_h();
    if (lh < 1) lh = 1;
    int32_t n = (r_h() - MARGIN_TOP - MARGIN_BOT) / lh;
    if (n < 1) n = 1;
    if (n > MAX_LINES) n = MAX_LINES;
    return n;
}

static int32_t page_max_x(void)
{
    int32_t n = r_w() - MARGIN_X * 2;
    return n > 1 ? n : 1;
}

PageLayout rd_get_layout(void)
{
    PageLayout pl;
    pl.margin_x = MARGIN_X;
    pl.margin_top = MARGIN_TOP;
    pl.margin_bot = MARGIN_BOT;
    pl.line_height = rd_get_line_h();
    if (pl.line_height < 1) pl.line_height = 1;
    pl.text_width = page_max_x();
    pl.page_height = r_h();
    pl.max_lines = page_max_lines();
    return pl;
}

static int32_t page_for_offset(int64_t target, int64_t *page_start)
{
    Page probe;
    int64_t off = 0;
    int32_t number = 1;

    while (off < target) {
        paginate_full(off, &probe);
        if (probe.next_offset <= off || probe.next_offset > target) break;
        off = probe.next_offset;
        number++;
    }

    *page_start = off;
    return number;
}

static void layout_line(Line *line, LineCursor *cur)
{
    line->len = 0;
    line->indent = cur->para_start ? cur->indent_w : 0;
    line->justify = 0;
    cur->x = line->indent;

    while (cur->off < s_mmap_len) {
        if (line->len >= MAX_CHARS - 1) break;

        int64_t prev = cur->off;
        uint32_t cp = s_decode((const uint8_t *)s_mmap_data, s_mmap_len, &cur->off);
        if (cp == 0) break;
        if (cp == '\r') {
            if (cur->off < s_mmap_len && s_mmap_data[cur->off] == '\n') cur->off++;
            break;
        }
        if (cp == '\n') break;

        int32_t cw = r_char_advance(cp);
        if (cur->x + cw > cur->max_x && line->len > 0) {
            int32_t over = cur->x + cw - cur->max_x;
            if (over <= rd_ui(6)) {
                line->text[line->len++] = cp;
                cur->x += cw;
                line->justify = 1;
                break;
            }
            cur->off = prev;
            line->justify = 1;
            break;
        }
        line->text[line->len++] = cp;
        cur->x += cw;
    }

    cur->para_start = is_para_start(cur->off);
}

static int32_t line_blank(const Line *line)
{
    if (line->len == 0) return 1;
    for (int32_t i = 0; i < line->len; i++) {
        uint32_t cp = line->text[i];
        if (cp != ' ' && cp != '\t' && cp != 0x3000) return 0;
    }
    return 1;
}

static int32_t line_punct_only(const Line *line)
{
    if (line->len == 0 || line->len > 4) return 0;
    for (int32_t i = 0; i < line->len; i++) {
        uint32_t cp = line->text[i];
        if (cp != ',' && cp != '.' && cp != '!' && cp != '?' && cp != ';' && cp != ':' &&
            cp != 0xFF0C && cp != 0x3002 && cp != 0xFF01 && cp != 0xFF1F &&
            cp != 0xFF1B && cp != 0xFF1A && cp != 0x3001 && cp != 0x2026 && cp != 0x2014) {
            return 0;
        }
    }
    return 1;
}

static void paginate_full(int64_t offset, Page *target)
{
    target->count = 0;
    target->start_offset = offset;
    target->next_offset = offset;

    if (offset < 0) offset = 0;
    if (offset >= s_mmap_len) {
        target->start_offset = offset;
        target->next_offset = offset;
        return;
    }

    LineCursor cur = {
        offset, 0, page_max_x(), rd_get_indent_w(), is_para_start(offset)
    };
    int32_t max_lines = page_max_lines();
    int32_t i = 0;
    while (i < max_lines && cur.off < s_mmap_len) {
        layout_line(&target->lines[i], &cur);
        if (line_blank(&target->lines[i])) continue;
        if (i == 0 && line_punct_only(&target->lines[i])) continue;
        target->count++;
        i++;
    }

    target->start_offset = offset;
    target->next_offset = cur.off;
}

static void paginate_init(int64_t offset)
{
    s_next_ready = 0;
    s_prefetch_target = offset;
    s_page_next->count = 0;
    s_ctx.cur.off = offset;
    s_ctx.cur.x = 0;
    s_ctx.cur.max_x = page_max_x();
    s_ctx.cur.indent_w = rd_get_indent_w();
    s_ctx.cur.para_start = is_para_start(offset);
    s_ctx.i = 0;
    s_ctx.max_lines = page_max_lines();
    s_ctx.state = 1;
}

static void paginate_step(void)
{
    if (s_ctx.state != 1) return;

    for (int32_t step = 0; step < 8 &&
         s_ctx.i < s_ctx.max_lines && s_ctx.cur.off < s_mmap_len; step++) {
        layout_line(&s_page_next->lines[s_ctx.i], &s_ctx.cur);
        if (line_blank(&s_page_next->lines[s_ctx.i])) continue;
        if (s_ctx.i == 0 && line_punct_only(&s_page_next->lines[s_ctx.i])) continue;
        s_page_next->count++;
        s_ctx.i++;
    }

    if (s_ctx.i >= s_ctx.max_lines || s_ctx.cur.off >= s_mmap_len) {
        s_page_next->start_offset = s_prefetch_target;
        s_page_next->next_offset = s_ctx.cur.off;
        s_ctx.state = 2;
        s_next_ready = 1;
    }
}

static int32_t write_atomic(const char *path, const void *buf, size_t len)
{
    char tmp[520];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return -1;

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    const uint8_t *p = (const uint8_t *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, p + done, len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return -1;
        }
        if (w == 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        done += (size_t)w;
    }

    int32_t ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && rename(tmp, path) == 0) return 0;
    unlink(tmp);
    return -1;
}

static void save_prog(int64_t offset)
{
    char ppath[512];
    snprintf(ppath, sizeof(ppath), "%s/.prog/%s.bin", s_data_path, s_cur_name);
    int64_t values[3] = {offset, -1, s_page_no};
    if (s_hist_count > 1) values[1] = s_page_hist[s_hist_count - 2];
    write_atomic(ppath, values, sizeof(values));
}

static void hist_reset(int64_t start)
{
    s_page_hist[0] = start;
    s_hist_count = 1;
}

static void hist_push(int64_t start)
{
    if (s_hist_count >= MAX_PAGE_HIST) {
        memmove(s_page_hist, s_page_hist + 1,
                sizeof(s_page_hist[0]) * (MAX_PAGE_HIST - 1));
        s_hist_count = MAX_PAGE_HIST - 1;
    }
    s_page_hist[s_hist_count++] = start;
}

void rd_goto_offset(int64_t offset)
{
    if (!s_mmap_data || s_mmap_len <= 0) return;

    if (offset < 0) offset = 0;
    if (offset >= s_mmap_len) offset = s_mmap_len - 1;

    paginate_full(offset, s_page_curr);
    rd_reset_prefetch();
    hist_reset(offset);

    if (s_page_curr->next_offset < s_mmap_len) {
        paginate_init(s_page_curr->next_offset);
    }
}

void rd_next_page(void)
{
    if (!rd_can_next()) return;

    int64_t next = s_page_curr->next_offset;
    if (s_next_ready && s_prefetch_target == next) {
        Page *tmp = s_page_curr;
        s_page_curr = s_page_next;
        s_page_next = tmp;
        s_next_ready = 0;
    } else {
        rd_reset_prefetch();
        paginate_full(next, s_page_curr);
    }

    hist_push(s_page_curr->start_offset);
    if (s_page_no > 0) s_page_no++;

    if (s_page_curr->next_offset < s_mmap_len) {
        paginate_init(s_page_curr->next_offset);
    }
}

void rd_prev_page(void)
{
    if (!rd_can_prev()) return;
    rd_reset_prefetch();

    if (s_hist_count > 1) {
        s_hist_count--;
        int64_t target = s_page_hist[s_hist_count - 1];
        paginate_full(target, s_page_curr);
    } else {
        int64_t target = 0;
        int64_t guessed = s_page_curr->start_offset - (s_mmap_len / 50);
        if (guessed < 0) guessed = 0;

        int64_t probe = guessed;
        int32_t safety = 0;
        int32_t found = 0;
        Page probe_page;

        while (probe < s_page_curr->start_offset && safety < 200) {
            paginate_full(probe, &probe_page);
            if (probe_page.next_offset >= s_page_curr->start_offset ||
                probe_page.next_offset >= s_mmap_len) {
                target = probe;
                found = 1;
                break;
            }
            if (probe_page.next_offset <= probe) break;
            probe = probe_page.next_offset;
            safety++;
        }
        if (!found) target = 0;

        paginate_full(target, s_page_curr);
        hist_reset(s_page_curr->start_offset);
    }

    if (s_page_curr->next_offset < s_mmap_len) {
        paginate_init(s_page_curr->next_offset);
    }
    if (s_page_no > 1) s_page_no--;
    else s_page_no = 0;
}

int32_t rd_can_next(void)
{
    return s_page_curr->next_offset < s_mmap_len;
}

int32_t rd_can_prev(void)
{
    return s_page_curr->start_offset > 0;
}

Settings* rd_get_settings(void)
{
    return &s_settings;
}

static void save_settings(void)
{
    char spath[512];
    snprintf(spath, sizeof(spath), "%s/.prog/settings.bin", s_data_path);
    write_atomic(spath, &s_settings, sizeof(s_settings));
}

static void load_settings(void)
{
    char spath[512];
    snprintf(spath, sizeof(spath), "%s/.prog/settings.bin", s_data_path);
    FILE *f = fopen(spath, "rb");
    int32_t migrate = 1;
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long fsize = ftell(f);
            if (fsize >= 12 && fseek(f, 0, SEEK_SET) == 0) {
                Settings tmp;
                memset(&tmp, 0, sizeof(tmp));
                size_t want = (size_t)fsize < sizeof(tmp) ? (size_t)fsize : sizeof(tmp);
                size_t rn = fread(&tmp, 1, want, f);
                if (rn >= 12) {
                    s_settings.font_idx = clamp_int(tmp.font_idx, 0, 6);
                    s_settings.indent_idx = clamp_int(tmp.indent_idx, 0, 2);
                    s_settings.gap_idx = clamp_int(tmp.gap_idx, 0, 2);
                    if (rn >= 16) s_settings.theme = clamp_int(tmp.theme, 0, 1);
                    if (rn >= sizeof(Settings) && tmp.version >= 14) {
                        s_settings.version = tmp.version;
                        migrate = 0;
                    }
                }
            }
        }
        fclose(f);
    }

    if (migrate) {
        s_settings.font_idx = 3;
        s_settings.indent_idx = 1;
        s_settings.gap_idx = 0;
        s_settings.version = 14;
        save_settings();
    }

    r_set_theme(s_settings.theme);
    r_set_font_size((18.0f + s_settings.font_idx * 2.0f) * r_ui_scale());
}

void rd_set_font_idx(int32_t i)
{
    s_settings.font_idx = clamp_int(i, 0, 6);
    r_set_font_size((18.0f + s_settings.font_idx * 2.0f) * r_ui_scale());
    save_settings();
}

void rd_set_indent_idx(int32_t i)
{
    s_settings.indent_idx = clamp_int(i, 0, 2);
    save_settings();
}

void rd_set_gap_idx(int32_t i)
{
    s_settings.gap_idx = clamp_int(i, 0, 2);
    save_settings();
}

int32_t rd_get_line_h(void)
{
    int32_t fh = r_font_h();
    if (fh < 1) fh = 1;
    int32_t gap = clamp_int(s_settings.gap_idx, 0, 2);
    int32_t lh = (rd_ui(47) + 1) / 2 + gap * rd_ui(2);
    return lh;
}

int32_t rd_get_indent_w(void)
{
    int32_t adv = r_char_advance(0x4E2D);
    if (adv <= 0) adv = rd_ui(16);
    return clamp_int(s_settings.indent_idx, 0, 2) * 2 * adv;
}

void rd_set_theme(int32_t t)
{
    s_settings.theme = t ? 1 : 0;
    r_set_theme(s_settings.theme);
    save_settings();
}

static int32_t bookmark_preview(int64_t offset, char *out, int32_t cap)
{
    int32_t n = 0;
    int64_t off = offset;

    while (off < s_mmap_len && n < cap - 1) {
        uint32_t cp = s_decode((const uint8_t *)s_mmap_data, s_mmap_len, &off);
        if (cp == 0 || cp == '\n' || cp == '\r') break;

        char enc[4];
        int32_t len = enc_utf8_encode(cp, enc);
        if (len <= 0 || n + len > cap - 1) break;
        memcpy(out + n, enc, (size_t)len);
        n += len;
    }

    out[n] = '\0';
    return n;
}

void rd_toggle_bookmark(void)
{
    if (!s_mmap_data || s_cur_name[0] == '\0') return;

    int32_t exist = -1;
    for (int32_t i = 0; i < s_bm_count; i++) {
        if (s_bms[i].offset == s_page_curr->start_offset) {
            exist = i;
            break;
        }
    }

    if (exist >= 0) {
        s_bms[exist] = s_bms[--s_bm_count];
    } else if (s_bm_count < MAX_BOOKMARKS) {
        Bookmark *b = &s_bms[s_bm_count++];
        b->offset = s_page_curr->start_offset;
        bookmark_preview(b->offset, b->preview, (int32_t)sizeof(b->preview));
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/.bm/%s.bin", s_data_path, s_cur_name);
    write_atomic(path, s_bms, sizeof(Bookmark) * (size_t)s_bm_count);
}

int32_t rd_page_bookmarked(void)
{
    for (int32_t i = 0; i < s_bm_count; i++) {
        if (s_bms[i].offset == s_page_curr->start_offset) return 1;
    }
    return 0;
}

int32_t rd_delete_file(const char *path)
{
    if (!path) return -1;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (unlink(path) != 0) {
        rd_scan_dir();
        return -1;
    }
    char side[512];
    snprintf(side, sizeof(side), "%s.name", path);
    unlink(side);
    snprintf(side, sizeof(side), "%s/.prog/%s.bin", s_data_path, base);
    unlink(side);
    snprintf(side, sizeof(side), "%s/.bm/%s.bin", s_data_path, base);
    unlink(side);

    rd_scan_dir();
    return 0;
}

int32_t rd_read_bookmarks(const char *path, Bookmark *out, int32_t cap)
{
    if (!path || !out || cap <= 0) return 0;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    char bm_path[768];
    int n = snprintf(bm_path, sizeof(bm_path), "%s/.bm/%s.bin", s_data_path, base);
    if (n <= 0 || (size_t)n >= sizeof(bm_path)) return 0;

    FILE *f = fopen(bm_path, "rb");
    if (!f) return 0;
    int32_t count = (int32_t)fread(out, sizeof(Bookmark), (size_t)cap, f);
    fclose(f);
    for (int32_t i = 0; i < count; i++) out[i].preview[sizeof(out[i].preview) - 1] = '\0';
    return count;
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
    if (s_mmap_data && s_mmap_len > 0 && s_page_curr->start_offset >= 0) {
        save_prog(s_page_curr->start_offset);
    }
}
