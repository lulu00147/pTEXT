#include <string.h>

static int32_t s_test_bad_paginate;
static int64_t s_test_prev_probe = -1;
static int32_t s_test_prev_safety = -1;

int64_t reader_test_paginate_next(int64_t start, int64_t next)
{
    if (s_test_bad_paginate == 1) return start;
    if (s_test_bad_paginate == 2) return start + 1000000;
    return next;
}

int64_t reader_test_prev_probe(int64_t probe)
{
    return s_test_prev_probe >= 0 ? s_test_prev_probe : probe;
}

int32_t reader_test_prev_safety(int32_t limit)
{
    return s_test_prev_safety >= 0 ? s_test_prev_safety : limit;
}

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

void rd_test_sidecar_path(char *out, size_t cap, const char *dir,
                          const char *name)
{
    sidecar_path(out, cap, dir, name);
}

void rd_test_name_sidecar_path(char *out, size_t cap, const char *path)
{
    name_sidecar_path(out, cap, path);
}

int32_t rd_test_bookmark_preview(int64_t offset, char *out, int32_t cap)
{
    return bookmark_preview((const uint8_t *)s_mmap_data, s_mmap_len,
                            s_decode, offset, out, cap);
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

void rd_test_reset_checkpoints(void)
{
    s_page_ckpt_count = 0;
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

int32_t rd_test_bookmark_count(const char *path)
{
    return bookmark_count(path);
}

int32_t rd_test_bm_legacy_count(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int32_t n = bm_legacy_count(f);
    fclose(f);
    return n;
}

void rd_test_cleanup_sidecars(const char *files)
{
    cleanup_orphans(files);
}

void rd_test_idx_save_guard(void)
{
    static char probe[2];
    s_mmap_data = probe;
    s_mmap_len = IDX_MIN_FILE;
    s_idx_dirty = 1;
    s_page_ckpt_count = 0;
    idx_save();
}

int32_t rd_test_bookmark_preview_null(void)
{
    static char out[8];
    return bookmark_preview(NULL, 1, enc_decode_utf8, 0, out, sizeof(out));
}

int32_t rd_test_bookmark_preview_bad_decode(void)
{
    static const uint8_t data[] = {'A'};
    static char out[8];
    return bookmark_preview(data, 1, NULL, 0, out, sizeof(out));
}

void rd_test_page_checkpoint(int32_t page_no)
{
    maybe_page_checkpoint(0, page_no);
}

int32_t rd_test_has_break(int32_t length)
{
    return has_break_candidate(length);
}

void rd_test_apply_break(int32_t hit)
{
    Line line;
    LineCursor cur;
    memset(&line, 0, sizeof(line));
    memset(&cur, 0, sizeof(cur));
    apply_break(&line, &cur, 0, 0, hit ? 1 : 0, 0);
}

int32_t rd_test_prev_safety_limit(int32_t ckpt_count)
{
    return prev_safety_limit(ckpt_count);
}

void rd_test_layout_break(void)
{
    static const char data[] =
        "aaaa bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    Line line;
    LineCursor cur;
    memset(&cur, 0, sizeof(cur));
    s_mmap_data = (char *)data;
    s_mmap_len = (int64_t)(sizeof(data) - 1);
    s_decode = enc_decode_utf8;
    snprintf(s_cur_name, sizeof(s_cur_name), "%s", "layout.txt");
    cur.max_x = 80;
    layout_line(&line, &cur);
}
