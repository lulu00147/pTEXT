#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "glyph_blit8.h"

#define DW 40
#define DH 16
#define DS 44

static void ref_blit(uint16_t *dst, const GlyphBlit8 *g, int32_t x, int32_t y,
                     uint16_t fg, uint16_t bg, const uint16_t *lut, int bg_only)
{
    int32_t sx = x + g->xo, sy = y + g->yo;
    for (int32_t row = 0; row < g->h; row++) {
        int32_t dy = sy + row;
        if (dy < 0 || dy >= DH) continue;
        for (int32_t col = 0; col < g->w; col++) {
            int32_t dx = sx + col;
            if (dx < 0 || dx >= DW) continue;
            uint8_t a = g->bmp[row * g->w + col];
            if (!a) continue;
            uint16_t *p = &dst[dy * DS + dx];
            if (bg_only) *p = lut[a];
            else *p = *p == bg ? lut[a] : glyph_blend565_255(*p, fg, a);
        }
    }
}

static void run_case(const char *name, int32_t x, int32_t y,
                     int32_t xo, int32_t yo, int bg_only)
{
    static const uint8_t bmp[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 20, 30, 40, 50, 60, 70, 80, 90,
        100, 150, 200, 255, 128, 64, 32, 16, 8,
        255, 0, 127, 63, 31, 15, 7, 3, 1,
    };
    GlyphBlit8 g = {bmp, 9, 5, xo, yo};
    uint16_t lut[256];
    uint16_t actual[DH * DS];
    uint16_t expected[DH * DS];
    uint16_t fg = 0xF800, bg = 0x0010;
    for (int i = 0; i < 256; i++) lut[i] = glyph_blend565_255(bg, fg, i);
    for (int yy = 0; yy < DH; yy++) {
        for (int xx = 0; xx < DS; xx++) {
            uint16_t v = (uint16_t)((xx * 37 + yy * 101) ^ 0x1234);
            actual[yy * DS + xx] = v;
            expected[yy * DS + xx] = v;
        }
    }
    ref_blit(expected, &g, x, y, fg, bg, lut, bg_only);
    if (bg_only) glyph_blit8_bg(actual, DS, DW, DH, x, y, &g, lut);
    else glyph_blit8(actual, DS, DW, DH, x, y, &g, fg, bg, lut);
    for (int i = 0; i < DH * DS; i++) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "%s: mismatch %d got %04X want %04X\n",
                    name, i, actual[i], expected[i]);
            assert(0);
        }
    }
}

