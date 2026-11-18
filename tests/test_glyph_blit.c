#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "glyph_blit.h"

#define DW 40
#define DH 16
#define DS 44

static uint16_t ref_blend(uint16_t bg, uint16_t fg, int32_t a)
{
    if (a <= 0) return bg;
    if (a >= 15) return fg;
    int32_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    int32_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int32_t dr = (fr - br) * a, dg = (fgc - bgc) * a, db = (fb - bb) * a;
    int32_t r = br + (dr >= 0 ? (dr + 7) / 15 : (dr - 7) / 15);
    int32_t g = bgc + (dg >= 0 ? (dg + 7) / 15 : (dg - 7) / 15);
    int32_t b = bb + (db >= 0 ? (db + 7) / 15 : (db - 7) / 15);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void reference_blit(uint16_t *dst, const GlyphBlit *g, int32_t x, int32_t y,
                           uint16_t fg, uint16_t bg, const uint16_t *lut)
{
    int32_t sx = x + g->xo, sy = y + g->yo;
    int32_t stride = (g->w + 1) >> 1;
    for (int32_t row = 0; row < g->h; row++) {
        int32_t dy = sy + row;
        if (dy < 0 || dy >= DH) continue;
        for (int32_t col = 0; col < g->w; col++) {
            int32_t dx = sx + col;
            if (dx < 0 || dx >= DW) continue;
            uint8_t packed = g->bmp[row * stride + (col >> 1)];
            uint8_t a = (col & 1) ? (packed >> 4) : (packed & 0x0F);
            if (!a) continue;
            uint16_t *p = &dst[dy * DS + dx];
            *p = *p == bg ? lut[a] : ref_blend(*p, fg, a);
        }
    }
}

static void run_case(const char *name, int32_t x, int32_t y,
                     uint16_t fg, uint16_t bg, int32_t xo, int32_t yo)
{
    static const uint8_t bmp[] = {
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x00, 0x34, 0x00, 0x00,
        0xF0, 0x12, 0x00, 0x00, 0x00,
        0x5A, 0xA5, 0x0F, 0xF0, 0x00,
        0xFF, 0x00, 0x66, 0x00, 0x00,
    };
    GlyphBlit g = {bmp, 9, 5, xo, yo};
    uint16_t lut[16];
    uint16_t actual[DH * DS];
    uint16_t expected[DH * DS];

    for (int32_t i = 0; i < 16; i++) lut[i] = ref_blend(bg, fg, i);
    for (int32_t yy = 0; yy < DH; yy++) {
        for (int32_t xx = 0; xx < DS; xx++) {
            uint16_t v = ((xx + yy) & 1) ? 0x1234 : bg;
            actual[yy * DS + xx] = v;
            expected[yy * DS + xx] = v;
        }
    }

    reference_blit(expected, &g, x, y, fg, bg, lut);
    glyph_blit(actual, DS, DW, DH, x, y, &g, fg, bg, lut);
    for (int32_t i = 0; i < DH * DS; i++) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "%s: mismatch at pixel %d: got 0x%04X expected 0x%04X\n",
                    name, i, actual[i], expected[i]);
            assert(0);
        }
    }
}

