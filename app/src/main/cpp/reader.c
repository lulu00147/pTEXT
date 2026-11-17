#include "reader.h"
#ifdef PTEXT_HOST_TEST
#include "render_host.h"
#else
#include "render.h"
#endif
#include "encoding.h"
#include "persist.h"
#include "clamp.h"

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
#define MAX_PAGE_HIST 64
#define BM_MAGIC 0x314D4250u
#define BM_VERSION 1u

static Bookmark s_bms[MAX_BOOKMARKS];
static int32_t s_bm_count;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
} BmHeader;

static uint32_t bm_checksum(const Bookmark *b, int32_t count)
{
    const uint8_t *p = (const uint8_t *)b;
    size_t n = sizeof(Bookmark) * (size_t)count;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static int32_t load_bookmarks(const char *path, Bookmark *out, int32_t cap)
{
    if (!path || !out || cap <= 0) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    BmHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    int32_t count;
    if (h.magic == BM_MAGIC) {
        if (h.version != BM_VERSION || h.count > MAX_BOOKMARKS ||
            h.count > (uint32_t)cap || fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return 0;
        }
        long size = ftell(f);
        long expect = (long)sizeof(BmHeader) + (long)h.count * (long)sizeof(Bookmark);
        if (size != expect ||
            fseek(f, (long)sizeof(BmHeader), SEEK_SET) != 0 ||
            fread(out, sizeof(Bookmark), h.count, f) != h.count ||
            bm_checksum(out, (int32_t)h.count) != h.checksum) {
            fclose(f);
            return 0;
        }
        count = (int32_t)h.count;
    } else {
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            return 0;
        }
        count = (int32_t)fread(out, sizeof(Bookmark), (size_t)cap, f);
    }
    fclose(f);

    for (int32_t i = 0; i < count; i++) out[i].preview[sizeof(out[i].preview) - 1] = '\0';
    return count;
}

static int32_t bookmark_count(const char *path)
{
    if (!path) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    BmHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    int32_t count;
    if (h.magic == BM_MAGIC) {
        if (h.version != BM_VERSION || h.count > MAX_BOOKMARKS ||
            fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return 0;
        }
        long size = ftell(f);
        long expect = (long)sizeof(BmHeader) + (long)h.count * (long)sizeof(Bookmark);
        if (size != expect) {
            fclose(f);
            return 0;
        }
        count = (int32_t)h.count;
    } else {
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return 0;
        }
        long size = ftell(f);
        if (size < 0) {
            fclose(f);
            return 0;
        }
        count = (int32_t)(size / (long)sizeof(Bookmark));
        if (count > MAX_BOOKMARKS) count = MAX_BOOKMARKS;
    }
    fclose(f);
    return count;
}

static void save_bookmarks(const char *path)
{
    uint8_t buf[sizeof(BmHeader) + MAX_BOOKMARKS * sizeof(Bookmark)];
    BmHeader h;
    h.magic = BM_MAGIC;
    h.version = BM_VERSION;
    h.count = (uint32_t)s_bm_count;
    h.checksum = bm_checksum(s_bms, s_bm_count);
    memcpy(buf, &h, sizeof(h));
    if (s_bm_count > 0) {
        memcpy(buf + sizeof(h), s_bms, sizeof(Bookmark) * (size_t)s_bm_count);
    }
    persist_write_atomic(path, buf, sizeof(h) + sizeof(Bookmark) * (size_t)s_bm_count);
}


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
static int32_t s_file_limit;

static int32_t sidecar_path(char *out, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(out, cap, "%s/%s/%s.bin", s_data_path, dir, name);
    return n > 0 && (size_t)n < cap;
}

static int32_t name_sidecar_path(char *out, size_t cap, const char *path)
{
    int n = snprintf(out, cap, "%s.name", path);
    return n > 0 && (size_t)n < cap;
}

static int32_t read_name_sidecar(const char *path, char *out, size_t cap)
{
    if (cap == 0) return 0;
    out[0] = '\0';

    char npath[1024];
    if (!name_sidecar_path(npath, sizeof(npath), path)) return 0;

    FILE *f = fopen(npath, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[--n] = '\0';
    }
    return out[0] != '\0';
}

static void page_checkpoint_add(int64_t off, int32_t page_no);
static int32_t page_checkpoint_find(int64_t target, int64_t *off);

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

