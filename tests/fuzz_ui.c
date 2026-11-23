#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "reader.h"
#include "ui_lists.h"
#include "ui_settings.h"
#include "ui_reading.h"

static FileItem files[8];
static Bookmark bms[8];
static Settings settings;
static Page page;
static PageLayout layout;
static int32_t width, height;
static int32_t limit;
static int32_t page_no;
static int32_t bookmarked;
static char book_name[64];

float r_ui_scale(void) { return 1.0f; }
int32_t r_w(void) { return width; }
int32_t r_h(void) { return height; }
int32_t r_font_h(void) { return 20; }
int32_t r_char_advance(uint32_t cp) { return cp < 0x80 ? 10 : 20; }
int32_t r_text_width(const uint32_t *s, int32_t n)
{
    int32_t w = 0;
    for (int32_t i = 0; i < n; i++) w += r_char_advance(s[i]);
    return w;
}
int32_t r_text_width_at_size(const uint32_t *s, int32_t n, float size)
{
    (void)size;
    return r_text_width(s, n);
}
void r_clear(void) {}
void r_clear_rect(int32_t l, int32_t t, int32_t r, int32_t b)
{
    (void)l; (void)t; (void)r; (void)b;
}
void r_stroke_rect(int32_t l, int32_t t, int32_t r, int32_t b, int32_t w)
{
    (void)l; (void)t; (void)r; (void)b; (void)w;
}
void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n)
{
    (void)x; (void)y; (void)s; (void)n;
}
void r_set_bg_only(int32_t on) { (void)on; }
void r_draw_text_justified(int32_t x, int32_t y, const uint32_t *s, int32_t n,
                           int32_t target_width)
{
    (void)x; (void)y; (void)s; (void)n; (void)target_width;
}
void r_draw_text_at_size_color(int32_t x, int32_t y, const uint32_t *s, int32_t n,
                               float size, uint16_t color)
{
    (void)x; (void)y; (void)s; (void)n; (void)size; (void)color;
}
void r_prepare_parallel(void) {}
int32_t r_preload_text(const uint32_t *s, int32_t n) { (void)s; return n >= 0; }
int32_t r_preload_cp(uint32_t cp) { (void)cp; return 1; }
void r_fast_begin(void) {}
int r_fast_prepare(void) { return 1; }
void r_fast_end(void) {}
void affinity_bind_current(void) {}

FileItem *rd_get_files(int32_t *count)
{
    if (count) *count = 8;
    return files;
}
int32_t rd_file_limit_reached(void) { return limit; }
Settings *rd_get_settings(void) { return &settings; }
Page *rd_get_page(void) { return &page; }
PageLayout rd_get_layout(void) { return layout; }
const char *rd_get_book_name(void) { return book_name; }
int32_t rd_get_page_no(void) { return page_no; }
int32_t rd_page_bookmarked(void) { return bookmarked; }
int32_t rd_line_blank(const Line *line) { return line->len == 0; }
int64_t rd_get_file_len(void) { return 100000; }

static uint32_t take(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size < 8) return 0;
    uint32_t s = (uint32_t)data[0] | ((uint32_t)data[1] << 8);
    width = 320 + (int32_t)(take(&s) % 900);
    height = 480 + (int32_t)(take(&s) % 1200);
    limit = (int32_t)(take(&s) & 1);

    memset(files, 0, sizeof(files));
    memset(bms, 0, sizeof(bms));
    memset(&settings, 0, sizeof(settings));
    memset(&page, 0, sizeof(page));
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        files[i].bookmarks = (int32_t)(take(&s) & 7);
        size_t n = size > i ? (size_t)(data[i] % 32) : 0;
        for (size_t j = 0; j < n; j++) files[i].name[j] = (char)('a' + (data[(i + j) % size] % 26));
        files[i].name[n < sizeof(files[i].name) ? n : sizeof(files[i].name) - 1] = '\0';
    }
    for (size_t i = 0; i < sizeof(bms) / sizeof(bms[0]); i++) {
        bms[i].offset = (int64_t)(take(&s) % 100000);
        size_t n = (size_t)(data[i % size] % 24);
        for (size_t j = 0; j < n; j++) bms[i].preview[j] = (char)('A' + (data[(i + j) % size] % 26));
        bms[i].preview[n < sizeof(bms[i].preview) ? n : sizeof(bms[i].preview) - 1] = '\0';
    }

    settings.font_idx = (int32_t)(take(&s) % 12) - 2;
    settings.indent_idx = (int32_t)(take(&s) % 6) - 2;
    settings.gap_idx = (int32_t)(take(&s) % 6) - 2;
    settings.theme = (int32_t)(take(&s) % 3) - 1;
    settings.affinity_idx = (int32_t)(take(&s) % 6) - 2;
    layout.margin_x = (int32_t)(take(&s) % 80) - 10;
    layout.margin_top = (int32_t)(take(&s) % 120) - 20;
    layout.margin_bot = (int32_t)(take(&s) % 120) - 20;
    layout.line_height = (int32_t)(take(&s) % 40) - 5;
    layout.text_width = (int32_t)(take(&s) % 900) - 50;
    layout.page_height = (int32_t)(take(&s) % 1200) - 50;
    page_no = (int32_t)(take(&s) % 5000) - 10;
    bookmarked = (int32_t)(take(&s) & 1);
    page.start_offset = (int64_t)(take(&s) % 100000);
    page.next_offset = (int64_t)(take(&s) % 100000);
    page.count = (int32_t)(take(&s) % 6);
    for (int32_t i = 0; i < page.count && i < MAX_LINES; i++) {
        page.lines[i].len = (int32_t)(take(&s) % 12);
        for (int32_t j = 0; j < page.lines[i].len && j < 200; j++)
            page.lines[i].text[j] = (uint32_t)(take(&s) % 0x3000);
        page.lines[i].justify = (int32_t)(take(&s) & 1);
    }
    size_t bn = (size_t)(take(&s) % (sizeof(book_name) - 1));
    for (size_t i = 0; i < bn; i++) book_name[i] = (char)('a' + (data[i % size] % 26));
    book_name[bn] = '\0';

    int32_t scroll = (int32_t)(take(&s) % 2000) - 1000;
    ui_draw_file_list(&scroll, (int32_t)(take(&s) & 3));
    ui_draw_bookmarks(bms, (int32_t)(take(&s) % 9), &scroll);
    ui_draw_delete_dialog(files[0].name);
    ui_draw_settings();
    ui_draw_reading();
    (void)ui_file_max_scroll((int32_t)(take(&s) % 40));
    (void)ui_file_index_at((int32_t)(take(&s) % 1200) - 100, scroll);
    (void)ui_file_mark_left();
    (void)ui_file_import_hit((int32_t)(take(&s) % 1200) - 100);
    (void)ui_bm_max_scroll((int32_t)(take(&s) % 40));
    (void)ui_bm_index_at((int32_t)(take(&s) % 1200) - 100, scroll);
    (void)ui_settings_contains((int32_t)(take(&s) % 1200) - 100,
                               (int32_t)(take(&s) % 1200) - 100);
    (void)ui_settings_row_at((int32_t)(take(&s) % 1200) - 100);
    (void)ui_settings_button_at((int32_t)(take(&s) % 1200) - 100);
    (void)ui_settings_affinity_button_at((int32_t)(take(&s) % 1200) - 100);
    return 0;
}
