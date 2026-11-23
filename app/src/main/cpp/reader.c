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

#define DIR_PROGRESS  ".prog"
#define DIR_BOOKMARKS ".bm"
#define DIR_INDEX     ".idx"
#define DIR_FILES     "files"
#define SIDECAR_SUFFIX ".bin"
#define NAME_SUFFIX   ".name"
#define SETTINGS_NAME "settings"

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

/* 1 = valid BmHeader, -1 = legacy raw bookmarks, 0 = invalid. */
static int32_t bm_read_header(FILE *f, BmHeader *h)
{
    if (fread(h, sizeof(*h), 1, f) != 1) return 0;
    if (h->magic != BM_MAGIC) return -1;
    if (h->version != BM_VERSION || h->count > MAX_BOOKMARKS ||
        fseek(f, 0, SEEK_END) != 0) return 0;

    long size = ftell(f);
    long expect = (long)sizeof(BmHeader) + (long)h->count * (long)sizeof(Bookmark);
    return size == expect ? 1 : 0;
}

static int32_t bm_legacy_count(FILE *f)
{
    if (fseek(f, 0, SEEK_END) != 0) return 0;
    long size = ftell(f);
    if (size < 0) return 0;

    int32_t count = (int32_t)(size / (long)sizeof(Bookmark));
    return count > MAX_BOOKMARKS ? MAX_BOOKMARKS : count;
}