#define PAGE_CKPT_MAX 1024
#define PAGE_CKPT_STEP 64
static int64_t s_page_ckpt_off[PAGE_CKPT_MAX];
static int32_t s_page_ckpt_no[PAGE_CKPT_MAX];
static int32_t s_page_ckpt_count;

void rd_reset_page_cache(void)
{
    s_page_ckpt_count = 0;
}

static Settings s_settings = {3, 1, 0, 0, 14};

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

    char sub[1024];
    snprintf(sub, sizeof(sub), "%s/.prog", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/.bm", s_data_path);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/files", s_data_path);
    mkdir(sub, 0755);

    rd_reset_page_cache();
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
    s_file_limit = 0;
    char dpath[1024];
    snprintf(dpath, sizeof(dpath), "%s/files", s_data_path);

    DIR *d = opendir(dpath);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char *ext = strrchr(ent->d_name, '.');
        if (ext && strcasecmp(ext, ".part") == 0) {
            char stale[1400];
            int n = snprintf(stale, sizeof(stale), "%s/%s", dpath, ent->d_name);
            if (n > 0 && (size_t)n < sizeof(stale)) unlink(stale);
            continue;
        }
        if (!ext || strcasecmp(ext, ".txt") != 0) continue;
        if (s_file_count >= MAX_FILES) {
            s_file_limit = 1;
            continue;
        }

        FileItem *f = &s_files[s_file_count];
        int path_len = snprintf(f->path, sizeof(f->path), "%s/%s", dpath, ent->d_name);
        if (path_len < 0 || (size_t)path_len >= sizeof(f->path)) continue;
        f->bookmarks = 0;

        char bm_path[1024];
        if (sidecar_path(bm_path, sizeof(bm_path), ".bm", ent->d_name)) {
            f->bookmarks = bookmark_count(bm_path);
        }

        f->name[0] = '\0';
        if (!read_name_sidecar(f->path, f->name, sizeof(f->name))) {
            snprintf(f->name, sizeof(f->name), "%s", ent->d_name);
        }
        s_file_count++;
    }
    closedir(d);
    qsort(s_files, (size_t)s_file_count, sizeof(FileItem), cmp_file_name);
}

FileItem* rd_get_files(int32_t *count)
{
    if (count) *count = s_file_count;
    return s_files;
}

int32_t rd_file_limit_reached(void)
{
    return s_file_limit;
}

