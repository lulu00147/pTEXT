#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reader.h"
#include "render_host.h"
#include "ui_lists.h"

static FileItem kFiles[32];
static int32_t kFileCount;
static int32_t kLimit;
static int32_t kWidth = 600;
static int32_t kHeight = 800;

float r_ui_scale(void) { return 1.0f; }
int32_t r_w(void) { return kWidth; }
int32_t r_h(void) { return kHeight; }
int32_t r_font_h(void) { return 20; }
int32_t r_char_advance(uint32_t cp) { return cp < 0x80 ? 10 : 20; }
int32_t r_text_width(const uint32_t *s, int32_t n)
{
    int32_t w = 0;
    for (int32_t i = 0; i < n; i++) w += r_char_advance(s[i]);
    return w;
}

FileItem *rd_get_files(int32_t *count)
{
    if (count) *count = kFileCount;
    return kFiles;
}
int32_t rd_file_limit_reached(void) { return kLimit; }

void r_clear(void) {}
void r_clear_rect(int32_t l, int32_t t, int32_t r, int32_t b)
{
    (void)l; (void)t; (void)r; (void)b;
}
void r_stroke_rect(int32_t l, int32_t t, int32_t r, int32_t b, int32_t width)
{
    (void)l; (void)t; (void)r; (void)b; (void)width;
}
void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n)
{
    (void)x; (void)y; (void)s; (void)n;
}

int main(void)
{
    assert(ui_file_max_scroll(0) == 0);
    assert(ui_file_max_scroll(1) == 0);
    assert(ui_file_max_scroll(20) > 0);
    assert(ui_bm_max_scroll(0) == 0);
    assert(ui_bm_max_scroll(20) > 0);

    kFileCount = 3;
    memset(kFiles, 0, sizeof(kFiles));
    snprintf(kFiles[0].name, sizeof(kFiles[0].name), "a.txt");
    snprintf(kFiles[1].name, sizeof(kFiles[1].name), "b.txt");
    snprintf(kFiles[2].name, sizeof(kFiles[2].name), "c.txt");
    kFiles[1].bookmarks = 2;

    assert(ui_file_index_at(79, 0) == -1);
    assert(ui_file_index_at(80, 0) == 0);
    assert(ui_file_index_at(159, 0) == 0);
    assert(ui_file_index_at(160, 0) == 1);
    assert(ui_file_index_at(700, 0) == -1);
    assert(ui_file_index_at(100, 100) == 1);
    assert(ui_file_index_at(100, 10000) == -1);
    assert(ui_file_index_at(80, -1000) == -1);

    assert(ui_file_mark_left() == 520);
    assert(!ui_file_import_hit(700));
    assert(ui_file_import_hit(701));

    assert(ui_bm_index_at(89, 0) == -1);
    assert(ui_bm_index_at(90, 0) == 0);
    assert(ui_bm_index_at(149, 0) == 0);
    assert(ui_bm_index_at(150, 0) == 1);
    assert(ui_bm_index_at(-1, 0) == -1);

    int32_t scroll = 0;
    kFileCount = 0;
    assert(ui_file_index_at(100, 0) == -1);
    ui_draw_file_list(&scroll, 0);
    kFileCount = 3;
    ui_draw_file_list(&scroll, 0);
    ui_draw_file_list(&scroll, 1);
    ui_draw_file_list(NULL, 0);
    scroll = 100000;
    ui_draw_file_list(&scroll, 0);
    scroll = -100000;
    ui_draw_file_list(&scroll, 0);
    ui_draw_file_list(&scroll, 2);
    ui_draw_file_list(&scroll, 3);
    kLimit = 1;
    ui_draw_file_list(&scroll, 0);
    kLimit = 0;

    for (int32_t count = 0; count <= 40; count++) {
        kFileCount = count;
        int32_t max = ui_file_max_scroll(count);
        assert(max >= 0);
        for (int32_t y = -200; y <= 1200; y += 7) {
            int32_t idx = ui_file_index_at(y, max / 2);
            assert(idx >= -1 && idx < count);
            int32_t bi = ui_bm_index_at(y, max / 2);
            assert(bi >= -1);
        }
        assert(ui_file_mark_left() >= 0);
    }
    kFileCount = 3;

    Bookmark bm;
    memset(&bm, 0, sizeof(bm));
    snprintf(bm.preview, sizeof(bm.preview), "preview");
    ui_draw_bookmarks(&bm, 1, &scroll);
    ui_draw_bookmarks(&bm, 1, NULL);
    ui_draw_bookmarks(NULL, 0, &scroll);
    ui_draw_bookmarks(NULL, 0, NULL);
    ui_draw_delete_dialog("a.txt");
    ui_draw_delete_dialog(NULL);

    Bookmark bms[20];
    memset(bms, 0, sizeof(bms));
    for (int32_t i = 0; i < 20; i++) snprintf(bms[i].preview, sizeof(bms[i].preview), "p%d", i);
    for (int32_t i = 3; i < 20; i++) {
        snprintf(kFiles[i].name, sizeof(kFiles[i].name), "f%d.txt", i);
    }
    kFileCount = 20;
    scroll = 0;
    ui_draw_file_list(&scroll, 0);
    scroll = 100000;
    ui_draw_file_list(&scroll, 0);
    ui_draw_bookmarks(bms, 20, &scroll);
    scroll = 70;
    ui_draw_bookmarks(bms, 20, &scroll);
    scroll = 0;
    ui_draw_bookmarks(bms, 20, &scroll);
    kFileCount = 3;

    kWidth = 200;
    kHeight = 160;
    assert(ui_file_max_scroll(20) > 0);
    assert(ui_file_index_at(0, 0) == -1);
    assert(ui_file_mark_left() == 120);
    assert(!ui_file_import_hit(60));
    assert(ui_file_import_hit(61));
    kFileCount = 20;
    ui_draw_file_list(&scroll, 0);
    ui_draw_bookmarks(bms, 20, &scroll);
    kFileCount = 3;
    ui_draw_file_list(&scroll, 0);
    ui_draw_bookmarks(&bm, 1, &scroll);
    ui_draw_delete_dialog("a.txt");

    puts("ui lists tests passed");
    return 0;
}
