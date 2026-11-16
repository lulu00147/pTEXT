#include "ui_lists.h"
#include "render.h"
#include "ui_util.h"

#include <stdio.h>

#define LX_MARGIN       ui_px(30)
#define LY_TITLE        ui_px(50)
#define LY_ITEM0        ui_px(120)
#define ITEM_H          ui_px(80)
#define BM_ITEM_H       ui_px(60)
#define LY_BOTTOM_BTN   ui_px(80)
#define BOTTOM_BTN_ZONE ui_px(100)
#define DELETE_W        ui_px(320)
#define DELETE_H        ui_px(200)
#define UI_LEN(arr)     (sizeof(arr)/sizeof(uint32_t) - 1)

static const uint32_t UI_TITLE[] = {0x70,0x54,0x45,0x58,0x54,0};
static const uint32_t UI_EMPTY[] = {0x6682,0x65E0,0x56FE,0x4E66,0xFF0C,0x70B9,
                                    0x51FB,0x4E0B,0x65B9,0x5BFC,0x5165,0};
static const uint32_t UI_IMPORT[] = {0x5B,0x20,0x2B,0x20,0x5D,0};
static const uint32_t UI_BM_TITLE[] = {0x2D,0x2D,0x20,0x4E66,0x7B7E,0x5217,
                                       0x8868,0x20,0x2D,0x2D,0};
static const uint32_t UI_BM_EMPTY[] = {0x6682,0x65E0,0x4E66,0x7B7E,0};
static const uint32_t UI_BM_MARK[] = {0x5B,0x20,0x2A,0x20,0x5D,0};
static const uint32_t UI_OPEN_FAIL[] = {0x65E0,0x6CD5,0x6253,0x5F00,0x6587,0x4EF6,0};
static const uint32_t UI_DELETE_FAIL[] = {0x5220,0x9664,0x5931,0x8D25,0};

int32_t ui_file_max_scroll(int32_t count)
{
    int32_t top = LY_ITEM0 - ITEM_H / 2;
    int32_t bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t visible = bottom - top;
    if (visible < ITEM_H) visible = ITEM_H;
    int32_t max_scroll = count * ITEM_H - visible;
    return max_scroll > 0 ? max_scroll : 0;
}

int32_t ui_file_index_at(int32_t y, int32_t scroll)
{
    int32_t count;
    rd_get_files(&count);
    int32_t list_top = LY_ITEM0 - ITEM_H / 2;
    int32_t iy = y + scroll;
    if (count <= 0 || iy < list_top || iy >= list_top + count * ITEM_H) return -1;
    int32_t idx = (iy - list_top) / ITEM_H;
    return idx >= 0 && idx < count ? idx : -1;
}

int32_t ui_file_mark_left(void)
{
    int32_t w = r_text_width(UI_BM_MARK, UI_LEN(UI_BM_MARK));
    return r_w() - w - LX_MARGIN;
}

int32_t ui_file_import_hit(int32_t y)
{
    return y > r_h() - BOTTOM_BTN_ZONE;
}

void ui_draw_file_list(int32_t *scroll_ptr, int32_t error)
{
    r_clear();
    int32_t scroll = scroll_ptr ? *scroll_ptr : 0;
    int32_t count;
    FileItem *files = rd_get_files(&count);
    int32_t max_scroll = ui_file_max_scroll(count);
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0) scroll = 0;
    if (scroll_ptr) *scroll_ptr = scroll;

    r_draw_text(LX_MARGIN, LY_TITLE, UI_TITLE, UI_LEN(UI_TITLE));
    int32_t list_top = LY_ITEM0 - ITEM_H / 2;
    int32_t list_bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t max_text_w = r_w() - LX_MARGIN * 2;
    int32_t bm_w = r_text_width(UI_BM_MARK, UI_LEN(UI_BM_MARK));

    if (count == 0) {
        r_draw_text(LX_MARGIN, LY_ITEM0 + ITEM_H / 2, UI_EMPTY, UI_LEN(UI_EMPTY));
    } else {
        for (int32_t i = 0; i < count; i++) {
            int32_t row_y = LY_ITEM0 + i * ITEM_H - scroll;
            int32_t y = row_y + (ITEM_H - r_font_h()) / 2;
            if (row_y + ITEM_H <= list_top || row_y >= list_bottom) continue;
            if (y < list_top || y + r_font_h() > list_bottom) continue;

            int32_t row_max_w = max_text_w;
            if (files[i].bookmarks > 0) row_max_w -= bm_w + ui_px(16);

            uint32_t txt[256];
            int32_t n = ui_utf8_to_utf32(txt, 255, files[i].name);
            int32_t full_n = n;
            while (n > 1 && r_text_width(txt, n) > row_max_w) n--;
            if (full_n > n && n > 0) txt[n - 1] = 0x2026;
            r_draw_text(LX_MARGIN, y, txt, n);

            if (files[i].bookmarks > 0) {
                r_draw_text(r_w() - bm_w - LX_MARGIN, y,
                            UI_BM_MARK, UI_LEN(UI_BM_MARK));
            }
        }
    }

    int32_t import_w = r_text_width(UI_IMPORT, UI_LEN(UI_IMPORT));
    r_draw_text(r_w() - import_w - LX_MARGIN, r_h() - LY_BOTTOM_BTN,
                UI_IMPORT, UI_LEN(UI_IMPORT));
    if (error == 1) {
        r_draw_text(LX_MARGIN, r_h() - BOTTOM_BTN_ZONE,
                    UI_OPEN_FAIL, UI_LEN(UI_OPEN_FAIL));
    } else if (error == 2) {
        r_draw_text(LX_MARGIN, r_h() - BOTTOM_BTN_ZONE,
                    UI_DELETE_FAIL, UI_LEN(UI_DELETE_FAIL));
    }
}

