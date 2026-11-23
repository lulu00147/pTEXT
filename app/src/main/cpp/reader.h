#ifndef READER_H
#define READER_H
#include <stdint.h>
#include <stddef.h>

typedef struct { char path[1024]; char name[256]; int32_t bookmarks; } FileItem;
typedef struct { int64_t offset; char preview[48]; } Bookmark;
typedef struct { int32_t font_idx; int32_t indent_idx; int32_t gap_idx; int32_t theme; int32_t version; int32_t affinity_idx; /* 新字段追加到末尾以保持二进制兼容 */ } Settings;

void rd_init(const char *data_path);
void rd_scan_dir(void);
FileItem* rd_get_files(int32_t *count);
int32_t rd_file_limit_reached(void);
int32_t rd_open_file(const char *path);
void rd_close(void);

#define MAX_LINES 80
#define MAX_CHARS 200
#define MAX_BOOKMARKS 50
typedef struct { uint32_t text[MAX_CHARS]; int32_t len; int32_t indent; uint8_t justify; } Line;
typedef struct {
    Line lines[MAX_LINES];
    int32_t count;
    int64_t start_offset;
    int64_t next_offset;
} Page;

int32_t rd_line_blank(const Line *line);

void rd_goto_offset(int64_t offset);
void rd_jump_to_offset(int64_t offset);
void rd_reset_page_cache(void);
void rd_prev_page(void);
void rd_next_page(void);
int32_t rd_can_prev(void);
int32_t rd_can_next(void);
Page* rd_get_page(void);
int32_t rd_get_page_no(void);
const char* rd_get_book_name(void);
int64_t rd_get_file_len(void);
Settings* rd_get_settings(void);
void rd_set_font_idx(int32_t i);
void rd_set_indent_idx(int32_t i);
void rd_set_gap_idx(int32_t i);
void rd_set_affinity_idx(int32_t i);
int32_t rd_get_line_h(void);
int32_t rd_get_indent_w(void);
void rd_toggle_bookmark(void);
int32_t rd_page_bookmarked(void);
int32_t rd_delete_file(const char *path);
int32_t rd_read_bookmarks(const char *path, Bookmark *out, int32_t cap);
void rd_set_theme(int32_t t);

typedef struct {
    int32_t margin_x;
    int32_t margin_top;
    int32_t margin_bot;
    int32_t line_height;
    int32_t text_width;
    int32_t page_height;
} PageLayout;

PageLayout rd_get_layout(void);

void rd_prefetch_step(void);
void rd_reset_prefetch(void);
int32_t rd_is_prefetching(void);
void rd_save_progress(void);
#ifdef PTEXT_HOST_TEST
void rd_test_paginate_full(int64_t offset);
int32_t rd_test_line_punct_only(const Line *line);
int32_t rd_test_line_break_allowed(uint32_t left, uint32_t right);
int32_t rd_test_idx_lookup(int64_t target, int64_t *off, int32_t *page_no);
int32_t rd_test_idx_parse(const uint8_t *buf, size_t len,
                          int64_t file_size, int64_t file_mtime,
                          uint32_t layout_hash, int64_t target,
                          int64_t *off, int32_t *page_no,
                          uint32_t *prefix_count);
int32_t rd_test_is_para_start(int64_t off);
int32_t rd_test_load_bookmarks(const char *path, Bookmark *out, int32_t cap);
int32_t rd_test_read_name_sidecar(const char *path, char *out, size_t cap);
int32_t rd_test_sidecar_path(char *out, size_t cap, const char *dir,
                             const char *name);
int32_t rd_test_name_sidecar_path(char *out, size_t cap, const char *path);
int32_t rd_test_bookmark_preview(int64_t offset, char *out, int32_t cap);
int32_t rd_test_page_max_lines(void);
int32_t rd_test_page_max_x(void);
int32_t rd_test_refresh_bookmark_previews(const char *path, Bookmark *bms,
                                          int32_t count);
void rd_test_page_checkpoint_add(int64_t offset, int32_t page_no);
int32_t rd_test_page_checkpoint_find(int64_t target, int64_t *offset);
void rd_test_hist_reset(int64_t start);
void rd_test_hist_push(int64_t start);
void rd_test_paginate_init(int64_t offset);
void rd_test_paginate_step(void);
int32_t rd_test_prefetch_state(void);
void rd_test_set_state(const char *name, int64_t len, int64_t start);
void rd_test_clear_state(void);
void rd_test_reset_checkpoints(void);
int32_t rd_test_bookmark_count(const char *path);
int32_t rd_test_bm_legacy_count(const char *path);
void rd_test_cleanup_sidecars(const char *files);
void rd_test_idx_save_guard(void);
int32_t rd_test_bookmark_preview_null(void);
int32_t rd_test_bookmark_preview_bad_decode(void);
int32_t rd_test_page_needs_checkpoint(int32_t page_no);
int32_t rd_test_has_break(int32_t length);
void rd_test_apply_break(int32_t hit);
int32_t rd_test_prev_safety_limit(int32_t ckpt_count);
void rd_test_layout_break(void);
void rd_test_set_prefetch(int32_t ready, int64_t target);
void rd_test_set_page_no(int32_t page_no);
void rd_test_set_bad_paginate(int32_t mode);
int32_t rd_test_page_for_offset(int64_t target, int64_t *start);
void rd_test_set_prev_injection(int64_t probe, int32_t safety);
#endif
#endif
