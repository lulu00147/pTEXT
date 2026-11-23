#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reader.h"
#include "render_host.h"
#include "ui_settings.h"
#include "ui_util.h"

static Settings kSettings = {3, 1, 0, 0, 14, 0};
static int32_t kWidth = 600;
static int32_t kHeight = 800;

float r_ui_scale(void) { return 1.0f; }
int32_t r_w(void) { return kWidth; }
int32_t r_h(void) { return kHeight; }
int32_t r_char_advance(uint32_t cp) { return cp < 0x80 ? 10 : 20; }
Settings *rd_get_settings(void) { return &kSettings; }

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
    int32_t l, t, r, b;
    ui_get_settings_rect(&l, &t, &r, &b);
    assert(l == 120 && t == 255 && r == 480 && b == 545);

    assert(ui_settings_row_at(t + 49) == -1);
    assert(ui_settings_row_at(t + 50) == 0);
    assert(ui_settings_row_at(t + 99) == 0);
    assert(ui_settings_row_at(t + 100) == 1);
    assert(ui_settings_row_at(t + 149) == 1);
    assert(ui_settings_row_at(t + 150) == 2);
    assert(ui_settings_row_at(t + 199) == 2);
    assert(ui_settings_row_at(t + 200) == 3);
    assert(ui_settings_row_at(t + 239) == 3);
    assert(ui_settings_row_at(t + 250) == 4);
    assert(ui_settings_row_at(t + 289) == 4);

    assert(ui_settings_contains(l, t));
    assert(ui_settings_contains(r, b));
    assert(!ui_settings_contains(l - 1, t));
    assert(!ui_settings_contains(r + 1, b));
    assert(!ui_settings_contains(l, b + 1));
    assert(!ui_settings_contains(r, t - 1));
    assert(ui_settings_button_at(l) == 0);
    assert(ui_settings_button_at(r - 98) == 0);
    assert(ui_settings_button_at(r - 97) == 1);
    assert(ui_settings_button_at(r) == 1);

    uint32_t text[4] = {'A', 'B', 'C', 0};
    assert(ui_fit_text(text, 3, 25) == 2);
    assert(text[1] == 0x2026);
    uint32_t one[2] = {'A', 0};
    assert(ui_fit_text(one, 1, 1) == 1);
    assert(ui_fit_text(NULL, 2, 10) == 0);
    assert(ui_fit_text(one, 0, 10) == 0);
    uint32_t text2[3] = {'A', 'B', 'C'};
    assert(ui_fit_text(text2, 3, 100) == 3);
    assert(ui_fit_text(text2, 3, 0) == 1);

    uint32_t utf32[8];
    assert(ui_utf8_to_utf32(utf32, 8, "A中") == 2);
    assert(utf32[0] == 'A' && utf32[1] == 0x4E2D);
    assert(ui_utf8_to_utf32(utf32, 1, "A中") == 1);
    assert(ui_utf8_to_utf32(NULL, 1, "A") == 0);
    assert(ui_utf8_to_utf32(utf32, 0, "A") == 0);
    assert(ui_utf8_to_utf32(utf32, 8, NULL) == 0);
    assert(ui_utf8_to_utf32(utf32, 8, "\xFF") == 1);
    assert(utf32[0] == 0xFFFD);

    kSettings.font_idx = 99;
    kSettings.indent_idx = -5;
    kSettings.gap_idx = 9;
    kSettings.theme = 7;
    ui_draw_settings();
    kSettings.font_idx = 3;
    kSettings.indent_idx = 1;
    kSettings.gap_idx = 0;
    kSettings.theme = 0;
    ui_draw_settings();

    kWidth = 200;
    kHeight = 160;
    ui_get_settings_rect(&l, &t, &r, &b);
    assert(l < 0 && t < 0 && r > l && b > t);
    assert(!ui_settings_contains(-100, -100));
    assert(ui_settings_row_at(t + 50) == 0);
    assert(ui_settings_row_at(t + 49) == -1);
    ui_draw_settings();
    puts("ui settings tests passed");

    return 0;
}