static void run_random_case(uint32_t *state)
{
    uint8_t bmp[128];
    uint16_t lut[16];
    uint16_t actual[DH * DS];
    uint16_t expected[DH * DS];
    uint32_t r = *state;
    r = r * 1664525u + 1013904223u;
    int32_t w = 1 + (int32_t)(r % 14);
    r = r * 1664525u + 1013904223u;
    int32_t h = 1 + (int32_t)(r % 10);
    int32_t stride = (w + 1) >> 1;
    for (int32_t i = 0; i < stride * h; i++) {
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
    GlyphBlit g = {bmp, w, h, xo, yo};

    for (int32_t i = 0; i < 16; i++) lut[i] = ref_blend(bg, fg, i);
    for (int32_t yy = 0; yy < DH; yy++) {
        for (int32_t xx = 0; xx < DS; xx++) {
            uint16_t v = (uint16_t)((xx * 37 + yy * 101) ^ (uint16_t)bg);
            actual[yy * DS + xx] = v;
            expected[yy * DS + xx] = v;
        }
    }

    reference_blit(expected, &g, x, y, fg, bg, lut);
    glyph_blit(actual, DS, DW, DH, x, y, &g, fg, bg, lut);
    for (int32_t i = 0; i < DH * DS; i++) {
        assert(actual[i] == expected[i]);
    }
    *state = r;
}

int main(void)
{
    static const uint16_t bgs[] = {0x0000, 0xFFFF};
    static const uint16_t fgs[] = {0xFFFF, 0x0000, 0xF800, 0x07E0, 0x001F};

    for (size_t bi = 0; bi < sizeof(bgs) / sizeof(bgs[0]); bi++) {
        for (size_t fi = 0; fi < sizeof(fgs) / sizeof(fgs[0]); fi++) {
            run_case("even", 10, 4, fgs[fi], bgs[bi], 1, 1);
            run_case("odd", 11, 4, fgs[fi], bgs[bi], 0, 1);
            run_case("left-clip", -5, 4, fgs[fi], bgs[bi], 2, 1);
            run_case("right-clip", DW - 4, 4, fgs[fi], bgs[bi], 1, 1);
            run_case("top-clip", 10, -2, fgs[fi], bgs[bi], 1, 1);
            run_case("bottom-clip", 10, DH - 2, fgs[fi], bgs[bi], 1, 1);
            run_case("outside", 10, DH + 8, fgs[fi], bgs[bi], 1, 1);
            run_case("far-left", -20, 4, fgs[fi], bgs[bi], -3, 1);
            run_case("far-right", DW + 20, 4, fgs[fi], bgs[bi], 1, 1);
            run_case("far-top", 10, -20, fgs[fi], bgs[bi], 1, -3);
            run_case("far-bottom", 10, DH + 20, fgs[fi], bgs[bi], 1, 1);
        }
    }

    uint32_t random_state = 0x12345678u;
    for (int32_t i = 0; i < 5000; i++) run_random_case(&random_state);

    uint16_t buf[DH * DS] = {0};
    uint16_t lut[16] = {0};
    uint8_t bmp[1] = {0};
    GlyphBlit valid = {bmp, 1, 1, 0, 0};
    GlyphBlit no_bmp = {NULL, 1, 1, 0, 0};
    glyph_blit(NULL, DS, DW, DH, 0, 0, &valid, 0xFFFF, 0, lut);
    glyph_blit(buf, DS, DW, DH, 0, 0, NULL, 0xFFFF, 0, lut);
    glyph_blit(buf, DS, DW, DH, 0, 0, &no_bmp, 0xFFFF, 0, lut);
    glyph_blit(buf, DS, DW, DH, 0, 0, &valid, 0xFFFF, 0, NULL);
    glyph_blit(buf, DW - 1, DW, DH, 0, 0, &valid, 0xFFFF, 0, lut);

    uint16_t untouched[DH * DS];
    for (int32_t i = 0; i < DH * DS; i++) untouched[i] = 0xA5A5;
    GlyphBlit zero_w = {bmp, 0, 1, 2, 2};
    GlyphBlit zero_h = {bmp, 1, 0, 2, 2};
    uint16_t zero_lut[16] = {0};
    glyph_blit(untouched, DS, DW, DH, 10, 4, &zero_w, 0xFFFF, 0, zero_lut);
    glyph_blit(untouched, DS, DW, DH, 10, 4, &zero_h, 0xFFFF, 0, zero_lut);
    for (int32_t i = 0; i < DH * DS; i++) assert(untouched[i] == 0xA5A5);

    puts("glyph blit tests passed");
    return 0;
}
