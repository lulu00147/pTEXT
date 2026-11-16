#ifndef READER_H
#define READER_H
#include <stdint.h>

typedef struct { char path[512]; char name[256]; int32_t bookmarks; } FileItem;
typedef struct { int64_t offset; char preview[48]; } Bookmark;
typedef struct { int32_t font_idx; int32_t indent_idx; int32_t gap_idx; int32_t theme; int32_t version; /* 新字段追加到末尾以保持二进制兼容 */ } Settings;

void rd_init(const char *data_path);
void rd_scan_dir(void);
FileItem* rd_get_files(int32_t *count);
int32_t rd_open_file(const char *path);
void rd_close(void);

#define MAX_LINES 80
#define MAX_CHARS 200
typedef struct { uint32_t text[MAX_CHARS]; int32_t len; int32_t indent; uint8_t justify; } Line;
typedef struct {
    Line lines[MAX_LINES];
    int32_t count;
    int64_t start_offset;
    int64_t next_offset;
} Page;

void rd_goto_offset(int64_t offset);
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
    int32_t max_lines;
} PageLayout;

PageLayout rd_get_layout(void);

void rd_prefetch_step(void);
void rd_reset_prefetch(void);
int32_t rd_is_prefetching(void);
void rd_save_progress(void);
#endif