static void exhaustive_small(void)
{
    static const int32_t xs[] = {-2, -1, 0, 1, DW - 2, DW - 1, DW};
    static const int32_t ys[] = {-1, 0, 1, DH - 1, DH};
    static const int32_t offs[] = {-1, 0, 1};
    static const uint8_t edge[] = {0, 1, 15, 127, 128, 254, 255};
    uint16_t fg = 0xF800, bg = 0x0010;
    uint16_t lut[256];
    for (int32_t i = 0; i < 256; i++) lut[i] = glyph_blend565_255(bg, fg, i);

    for (int32_t w = 1; w <= 2; w++) {
        for (int32_t h = 1; h <= 2; h++) {
            int32_t pixels = w * h;
            int32_t patterns = pixels == 1 ? 256 : (pixels == 2 ? 49 : 2401);
            for (int32_t p = 0; p < patterns; p++) {
                uint8_t bmp[4];
                if (pixels == 1) {
                    bmp[0] = (uint8_t)p;
                } else if (pixels == 2) {
                    bmp[0] = edge[p / 7];
                    bmp[1] = edge[p % 7];
                } else {
                    uint32_t v = (uint32_t)p;
                    for (int32_t i = 0; i < 4; i++) {
                        bmp[i] = edge[v % 7];
                        v /= 7;
                    }
                }
                GlyphBlit8 g = {bmp, w, h, 0, 0};
                for (size_t oi = 0; oi < sizeof(offs) / sizeof(offs[0]); oi++) {
                    g.xo = offs[oi];
                    for (size_t oj = 0; oj < sizeof(offs) / sizeof(offs[0]); oj++) {
                        g.yo = offs[oj];
                        for (size_t xi = 0; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
                            for (size_t yi = 0; yi < sizeof(ys) / sizeof(ys[0]); yi++) {
                                uint16_t actual[DH * DS];
                                uint16_t expected[DH * DS];
                                for (int32_t i = 0; i < DH * DS; i++) {
                                    uint16_t v = (uint16_t)((i * 37) ^ bg);
                                    actual[i] = v;
                                    expected[i] = v;
                                }
                                ref_blit(expected, &g, xs[xi], ys[yi], fg, bg, lut, 0);
                                glyph_blit8(actual, DS, DW, DH, xs[xi], ys[yi], &g,
                                            fg, bg, lut);
                                assert(memcmp(actual, expected, sizeof(actual)) == 0);

                                for (int32_t i = 0; i < DH * DS; i++) {
                                    actual[i] = expected[i] = (uint16_t)(i * 13);
                                }
                                ref_blit(expected, &g, xs[xi], ys[yi], fg, bg, lut, 1);
                                glyph_blit8_bg(actual, DS, DW, DH, xs[xi], ys[yi], &g, lut);
                                assert(memcmp(actual, expected, sizeof(actual)) == 0);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void run_random_case(uint32_t *state)
{
    uint8_t bmp[256];
    uint16_t lut[256];
    uint16_t actual[DH * DS];
    uint16_t expected[DH * DS];
    uint32_t r = *state;
    r = r * 1664525u + 1013904223u;
    int32_t w = 1 + (int32_t)(r % 16);
    r = r * 1664525u + 1013904223u;
    int32_t h = 1 + (int32_t)(r % 10);
    for (int32_t i = 0; i < w * h; i++) {
        r = r * 1664525u + 1013904223u;
        bmp[i] = (uint8_t)(r >> 24);
    }
    r = r * 1664525u + 1013904223u;
    int32_t xo = (int32_t)(r % 11) - 5;
    r = r * 1664525u + 1013904223u;
    int32_t yo = (int32_t)(r % 11) - 5;
    r = r * 1664525u + 1013904223u;
    int32_t x = (int32_t)(r % 80) - 30;
    r = r * 1664525u + 1013904223u;
    int32_t y = (int32_t)(r % 50) - 20;
    r = r * 1664525u + 1013904223u;
    uint16_t fg = (uint16_t)(r >> 16);
    r = r * 1664525u + 1013904223u;
    uint16_t bg = (uint16_t)(r >> 16);
    int bg_only = (int)(r & 1);
    GlyphBlit8 g = {bmp, w, h, xo, yo};

    for (int i = 0; i < 256; i++) lut[i] = glyph_blend565_255(bg, fg, i);
    for (int yy = 0; yy < DH; yy++) {
        for (int xx = 0; xx < DS; xx++) {
            uint16_t v = (uint16_t)((xx * 37 + yy * 101) ^ bg);
            actual[yy * DS + xx] = v;
            expected[yy * DS + xx] = v;
        }
    }
    ref_blit(expected, &g, x, y, fg, bg, lut, bg_only);
    if (bg_only) glyph_blit8_bg(actual, DS, DW, DH, x, y, &g, lut);
    else glyph_blit8(actual, DS, DW, DH, x, y, &g, fg, bg, lut);
    for (int i = 0; i < DH * DS; i++) assert(actual[i] == expected[i]);
    *state = r;
}

int main(void)
{
    run_case("even", 10, 4, 1, 1, 0);
    run_case("odd", 11, 4, 0, 1, 0);
    run_case("left", -5, 4, 2, 1, 0);
    run_case("right", DW - 4, 4, 1, 1, 0);
    run_case("top", 10, -2, 1, 1, 0);
    run_case("bottom", 10, DH - 2, 1, 1, 0);
    run_case("far", -20, -20, -3, -3, 0);
    run_case("bg-even", 10, 4, 1, 1, 1);
    run_case("bg-odd", 11, 4, 0, 1, 1);
    run_case("bg-clip", -5, -2, 2, 1, 1);
    run_case("bg-outside", 100, 100, 1, 1, 1);

    uint32_t state = 0x13579BDFu;
    exhaustive_small();
    for (int i = 0; i < 5000; i++) run_random_case(&state);

    uint16_t buf[DH * DS] = {0};
    uint16_t lut[256] = {0};
    uint8_t bmp[1] = {0};
    GlyphBlit8 valid = {bmp, 1, 1, 0, 0};
    GlyphBlit8 zero = {bmp, 0, 0, 0, 0};
    GlyphBlit8 no_bmp = {NULL, 1, 1, 0, 0};
    glyph_blit8(NULL, DS, DW, DH, 0, 0, &valid, 0xFFFF, 0, lut);
    glyph_blit8(buf, DS, DW, DH, 0, 0, NULL, 0xFFFF, 0, lut);
    glyph_blit8(buf, DS, DW, DH, 0, 0, &no_bmp, 0xFFFF, 0, lut);
    glyph_blit8(buf, DW - 1, DW, DH, 0, 0, &valid, 0xFFFF, 0, lut);
    glyph_blit8(buf, DS, DW, DH, 0, 0, &valid, 0xFFFF, 0, NULL);
    for (int i = 0; i < DH * DS; i++) buf[i] = 0x0010;
    glyph_blit8(buf, DS, DW, DH, 0, 0, &valid, 0xFFFF, 0x0010, lut);
    for (int i = 0; i < DH * DS; i++) buf[i] = 0x0010;
    uint8_t ink_bmp[1] = {1};
    GlyphBlit8 ink = {ink_bmp, 1, 1, 0, 0};
    glyph_blit8(buf, DS, DW, DH, 0, 0, &ink, 0xFFFF, 0x0010, lut);
    for (int i = 0; i < DH * DS; i++) buf[i] = 0;
    glyph_blit8(buf, DS, DW, DH, 0, DH, &valid, 0xFFFF, 0, lut);
    glyph_blit8_bg(NULL, DS, DW, DH, 0, 0, &valid, lut);
    glyph_blit8_bg(buf, DS, DW, DH, 0, 0, NULL, lut);
    glyph_blit8_bg(buf, DS, DW, DH, 0, 0, &no_bmp, lut);
    glyph_blit8_bg(buf, DW - 1, DW, DH, 0, 0, &valid, lut);
    glyph_blit8_bg(buf, DS, DW, DH, 0, 0, &valid, NULL);
    for (int i = 0; i < DH * DS; i++) assert(buf[i] == 0);
    glyph_blit8(buf, DS, DW, DH, 10, 4, &zero, 0xFFFF, 0, lut);
    glyph_blit8_bg(buf, DS, DW, DH, 10, 4, &zero, lut);
    for (int i = 0; i < DH * DS; i++) assert(buf[i] == 0);

    puts("glyph blit8 tests passed");
    return 0;
}