static void load_book_name(const char *path)
{
    s_book_name[0] = '\0';
    if (!read_name_sidecar(path, s_book_name, sizeof(s_book_name))) {
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
    char ppath[1024];
    if (sidecar_path(ppath, sizeof(ppath), ".prog", s_cur_name)) {
        FILE *f = fopen(ppath, "rb");
        if (f) {
            int64_t values[3];
            size_t n = fread(values, sizeof(values[0]), 3, f);
            if (n >= 1) saved_offset = values[0];
            if (n >= 2) prev_offset = values[1];
            fclose(f);
        }
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
    if (sidecar_path(ppath, sizeof(ppath), ".bm", s_cur_name))
        s_bm_count = load_bookmarks(ppath, s_bms, MAX_BOOKMARKS);
    for (int32_t i = 0; i < s_bm_count; i++) {
        if (s_bms[i].offset < 0) s_bms[i].offset = 0;
        if (s_bms[i].offset >= len) s_bms[i].offset = len - 1;
    }

    int64_t open_offset = saved_offset;
    int32_t open_page_no = 1;
    s_page_ckpt_count = 0;
    if (saved_offset == start_offset) {
        open_page_no = 1;
        page_checkpoint_add(start_offset, 1);
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
    s_bm_count = 0;
    s_book_name[0] = '\0';
    rd_reset_prefetch();
    rd_reset_page_cache();
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

static void page_checkpoint_add(int64_t off, int32_t page_no)
{
    if (page_no <= 0) return;
    if (s_page_ckpt_count > 0) {
        int32_t last = s_page_ckpt_count - 1;
        if (s_page_ckpt_off[last] == off || page_no < s_page_ckpt_no[last] + PAGE_CKPT_STEP) return;
    }
    if (s_page_ckpt_count >= PAGE_CKPT_MAX) return;
    s_page_ckpt_off[s_page_ckpt_count] = off;
    s_page_ckpt_no[s_page_ckpt_count] = page_no;
    s_page_ckpt_count++;
}

static int32_t page_checkpoint_find(int64_t target, int64_t *off)
{
    int32_t best = -1;
    for (int32_t i = 0; i < s_page_ckpt_count; i++) {
        if (s_page_ckpt_off[i] > target) break;
        best = i;
    }
    if (best < 0) return 0;
    *off = s_page_ckpt_off[best];
    return s_page_ckpt_no[best];
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
    return pl;
}

static int32_t page_for_offset(int64_t target, int64_t *page_start)
{
    Page probe;
    int64_t off = 0;
    int32_t number = 1;
    page_checkpoint_add(off, number);

    while (off < target) {
        paginate_full(off, &probe);
        if (probe.next_offset <= off || probe.next_offset > target) break;
        off = probe.next_offset;
        number++;
        if (number % PAGE_CKPT_STEP == 0) page_checkpoint_add(off, number);
    }

    page_checkpoint_add(off, number);
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

int32_t rd_line_blank(const Line *line)
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
        if (rd_line_blank(&target->lines[i])) continue;
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
        if (rd_line_blank(&s_page_next->lines[s_ctx.i])) continue;
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

static void save_prog(int64_t offset)
{
    char ppath[1024];
    if (!sidecar_path(ppath, sizeof(ppath), ".prog", s_cur_name)) return;
    int64_t values[3] = {offset, -1, s_page_no};
    if (s_hist_count > 1) values[1] = s_page_hist[s_hist_count - 2];
    persist_write_atomic(ppath, values, sizeof(values));
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

void rd_jump_to_offset(int64_t offset)
{
    if (!s_mmap_data || s_mmap_len <= 0) return;
    if (offset < 0) offset = 0;
    if (offset >= s_mmap_len) offset = s_mmap_len - 1;

    int64_t page_start = 0;
    int32_t page_no = 1;

    if (s_page_no > 0 && s_page_curr->start_offset <= offset) {
        page_start = s_page_curr->start_offset;
        page_no = s_page_no;
    } else {
        page_no = page_checkpoint_find(offset, &page_start);
        if (page_no <= 0) {
            page_no = page_for_offset(offset, &page_start);
        }
    }

    int64_t off = page_start;
    while (off < offset) {
        Page probe;
        paginate_full(off, &probe);
        if (probe.next_offset <= off || probe.next_offset > offset) break;
        off = probe.next_offset;
        page_no++;
        if (page_no % PAGE_CKPT_STEP == 0) page_checkpoint_add(off, page_no);
    }
    page_start = off;

    rd_goto_offset(page_start);
    s_page_no = page_no > 0 ? page_no : 1;
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
    if (s_page_no > 0 && s_page_no % PAGE_CKPT_STEP == 0) {
        page_checkpoint_add(s_page_curr->start_offset, s_page_no);
    }

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
        int64_t probe = 0;
        int32_t found = 0;
        Page probe_page;

        (void)page_checkpoint_find(s_page_curr->start_offset - 1, &probe);
        int32_t safety_limit = s_page_ckpt_count > 1 ? PAGE_CKPT_STEP + 2 : 100000;
        int32_t safety = 0;
        while (probe < s_page_curr->start_offset && safety <= safety_limit) {
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
    char spath[1024];
    if (!sidecar_path(spath, sizeof(spath), ".prog", "settings")) return;
    persist_write_atomic(spath, &s_settings, sizeof(s_settings));
}

static void load_settings(void)
{
    char spath[1024];
    if (!sidecar_path(spath, sizeof(spath), ".prog", "settings")) return;
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
                    s_settings.font_idx = clamp_i32(tmp.font_idx, 0, 6);
                    s_settings.indent_idx = clamp_i32(tmp.indent_idx, 0, 2);
                    s_settings.gap_idx = clamp_i32(tmp.gap_idx, 0, 2);
                    if (rn >= 16) s_settings.theme = clamp_i32(tmp.theme, 0, 1);
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
    s_settings.font_idx = clamp_i32(i, 0, 6);
    r_set_font_size((18.0f + s_settings.font_idx * 2.0f) * r_ui_scale());
    rd_reset_page_cache();
    save_settings();
}

void rd_set_indent_idx(int32_t i)
{
    s_settings.indent_idx = clamp_i32(i, 0, 2);
    rd_reset_page_cache();
    save_settings();
}

void rd_set_gap_idx(int32_t i)
{
    s_settings.gap_idx = clamp_i32(i, 0, 2);
    rd_reset_page_cache();
    save_settings();
}

int32_t rd_get_line_h(void)
{
    int32_t gap = clamp_i32(s_settings.gap_idx, 0, 2);
    return (rd_ui(47) + 1) / 2 + gap * rd_ui(2);
}

int32_t rd_get_indent_w(void)
{
    int32_t adv = r_char_advance(0x4E2D);
    if (adv <= 0) adv = rd_ui(16);
    return clamp_i32(s_settings.indent_idx, 0, 2) * 2 * adv;
}

void rd_set_theme(int32_t t)
{
    s_settings.theme = t ? 1 : 0;
    r_set_theme(s_settings.theme);
    save_settings();
}

static int32_t bookmark_preview(int64_t offset, char *out, int32_t cap)
{
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (!s_mmap_data || s_mmap_len <= 0) return 0;

    int64_t off = offset;
    if (off < 0) off = 0;
    if (off >= s_mmap_len) return 0;

    while (off < s_mmap_len) {
        int32_t n = 0;
        int32_t has_text = 0;

        while (off < s_mmap_len) {
            uint32_t cp = s_decode((const uint8_t *)s_mmap_data, s_mmap_len, &off);
            if (cp == 0 || cp == '\n' || cp == '\r') break;
            int32_t is_space = cp == ' ' || cp == '\t' || cp == 0x3000;
            if (is_space && !has_text) continue;
            if (!is_space) has_text = 1;

            char enc[4];
            int32_t len = enc_utf8_encode(cp, enc);
            if (len <= 0) break;
            if (n + len > cap - 1) break;
            memcpy(out + n, enc, (size_t)len);
            n += len;
        }

        if (has_text && n > 0) {
            out[n] = '\0';
            return n;
        }
    }

    out[0] = '\0';
    return 0;
}

static void refresh_bookmark_previews(const char *path, Bookmark *bms, int32_t count)
{
    if (!path || !bms || count <= 0) return;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;

    int64_t len = (int64_t)lseek(fd, 0, SEEK_END);
    if (len <= 0) {
        close(fd);
        return;
    }

    void *map = mmap(NULL, (size_t)len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    int64_t start = 0;
    TextDecodeFn decode;
    const uint8_t *data = (const uint8_t *)map;
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        start = 3;
        decode = enc_decode_utf8;
    } else {
        decode = enc_utf8_prefix_ok(data, len) ? enc_decode_utf8 : enc_decode_gbk;
    }

    char *old_map = s_mmap_data;
    int64_t old_len = s_mmap_len;
    TextDecodeFn old_decode = s_decode;

    s_mmap_data = (char *)map;
    s_mmap_len = len;
    s_decode = decode;

    for (int32_t i = 0; i < count; i++) {
        int64_t off = bms[i].offset;
        if (off < start) off = start;
        if (off >= len) off = len - 1;
        bookmark_preview(off, bms[i].preview, (int32_t)sizeof(bms[i].preview));
    }

    s_mmap_data = old_map;
    s_mmap_len = old_len;
    s_decode = old_decode;
    munmap(map, (size_t)len);
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

    char path[1024];
    if (!sidecar_path(path, sizeof(path), ".bm", s_cur_name)) return;
    save_bookmarks(path);
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
    int32_t side_error = 0;
    char side[1024];
    if (name_sidecar_path(side, sizeof(side), path)) {
        if (unlink(side) != 0 && errno != ENOENT) side_error = 1;
    } else {
        side_error = 1;
    }
    if (sidecar_path(side, sizeof(side), ".prog", base)) {
        if (unlink(side) != 0 && errno != ENOENT) side_error = 1;
    } else {
        side_error = 1;
    }
    if (sidecar_path(side, sizeof(side), ".bm", base)) {
        if (unlink(side) != 0 && errno != ENOENT) side_error = 1;
    } else {
        side_error = 1;
    }

    rd_scan_dir();
    return side_error ? -2 : 0;
}

int32_t rd_read_bookmarks(const char *path, Bookmark *out, int32_t cap)
{
    if (!path || !out || cap <= 0) return 0;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    char bm_path[1024];
    if (!sidecar_path(bm_path, sizeof(bm_path), ".bm", base)) return 0;

    int32_t count = load_bookmarks(bm_path, out, cap);
    if (count > 0) refresh_bookmark_previews(path, out, count);
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