static int32_t load_bookmarks(const char *path, Bookmark *out, int32_t cap)
{
    if (!path || !out || cap <= 0) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    BmHeader h;
    int32_t kind = bm_read_header(f, &h);
    if (kind == 0) {
        fclose(f);
        return 0;
    }

    int32_t count;
    if (kind > 0) {
        if (h.count > (uint32_t)cap ||
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
    int32_t kind = bm_read_header(f, &h);
    int32_t count = kind > 0 ? (int32_t)h.count :
                    kind < 0 ? bm_legacy_count(f) : 0;
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
    int n = snprintf(out, cap, "%s/%s/%s%s", s_data_path, dir, name, SIDECAR_SUFFIX);
    return n > 0 && (size_t)n < cap;
}

static int32_t name_sidecar_path(char *out, size_t cap, const char *path)
{
    int n = snprintf(out, cap, "%s%s", path, NAME_SUFFIX);
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
static int64_t s_mmap_mtime;
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
#define IDX_MAGIC 0x32445850u
#define IDX_VERSION 1u
#define IDX_STEP PAGE_CKPT_STEP
#define IDX_MIN_FILE (1024 * 1024)
#define IDX_MIN_TARGET (256 * 1024)
#define LAYOUT_RULES_VERSION 1u
static int64_t s_page_ckpt_off[PAGE_CKPT_MAX];
static int32_t s_page_ckpt_no[PAGE_CKPT_MAX];
static int32_t s_page_ckpt_count;
static int32_t s_idx_dirty;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t step;
    uint32_t count;
    uint32_t layout_hash;
    uint32_t checksum;
    uint32_t reserved;
    uint32_t reserved2;
    int64_t file_size;
    int64_t file_mtime;
} IdxHeader;

typedef struct {
    int64_t off;
    int32_t page_no;
    int32_t reserved;
} IdxRecord;

_Static_assert(sizeof(IdxHeader) == 48, "index header layout");
_Static_assert(sizeof(IdxRecord) == 16, "index record layout");

static uint8_t s_idx_buf[sizeof(IdxHeader) + PAGE_CKPT_MAX * sizeof(IdxRecord)] __attribute__((aligned(8)));

void rd_reset_page_cache(void)
{
    s_page_ckpt_count = 0;
    s_idx_dirty = 0;
}

static Settings s_settings = {3, 1, 0, 0, 14, 0};

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
static void idx_save(void);
static int32_t idx_lookup(int64_t target, int64_t *off, int32_t *page_no);
static int32_t bookmark_preview(int64_t offset, char *out, int32_t cap);
static void refresh_bookmark_previews(const char *path, Bookmark *bms,
                                      int32_t count);
static void hist_reset(int64_t start);
static void hist_push(int64_t start);
static void paginate_init(int64_t offset);
static void paginate_step(void);
#ifdef PTEXT_HOST_TEST
static int32_t s_test_bad_paginate;
static int64_t s_test_prev_probe = -1;
static int32_t s_test_prev_safety = -1;
#endif

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
    snprintf(sub, sizeof(sub), "%s/%s", s_data_path, DIR_PROGRESS);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/%s", s_data_path, DIR_BOOKMARKS);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/%s", s_data_path, DIR_INDEX);
    mkdir(sub, 0755);
    snprintf(sub, sizeof(sub), "%s/%s", s_data_path, DIR_FILES);
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
    snprintf(dpath, sizeof(dpath), "%s/%s", s_data_path, DIR_FILES);

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
        if (sidecar_path(bm_path, sizeof(bm_path), DIR_BOOKMARKS, ent->d_name)) {
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
    struct stat st;
    int64_t mtime = 0;
    if (fstat(fd, &st) == 0) mtime = (int64_t)st.st_mtime;

    void *map = mmap(NULL, (size_t)len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return -1;

    s_mmap_data = (char *)map;
    s_mmap_len = len;
    s_mmap_mtime = mtime;

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
    char ppath[1024];
    if (sidecar_path(ppath, sizeof(ppath), DIR_PROGRESS, s_cur_name)) {
        FILE *f = fopen(ppath, "rb");
        if (f) {
            int64_t values[3];
            size_t n = fread(values, sizeof(values[0]), 3, f);
            if (n >= 1) saved_offset = values[0];
            fclose(f);
        }
    }
    if (saved_offset < start_offset) saved_offset = start_offset;
    if (saved_offset >= len) saved_offset = len - 1;

    s_bm_count = 0;
    if (sidecar_path(ppath, sizeof(ppath), DIR_BOOKMARKS, s_cur_name))
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
    }

    rd_goto_offset(open_offset);
    s_page_no = open_page_no > 0 ? open_page_no : 1;
    return 0;
}

void rd_close(void)
{
    rd_save_progress();
    idx_save();
    close_mapping();
    s_mmap_mtime = 0;
    s_idx_dirty = 0;
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
    int32_t avail = r_h() - MARGIN_TOP - MARGIN_BOT;
    int32_t step = lh * 10 + 7;
    int32_t n = avail > 0 ? (avail * 10) / step + 1 : 1;
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
    s_idx_dirty = 1;
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

static void idx_hash_u32(uint32_t *h, uint32_t v)
{
    *h ^= v;
    *h *= 16777619u;
}

static uint32_t idx_layout_hash(void)
{
    uint32_t h = 2166136261u;
    PageLayout pl = rd_get_layout();
    idx_hash_u32(&h, LAYOUT_RULES_VERSION);
    idx_hash_u32(&h, (uint32_t)s_settings.font_idx);
    idx_hash_u32(&h, (uint32_t)s_settings.indent_idx);
    idx_hash_u32(&h, (uint32_t)s_settings.gap_idx);
    idx_hash_u32(&h, (uint32_t)pl.margin_x);
    idx_hash_u32(&h, (uint32_t)pl.margin_top);
    idx_hash_u32(&h, (uint32_t)pl.margin_bot);
    idx_hash_u32(&h, (uint32_t)pl.line_height);
    idx_hash_u32(&h, (uint32_t)pl.text_width);
    idx_hash_u32(&h, (uint32_t)pl.page_height);
    idx_hash_u32(&h, (uint32_t)r_font_h());
    idx_hash_u32(&h, (uint32_t)r_char_advance(0x4E2D));
    return h;
}

static void idx_save(void)
{
    if (!s_idx_dirty || !s_mmap_data || s_mmap_len < IDX_MIN_FILE ||
        s_page_ckpt_count < 2) return;

    char path[1024];
    if (!sidecar_path(path, sizeof(path), DIR_INDEX, s_cur_name)) return;

    IdxHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = IDX_MAGIC;
    h.version = IDX_VERSION;
    h.step = IDX_STEP;
    h.count = (uint32_t)s_page_ckpt_count;
    h.layout_hash = idx_layout_hash();
    h.file_size = s_mmap_len;
    h.file_mtime = s_mmap_mtime;

    uint32_t checksum = 2166136261u;
    uint8_t *p = s_idx_buf + sizeof(h);
    for (int32_t i = 0; i < s_page_ckpt_count; i++) {
        IdxRecord r;
        r.off = s_page_ckpt_off[i];
        r.page_no = s_page_ckpt_no[i];
        r.reserved = 0;
        memcpy(p + (size_t)i * sizeof(r), &r, sizeof(r));
        const uint8_t *rp = (const uint8_t *)&r;
        for (size_t j = 0; j < sizeof(r); j++) {
            checksum ^= rp[j];
            checksum *= 16777619u;
        }
    }
    h.checksum = checksum;
    memcpy(s_idx_buf, &h, sizeof(h));
    size_t total = sizeof(h) + (size_t)s_page_ckpt_count * sizeof(IdxRecord);
    if (persist_write_atomic(path, s_idx_buf, total) == 0) s_idx_dirty = 0;
}

static int32_t idx_parse(const uint8_t *buf, size_t len,
                         int64_t file_size, int64_t file_mtime,
                         uint32_t layout_hash, int64_t target,
                         int64_t *off, int32_t *page_no, uint32_t *prefix_count)
{
    if (!buf || len < sizeof(IdxHeader) || !off || !page_no) return 0;

    IdxHeader h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != IDX_MAGIC || h.version != IDX_VERSION ||
        h.step != IDX_STEP || h.count == 0 || h.count > PAGE_CKPT_MAX ||
        h.file_size != file_size || h.file_mtime != file_mtime ||
        h.layout_hash != layout_hash) return 0;

    size_t expected = sizeof(h) + (size_t)h.count * sizeof(IdxRecord);
    if (len != expected) return 0;

    const uint8_t *base = buf + sizeof(h);
    uint32_t checksum = 2166136261u;
    int64_t prev_off = -1;
    int32_t prev_page = 0;
    for (uint32_t i = 0; i < h.count; i++) {
        IdxRecord r;
        memcpy(&r, base + (size_t)i * sizeof(r), sizeof(r));
        const uint8_t *rp = (const uint8_t *)&r;
        for (size_t j = 0; j < sizeof(r); j++) {
            checksum ^= rp[j];
            checksum *= 16777619u;
        }
        if (r.off < 0 || r.off >= file_size ||
            r.off <= prev_off || r.page_no <= prev_page) return 0;
        prev_off = r.off;
        prev_page = r.page_no;
    }
    if (checksum != h.checksum) return 0;

    uint32_t lo = 0, hi = h.count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        IdxRecord r;
        memcpy(&r, base + (size_t)mid * sizeof(r), sizeof(r));
        if (r.off <= target) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;

    IdxRecord selected;
    memcpy(&selected, base + (size_t)(lo - 1) * sizeof(selected), sizeof(selected));
    *off = selected.off;
    *page_no = selected.page_no;
    if (prefix_count) *prefix_count = lo;
    return 1;
}

static int32_t idx_lookup(int64_t target, int64_t *off, int32_t *page_no)
{
    if (!s_mmap_data || !s_cur_name[0] || s_mmap_len < IDX_MIN_FILE ||
        target < IDX_MIN_TARGET || s_page_ckpt_count > 0) return 0;

    char path[1024];
    if (!sidecar_path(path, sizeof(path), DIR_INDEX, s_cur_name)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0 || (size_t)fsize > sizeof(s_idx_buf) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t len = (size_t)fsize;
    if (fread(s_idx_buf, 1, len, f) != len) {
        fclose(f);
        return 0;
    }
    fclose(f);

    uint32_t prefix = 0;
    int64_t selected_off = 0;
    int32_t selected_page = 0;
    if (!idx_parse(s_idx_buf, len, s_mmap_len, s_mmap_mtime,
                   idx_layout_hash(), target, &selected_off,
                   &selected_page, &prefix)) {
        s_page_ckpt_count = 0;
        s_idx_dirty = 0;
        return 0;
    }

    const uint8_t *base = s_idx_buf + sizeof(IdxHeader);
    for (uint32_t i = 0; i < prefix; i++) {
        IdxRecord r;
        memcpy(&r, base + (size_t)i * sizeof(r), sizeof(r));
        s_page_ckpt_off[i] = r.off;
        s_page_ckpt_no[i] = r.page_no;
    }
    s_page_ckpt_count = (int32_t)prefix;
    s_idx_dirty = 0;
    *off = selected_off;
    *page_no = selected_page;
    return 1;
}

static int32_t page_for_offset(int64_t target, int64_t *page_start)
{
    Page probe;
    int64_t off = 0;
    int32_t number = 1;
    (void)idx_lookup(target, &off, &number);
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

/* CJK 禁则：优先在合法字符边界换行，避免标点落到行首或开引号落到行尾。 */
static int32_t no_break_before(uint32_t cp)
{
    switch (cp) {
    case '.':
    case ',':
    case ':':
    case ';':
    case '!':
    case '?':
    case ')':
    case ']':
    case '}':
    case 0x00BB:  /* » */
    case 0x2019:  /* ’ */
    case 0x201D:  /* ” */
    case 0x2014:  /* — */
    case 0x2026:  /* … */
    case 0x3001:  /* 、 */
    case 0x3002:  /* 。 */
    case 0x3009:  /* 〉 */
    case 0x300B:  /* 》 */
    case 0x300D:  /* 」 */
    case 0x300F:  /* 』 */
    case 0x3011:  /* 】 */
    case 0x3015:  /* 〕 */
    case 0x3017:  /* 〗 */
    case 0x3019:  /* 〙 */
    case 0x301B:  /* 〛 */
    case 0xFF01:  /* ！ */
    case 0xFF09:  /* ） */
    case 0xFF0C:  /* ， */
    case 0xFF0E:  /* ． */
    case 0xFF1A:  /* ： */
    case 0xFF1B:  /* ； */
    case 0xFF1F:  /* ？ */
    case 0xFF3D:  /* ］ */
    case 0xFF5D:  /* ｝ */
        return 1;
    default:
        return 0;
    }
}

static int32_t no_break_after(uint32_t cp)
{
    switch (cp) {
    case '(':
    case '[':
    case '{':
    case 0x00AB:  /* « */
    case 0x2018:  /* ‘ */
    case 0x201C:  /* “ */
    case 0x3008:  /* 〈 */
    case 0x300A:  /* 《 */
    case 0x300C:  /* 「 */
    case 0x300E:  /* 『 */
    case 0x3010:  /* 【 */
    case 0x3014:  /* 〔 */
    case 0x3016:  /* 〖 */
    case 0x3018:  /* 〘 */
    case 0x301A:  /* 〚 */
    case 0xFF08:  /* （ */
    case 0xFF3B:  /* ［ */
    case 0xFF5B:  /* ｛ */
        return 1;
    default:
        return 0;
    }
}

static int32_t is_combining_mark(uint32_t cp)
{
    return (cp >= 0x0300 && cp <= 0x036F) ||
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

static int32_t line_break_allowed(uint32_t left, uint32_t right)
{
    if (!left || !right) return 1;
    if (is_combining_mark(right)) return 0;
    if (no_break_after(left) || no_break_before(right)) return 0;
    return 1;
}

static void layout_line(Line *line, LineCursor *cur)
{
    line->len = 0;
    line->indent = cur->para_start ? cur->indent_w : 0;
    line->justify = 0;
    cur->x = line->indent;

    int64_t last_break_off = cur->off;
    int32_t last_break_len = 0;
    int32_t last_break_x = cur->x;
    uint32_t prev_cp = 0;

    while (cur->off < s_mmap_len) {
        if (line->len >= MAX_CHARS - 1) break;

        int64_t prev = cur->off;
        uint32_t cp = s_decode((const uint8_t *)s_mmap_data, s_mmap_len, &cur->off);
        if (cp == 0) break;
        if (prev_cp && line_break_allowed(prev_cp, cp)) {
            last_break_off = prev;
            last_break_len = line->len;
            last_break_x = cur->x;
        }
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
            if (last_break_len > 0) {
                cur->off = last_break_off;
                line->len = last_break_len;
                cur->x = last_break_x;
            } else {
                cur->off = prev;
            }
            line->justify = 1;
            break;
        }
        line->text[line->len++] = cp;
        cur->x += cw;
        prev_cp = cp;
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
#ifdef PTEXT_HOST_TEST
    if (s_test_bad_paginate == 1) target->next_offset = target->start_offset;
    if (s_test_bad_paginate == 2)
        target->next_offset = target->start_offset + 1000000;
#endif
}

#ifdef PTEXT_HOST_TEST
void rd_test_paginate_full(int64_t offset)
{
    paginate_full(offset, s_page_curr);
}

int32_t rd_test_line_punct_only(const Line *line)
{
    return line_punct_only(line);
}

int32_t rd_test_line_break_allowed(uint32_t left, uint32_t right)
{
    return line_break_allowed(left, right);
}

int32_t rd_test_idx_lookup(int64_t target, int64_t *off, int32_t *page_no)
{
    return idx_lookup(target, off, page_no);
}

int32_t rd_test_idx_parse(const uint8_t *buf, size_t len,
                          int64_t file_size, int64_t file_mtime,
                          uint32_t layout_hash, int64_t target,
                          int64_t *off, int32_t *page_no,
                          uint32_t *prefix_count)
{
    return idx_parse(buf, len, file_size, file_mtime, layout_hash,
                     target, off, page_no, prefix_count);
}

int32_t rd_test_is_para_start(int64_t off)
{
    return is_para_start(off);
}

int32_t rd_test_load_bookmarks(const char *path, Bookmark *out, int32_t cap)
{
    return load_bookmarks(path, out, cap);
}

int32_t rd_test_read_name_sidecar(const char *path, char *out, size_t cap)
{
    return read_name_sidecar(path, out, cap);
}

int32_t rd_test_sidecar_path(char *out, size_t cap, const char *dir,
                             const char *name)
{
    return sidecar_path(out, cap, dir, name);
}

int32_t rd_test_name_sidecar_path(char *out, size_t cap, const char *path)
{
    return name_sidecar_path(out, cap, path);
}

int32_t rd_test_bookmark_preview(int64_t offset, char *out, int32_t cap)
{
    return bookmark_preview(offset, out, cap);
}

int32_t rd_test_page_max_lines(void)
{
    return page_max_lines();
}

int32_t rd_test_page_max_x(void)
{
    return page_max_x();
}

int32_t rd_test_refresh_bookmark_previews(const char *path, Bookmark *bms,
                                          int32_t count)
{
    refresh_bookmark_previews(path, bms, count);
    return 0;
}

void rd_test_page_checkpoint_add(int64_t offset, int32_t page_no)
{
    page_checkpoint_add(offset, page_no);
}

int32_t rd_test_page_checkpoint_find(int64_t target, int64_t *offset)
{
    return page_checkpoint_find(target, offset);
}

void rd_test_hist_reset(int64_t start)
{
    hist_reset(start);
}

void rd_test_hist_push(int64_t start)
{
    hist_push(start);
}

void rd_test_paginate_init(int64_t offset)
{
    paginate_init(offset);
}

void rd_test_paginate_step(void)
{
    paginate_step();
}

int32_t rd_test_prefetch_state(void)
{
    return s_ctx.state;
}

void rd_test_set_state(const char *name, int64_t len, int64_t start)
{
    static char fake_map[1];
    s_mmap_data = fake_map;
    s_mmap_len = len;
    snprintf(s_cur_name, sizeof(s_cur_name), "%s", name ? name : "");
    s_page_curr->start_offset = start;
}

void rd_test_clear_state(void)
{
    s_mmap_data = NULL;
    s_mmap_len = 0;
    s_cur_name[0] = '\0';
    s_page_curr->start_offset = 0;
}

void rd_test_set_prefetch(int32_t ready, int64_t target)
{
    s_next_ready = ready;
    s_prefetch_target = target;
}

void rd_test_set_page_no(int32_t page_no)
{
    s_page_no = page_no;
}

void rd_test_set_bad_paginate(int32_t mode)
{
    s_test_bad_paginate = mode;
}

int32_t rd_test_page_for_offset(int64_t target, int64_t *start)
{
    return page_for_offset(target, start);
}

void rd_test_set_prev_injection(int64_t probe, int32_t safety)
{
    s_test_prev_probe = probe;
    s_test_prev_safety = safety;
}
#endif

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

    for (int32_t step = 0; step < 24 &&
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
    if (!sidecar_path(ppath, sizeof(ppath), DIR_PROGRESS, s_cur_name)) return;
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
#ifdef PTEXT_HOST_TEST
        if (s_test_prev_probe >= 0) probe = s_test_prev_probe;
#endif
        int32_t safety_limit = s_page_ckpt_count > 1 ? PAGE_CKPT_STEP + 2 : 100000;
#ifdef PTEXT_HOST_TEST
        if (s_test_prev_safety >= 0) safety_limit = s_test_prev_safety;
#endif
        int32_t safety = 0;
        while (probe < s_page_curr->start_offset && safety <= safety_limit) {
            paginate_full(probe, &probe_page);
            if (probe_page.next_offset >= s_page_curr->start_offset) {
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
    if (!sidecar_path(spath, sizeof(spath), DIR_PROGRESS, SETTINGS_NAME)) return;
    persist_write_atomic(spath, &s_settings, sizeof(s_settings));
}

static void load_settings(void)
{
    char spath[1024];
    if (!sidecar_path(spath, sizeof(spath), DIR_PROGRESS, SETTINGS_NAME)) return;
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
                if (rn >= 20) {
                    s_settings.font_idx = clamp_i32(tmp.font_idx, 0, 6);
                    s_settings.indent_idx = clamp_i32(tmp.indent_idx, 0, 2);
                    s_settings.gap_idx = clamp_i32(tmp.gap_idx, 0, 2);
                    s_settings.theme = clamp_i32(tmp.theme, 0, 1);
                    s_settings.affinity_idx = rn >= sizeof(Settings)
                        ? clamp_i32(tmp.affinity_idx, 0, 2) : 0;
                    if (tmp.version >= 14) {
                        s_settings.version = 14;
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
        s_settings.affinity_idx = 0;
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

void rd_set_affinity_idx(int32_t i)
{
    s_settings.affinity_idx = clamp_i32(i, 0, 2);
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
    if (!sidecar_path(path, sizeof(path), DIR_BOOKMARKS, s_cur_name)) return;
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
    if (sidecar_path(side, sizeof(side), DIR_PROGRESS, base)) {
        if (unlink(side) != 0 && errno != ENOENT) side_error = 1;
    } else {
        side_error = 1;
    }
    if (sidecar_path(side, sizeof(side), DIR_BOOKMARKS, base)) {
        if (unlink(side) != 0 && errno != ENOENT) side_error = 1;
    } else {
        side_error = 1;
    }
    if (sidecar_path(side, sizeof(side), DIR_INDEX, base)) {
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
    if (!sidecar_path(bm_path, sizeof(bm_path), DIR_BOOKMARKS, base)) return 0;

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
