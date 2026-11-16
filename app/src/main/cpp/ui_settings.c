#include "ui_settings.h"
#include "reader.h"
#include "render.h"
#include "ui_util.h"

#include <stdio.h>

#define SETTINGS_W      ui_px(360)
#define SETTINGS_H      ui_px(240)
#define SETTINGS_ROW_H  ui_px(50)

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void ui_get_settings_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    *l = r_w() / 2 - SETTINGS_W / 2;
    *t = r_h() / 2 - SETTINGS_H / 2;
    *r = *l + SETTINGS_W;
    *b = *t + SETTINGS_H;
}

int32_t ui_settings_contains(int32_t x, int32_t y)
{
    int32_t l, t, r, b;
    ui_get_settings_rect(&l, &t, &r, &b);
    return x >= l && x <= r && y >= t && y <= b;
}

int32_t ui_settings_row_at(int32_t y)
{
    int32_t l, t, r, b;
    ui_get_settings_rect(&l, &t, &r, &b);
    int32_t rel = y - t - ui_px(50);
    return rel >= 0 ? (rel + SETTINGS_ROW_H / 2) / SETTINGS_ROW_H : -1;
}

int32_t ui_settings_button_at(int32_t x)
{
    int32_t l, t, r, b;
    ui_get_settings_rect(&l, &t, &r, &b);
    return x < r - ui_px(97) ? 0 : 1;
}

static void draw_toggle_buttons(int32_t r, int32_t y, uint32_t left, uint32_t right)
{
    uint32_t lbtn[3] = {'[', left, ']'};
    uint32_t rbtn[3] = {'[', right, ']'};
    r_draw_text(r - ui_px(130), y, lbtn, 3);
    r_draw_text(r - ui_px(65), y, rbtn, 3);
}

void ui_draw_settings(void)
{
    int32_t l, t, r, b;
    ui_get_settings_rect(&l, &t, &r, &b);
    r_clear_rect(l, t, r, b);
    r_stroke_rect(l, t, r, b, ui_px(2));

    uint32_t utxt[128];
    int32_t n;
    Settings *s = rd_get_settings();
    int32_t font_idx = clamp_i(s->font_idx, 0, 6);
    int32_t indent_idx = clamp_i(s->indent_idx, 0, 2);
    int32_t gap_idx = clamp_i(s->gap_idx, 0, 2);
    int32_t theme = clamp_i(s->theme, 0, 1);
    int32_t y = t + ui_px(50);
    char buf[64];

    snprintf(buf, sizeof(buf), "字号: %d", 18 + font_idx * 2);
    n = ui_utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l + ui_px(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;

    snprintf(buf, sizeof(buf), "缩进: %d字符", indent_idx * 2);
    n = ui_utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l + ui_px(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;

    const char *gaps[] = {"0", "0.5", "1.0"};
    snprintf(buf, sizeof(buf), "行距: %s", gaps[gap_idx]);
    n = ui_utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l + ui_px(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;

    snprintf(buf, sizeof(buf), "主题: %s", theme ? "日间" : "夜间");
    n = ui_utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l + ui_px(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+');
}
