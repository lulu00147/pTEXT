#ifndef GLYPH_BLIT8_H
#define GLYPH_BLIT8_H

#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *bmp;
    int32_t w, h;
    int32_t xo, yo;
} GlyphBlit8;

static inline uint16_t glyph_blend565_255(uint16_t bg, uint16_t fg, int32_t a)
{
    if (a <= 0) return bg;
    if (a >= 255) return fg;
    int32_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    int32_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int32_t dr = (fr - br) * a, dg = (fgc - bgc) * a, db = (fb - bb) * a;
    int32_t r = br + (dr >= 0 ? (dr + 127) / 255 : (dr - 127) / 255);
    int32_t g = bgc + (dg >= 0 ? (dg + 127) / 255 : (dg - 127) / 255);
    int32_t b = bb + (db >= 0 ? (db + 127) / 255 : (db - 127) / 255);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline void glyph_blit8(uint16_t *dst, int32_t dst_stride,
                               int32_t dst_w, int32_t dst_h,
                               int32_t x, int32_t y, const GlyphBlit8 *g,
                               uint16_t fg, uint16_t bg, const uint16_t *lut)
{
    if (!dst || !g || !g->bmp || !lut || dst_stride < dst_w) return;
    int32_t sx = x + g->xo, sy = y + g->yo;
    int32_t y0 = sy < 0 ? 0 : sy;
    int32_t y1 = sy + g->h;
    if (y1 > dst_h) y1 = dst_h;
    int32_t x0 = sx < 0 ? 0 : sx;
    int32_t x1 = sx + g->w;
    if (x1 > dst_w) x1 = dst_w;
    if (x1 <= x0 || y1 <= y0) return;

    for (int32_t row = y0; row < y1; row++) {
        uint16_t *drow = &dst[row * dst_stride];
        const uint8_t *srow = &g->bmp[(row - sy) * g->w];
        for (int32_t col = x0; col < x1; col++) {
            uint8_t a = srow[col - sx];
            if (!a) continue;
            uint16_t b = drow[col];
            drow[col] = b == bg ? lut[a] : glyph_blend565_255(b, fg, a);
        }
    }
}

static inline void glyph_blit8_bg(uint16_t *dst, int32_t dst_stride,
                                  int32_t dst_w, int32_t dst_h,
                                  int32_t x, int32_t y, const GlyphBlit8 *g,
                                  const uint16_t *lut)
{
    if (!dst || !g || !g->bmp || !lut || dst_stride < dst_w) return;
    int32_t sx = x + g->xo, sy = y + g->yo;
    int32_t y0 = sy < 0 ? 0 : sy;
    int32_t y1 = sy + g->h;
    if (y1 > dst_h) y1 = dst_h;
    int32_t x0 = sx < 0 ? 0 : sx;
    int32_t x1 = sx + g->w;
    if (x1 > dst_w) x1 = dst_w;
    if (x1 <= x0 || y1 <= y0) return;

    for (int32_t row = y0; row < y1; row++) {
        uint16_t *drow = &dst[row * dst_stride];
        const uint8_t *srow = &g->bmp[(row - sy) * g->w];
        for (int32_t col = x0; col < x1; col++) {
            uint8_t a = srow[col - sx];
            if (a) drow[col] = lut[a];
        }
    }
}

#endif