int32_t ui_bm_max_scroll(int32_t count)
{
    int32_t top = LY_ITEM0 - BM_ITEM_H / 2;
    int32_t bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t visible = bottom - top;
    if (visible < BM_ITEM_H) visible = BM_ITEM_H;
    int32_t max_scroll = count * BM_ITEM_H - visible;
    return max_scroll > 0 ? max_scroll : 0;
}

int32_t ui_bm_index_at(int32_t y, int32_t scroll)
{
    int32_t list_top = LY_ITEM0 - BM_ITEM_H / 2;
    int32_t iy = y + scroll;
    if (iy < list_top) return -1;
    return (iy - list_top) / BM_ITEM_H;
}

void ui_draw_bookmarks(const Bookmark *bms, int32_t count, int32_t *scroll_ptr)
{
    r_clear();
    r_draw_text(LX_MARGIN, LY_TITLE, UI_BM_TITLE, UI_LEN(UI_BM_TITLE));
    int32_t scroll = scroll_ptr ? *scroll_ptr : 0;
    int32_t max_scroll = ui_bm_max_scroll(count);
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0) scroll = 0;
    if (scroll_ptr) *scroll_ptr = scroll;

    int32_t list_top = LY_ITEM0 - BM_ITEM_H / 2;
    int32_t list_bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t max_text_w = r_w() - LX_MARGIN * 2;

    if (count == 0) {
        r_draw_text(LX_MARGIN, LY_ITEM0 + BM_ITEM_H / 2,
                    UI_BM_EMPTY, UI_LEN(UI_BM_EMPTY));
        return;
    }

    for (int32_t i = 0; i < count; i++) {
        int32_t row_y = LY_ITEM0 + i * BM_ITEM_H - scroll;
        int32_t y = row_y + (BM_ITEM_H - r_font_h()) / 2;
        if (row_y + BM_ITEM_H <= list_top || row_y >= list_bottom) continue;
        if (y < list_top || y + r_font_h() > list_bottom) continue;

        char buf[64];
        snprintf(buf, sizeof(buf), "%d. %s", i + 1, bms[i].preview);
        uint32_t txt[64];
        int32_t n = ui_utf8_to_utf32(txt, 63, buf);
        int32_t full_n = n;
        while (n > 1 && r_text_width(txt, n) > max_text_w) n--;
        if (full_n > n && n > 0) txt[n - 1] = 0x2026;
        r_draw_text(LX_MARGIN, y, txt, n);
    }
}

void ui_get_delete_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    *l = r_w() / 2 - DELETE_W / 2;
    *t = r_h() / 2 - DELETE_H / 2;
    *r = *l + DELETE_W;
    *b = *t + DELETE_H;
}

void ui_draw_delete_dialog(const char *name)
{
    int32_t l, t, r, b;
    ui_get_delete_rect(&l, &t, &r, &b);
    r_clear_rect(l, t, r, b);
    r_stroke_rect(l, t, r, b, ui_px(2));

    const uint32_t title[] = {0x5220,0x9664,0x6587,0x4EF6,0xFF1F};
    const uint32_t cancel[] = {0x53D6,0x6D88};
    const uint32_t del[] = {0x5220,0x9664};
    r_draw_text(l + ui_px(24), t + ui_px(28), title, 5);

    uint32_t txt[256];
    int32_t n = ui_utf8_to_utf32(txt, 255, name ? name : "");
    int32_t full_n = n;
    int32_t maxw = (r - l) - ui_px(48);
    while (n > 1 && r_text_width(txt, n) > maxw) n--;
    if (full_n > n && n > 0) txt[n - 1] = 0x2026;
    r_draw_text(l + ui_px(24), t + ui_px(90), txt, n);

    r_draw_text(l + ui_px(40), b - ui_px(56), cancel, 2);
    int32_t dw = r_text_width(del, 2);
    r_draw_text(r - dw - ui_px(40), b - ui_px(56), del, 2);
}
