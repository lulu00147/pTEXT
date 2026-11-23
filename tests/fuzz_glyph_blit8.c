#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "glyph_blit8.h"

#define FW 32
#define FH 24
#define FS 40

static void ref_blit(uint16_t *dst, const GlyphBlit8 *g, int32_t x, int32_t y,
                     uint16_t fg, uint16_t bg, const uint16_t *lut, int bg_only)
{
    int32_t sx = x + g->xo, sy = y + g->yo;
    for (int32_t row = 0; row < g->h; row++) {
        int32_t dy = sy + row;
        if (dy < 0 || dy >= FH) continue;
        for (int32_t col = 0; col < g->w; col++) {
            int32_t dx = sx + col;
            if (dx < 0 || dx >= FW) continue;
            uint8_t a = g->bmp[row * g->w + col];
            if (!a) continue;
            uint16_t *p = &dst[dy * FS + dx];
            if (bg_only) *p = lut[a];
            else *p = (*p == bg) ? lut[a] : glyph_blend565_255(*p, fg, a);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size < 8) return 0;

    int32_t w = 1 + (data[0] % 16);
    int32_t h = 1 + (data[1] % 10);
    if ((size_t)w * (size_t)h + 8 > size) return 0;

    GlyphBlit8 g = {
        data + 8, w, h,
        (int32_t)(data[2] % 11) - 5,
        (int32_t)(data[3] % 11) - 5,
    };
    int32_t x = (int32_t)(data[4] % 64) - 20;
    int32_t y = (int32_t)(data[5] % 40) - 12;
    uint16_t fg = (uint16_t)(data[6] << 8 | data[7]);
    uint16_t bg = (uint16_t)(data[6] * 257u);
    int bg_only = data[2] & 1;

    uint16_t lut[256];
    for (int32_t i = 0; i < 256; i++) {
        lut[i] = glyph_blend565_255(bg, fg, i);
    }

    uint16_t actual[FH * FS];
    uint16_t expected[FH * FS];
    for (int32_t yy = 0; yy < FH; yy++) {
        for (int32_t xx = 0; xx < FS; xx++) {
            uint16_t v = (uint16_t)((xx * 37 + yy * 101) ^ bg);
            actual[yy * FS + xx] = v;
            expected[yy * FS + xx] = v;
        }
    }

    ref_blit(expected, &g, x, y, fg, bg, lut, bg_only);
    if (bg_only) glyph_blit8_bg(actual, FS, FW, FH, x, y, &g, lut);
    else glyph_blit8(actual, FS, FW, FH, x, y, &g, fg, bg, lut);

    if (memcmp(actual, expected, sizeof(actual)) != 0) {
        __builtin_trap();
    }
    return 0;
}
