#ifndef GLYPH_BLIT_H
#define GLYPH_BLIT_H

#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#define GLYPH_INLINE static __forceinline
#else
#define GLYPH_INLINE static inline __attribute__((always_inline))
#endif

typedef struct {
    const uint8_t *bmp;
    int32_t w;
    int32_t h;
    int32_t xo;
    int32_t yo;
} GlyphBlit;

GLYPH_INLINE uint16_t glyph_blend565(uint16_t bg, uint16_t fg, int32_t a)
{
    if (a <= 0) return bg;
    if (a >= 15) return fg;
    int32_t br = (bg >> 11) & 0x1F;
    int32_t bgc = (bg >> 5) & 0x3F;
    int32_t bb = bg & 0x1F;
    int32_t fr = (fg >> 11) & 0x1F;
    int32_t fgc = (fg >> 5) & 0x3F;
    int32_t fb = fg & 0x1F;
    int32_t dr = (fr - br) * a;
    int32_t dg = (fgc - bgc) * a;
    int32_t db = (fb - bb) * a;
    int32_t r = br + (dr >= 0 ? (dr + 7) / 15 : (dr - 7) / 15);
    int32_t g = bgc + (dg >= 0 ? (dg + 7) / 15 : (dg - 7) / 15);
    int32_t b = bb + (db >= 0 ? (db + 7) / 15 : (db - 7) / 15);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

GLYPH_INLINE void glyph_blit(uint16_t *dst, int32_t dst_stride,
                              int32_t dst_w, int32_t dst_h,
                              int32_t x, int32_t y, const GlyphBlit *g,
                              uint16_t fg, uint16_t bg, const uint16_t *lut)
{
    if (!dst || !g || !g->bmp || !lut || dst_stride < dst_w) return;

    int32_t sx = x + g->xo, sy = y + g->yo;
    int32_t src_stride = (g->w + 1) >> 1;
    int32_t y0 = sy < 0 ? 0 : sy;
    int32_t y1 = sy + g->h;
    if (y1 > dst_h) y1 = dst_h;
    int32_t x0 = sx < 0 ? 0 : sx;
    int32_t x1 = sx + g->w;
    if (x1 > dst_w) x1 = dst_w;

    for (int32_t row = y0; row < y1; row++) {
        int32_t src_row = row - sy;
        uint16_t *drow = &dst[row * dst_stride];
        const uint8_t *src = &g->bmp[src_row * src_stride];
        int32_t col = x0;
        if ((col - sx) & 1) {
            int32_t sc = col - sx;
            uint8_t packed = src[sc >> 1];
            uint8_t a = (sc & 1) ? (packed >> 4) : (packed & 0x0F);
            uint16_t b = drow[col];
            drow[col] = b == bg ? lut[a] : glyph_blend565(b, fg, a);
            col++;
        }
        for (; col + 1 < x1; col += 2) {
            int32_t sc = col - sx;
            uint8_t packed = src[sc >> 1];
            if (!packed) continue;
            uint8_t a0 = packed & 0x0F;
            uint8_t a1 = packed >> 4;
            uint16_t b0 = drow[col], b1 = drow[col + 1];
            uint16_t v0 = b0 == bg ? lut[a0] : glyph_blend565(b0, fg, a0);
            uint16_t v1 = b1 == bg ? lut[a1] : glyph_blend565(b1, fg, a1);
            uint32_t pair = (uint32_t)v0 | ((uint32_t)v1 << 16);
            memcpy(&drow[col], &pair, sizeof(pair));
        }
        if (col < x1) {
            int32_t sc = col - sx;
            uint8_t packed = src[sc >> 1];
            uint8_t a = (sc & 1) ? (packed >> 4) : (packed & 0x0F);
            uint16_t b = drow[col];
            drow[col] = b == bg ? lut[a] : glyph_blend565(b, fg, a);
        }
    }
}

#endif
