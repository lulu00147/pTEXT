#ifndef GLYPH_BLIT_H
#define GLYPH_BLIT_H

#include <stdint.h>
#include <string.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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

#if defined(__aarch64__)
GLYPH_INLINE void glyph_neon4(uint16_t *drow, const uint8_t *src, int32_t sc,
                               const uint8x16x2_t *table,
                               uint16_t fg, uint16_t bg, const uint16_t *lut)
{
    uint8_t p0 = src[sc >> 1];
    uint8_t p1 = src[(sc + 2) >> 1];
    if (!(p0 | p1)) return;

    uint16_t packed = (uint16_t)p0 | ((uint16_t)p1 << 8);
    uint8x8_t pb = vreinterpret_u8_u16(vdup_n_u16(packed));
    uint8x8_t lo = vand_u8(pb, vdup_n_u8(0x0F));
    uint8x8_t hi = vshr_n_u8(pb, 4);
    uint8x8_t alpha = vzip_u8(lo, hi).val[0];
    uint8x8_t q = vshl_n_u8(alpha, 1);
    uint8x8_t idx8 = vzip_u8(q, vadd_u8(q, vdup_n_u8(1))).val[0];
    uint8x16_t idx = vcombine_u8(idx8, vdup_n_u8(0));
    uint8x8_t res = vget_low_u8(vqtbl2q_u8(*table, idx));
    uint16x4_t v = vreinterpret_u16_u8(res);
    uint16x4_t d = vld1_u16(drow);
    vst1_u16(drow, v);

    uint16x4_t mask = vceq_u16(d, vdup_n_u16(bg));
    uint64_t mb = vget_lane_u64(vreinterpret_u64_u16(mask), 0);
    if (mb == UINT64_MAX) return;

    uint16_t orig[4];
    uint8_t al[8];
    vst1_u16(orig, d);
    vst1_u8(al, alpha);
    for (int32_t i = 0; i < 4; i++) {
        if ((uint16_t)(mb >> (i * 16)) != 0xFFFFu) {
            drow[i] = orig[i] == bg ? lut[al[i]] : glyph_blend565(orig[i], fg, al[i]);
        }
    }
}
#endif

GLYPH_INLINE uint8_t glyph_nibble(uint8_t packed, int32_t sc)
{
    return (uint8_t)((packed >> ((sc & 1) << 2)) & 0x0F);
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
    if (x1 <= x0 || y1 <= y0) return;

#if defined(__aarch64__)
    uint8x16x2_t table;
    table.val[0] = vld1q_u8((const uint8_t *)lut);
    table.val[1] = vld1q_u8((const uint8_t *)lut + 16);
#endif

    for (int32_t row = y0; row < y1; row++) {
        int32_t src_row = row - sy;
        uint16_t *drow = &dst[row * dst_stride];
        const uint8_t *src = &g->bmp[src_row * src_stride];
        int32_t col = x0;
        if ((col - sx) & 1) {
            int32_t sc = col - sx;
            uint8_t packed = src[sc >> 1];
            uint8_t a = glyph_nibble(packed, sc);
            uint16_t b = drow[col];
            drow[col] = b == bg ? lut[a] : glyph_blend565(b, fg, a);
            col++;
        }
#if defined(__aarch64__)
        for (; col + 7 < x1; col += 8) {
            int32_t sc = col - sx;
            if (sc + 7 >= g->w) break;
            if (!(src[sc >> 1] | src[(sc + 2) >> 1] |
                  src[(sc + 4) >> 1] | src[(sc + 6) >> 1])) continue;
            glyph_neon4(&drow[col], src, sc, &table, fg, bg, lut);
            glyph_neon4(&drow[col + 4], src, sc + 4, &table, fg, bg, lut);
        }
        for (; col + 3 < x1; col += 4) {
            int32_t sc = col - sx;
            if (sc + 3 >= g->w) break;
            glyph_neon4(&drow[col], src, sc, &table, fg, bg, lut);
        }
#endif
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
            uint8_t a = glyph_nibble(packed, sc);
            uint16_t b = drow[col];
            drow[col] = b == bg ? lut[a] : glyph_blend565(b, fg, a);
        }
    }
}

GLYPH_INLINE void glyph_blit_bg(uint16_t *dst, int32_t dst_stride,
                                 int32_t dst_w, int32_t dst_h,
                                 int32_t x, int32_t y, const GlyphBlit *g,
                                 const uint16_t *lut)
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
    if (x1 <= x0 || y1 <= y0) return;

#if defined(__aarch64__)
    uint8x16x2_t table;
    table.val[0] = vld1q_u8((const uint8_t *)lut);
    table.val[1] = vld1q_u8((const uint8_t *)lut + 16);
#endif

    for (int32_t row = y0; row < y1; row++) {
        int32_t src_row = row - sy;
        uint16_t *drow = &dst[row * dst_stride];
        const uint8_t *src = &g->bmp[src_row * src_stride];
        int32_t col = x0;
        if ((col - sx) & 1) {
            int32_t sc = col - sx;
            uint8_t a = (src[sc >> 1] >> 4) & 0x0F;
            if (a) drow[col] = lut[a];
            col++;
        }
#if defined(__aarch64__)
        for (; col + 7 < x1; col += 8) {
            int32_t sc = col - sx;
            if (sc + 7 >= g->w) break;
            uint8_t p0 = src[sc >> 1];
            uint8_t p1 = src[(sc + 2) >> 1];
            uint8_t p2 = src[(sc + 4) >> 1];
            uint8_t p3 = src[(sc + 6) >> 1];
            if (!(p0 | p1 | p2 | p3)) continue;

            uint16_t packed = (uint16_t)p0 | ((uint16_t)p1 << 8);
            uint8x8_t pb = vreinterpret_u8_u16(vdup_n_u16(packed));
            uint8x8_t lo = vand_u8(pb, vdup_n_u8(0x0F));
            uint8x8_t hi = vshr_n_u8(pb, 4);
            uint8x8_t alpha = vzip_u8(lo, hi).val[0];
            uint8x8_t q = vshl_n_u8(alpha, 1);
            uint8x8_t idx8 = vzip_u8(q, vadd_u8(q, vdup_n_u8(1))).val[0];
            uint8x16_t idx = vcombine_u8(idx8, vdup_n_u8(0));
            uint8x8_t res = vget_low_u8(vqtbl2q_u8(table, idx));
            vst1_u16(&drow[col], vreinterpret_u16_u8(res));

            packed = (uint16_t)p2 | ((uint16_t)p3 << 8);
            pb = vreinterpret_u8_u16(vdup_n_u16(packed));
            lo = vand_u8(pb, vdup_n_u8(0x0F));
            hi = vshr_n_u8(pb, 4);
            alpha = vzip_u8(lo, hi).val[0];
            q = vshl_n_u8(alpha, 1);
            idx8 = vzip_u8(q, vadd_u8(q, vdup_n_u8(1))).val[0];
            idx = vcombine_u8(idx8, vdup_n_u8(0));
            res = vget_low_u8(vqtbl2q_u8(table, idx));
            vst1_u16(&drow[col + 4], vreinterpret_u16_u8(res));
        }
        for (; col + 3 < x1; col += 4) {
            int32_t sc = col - sx;
            if (sc + 3 >= g->w) break;
            uint8_t p0 = src[sc >> 1];
            uint8_t p1 = src[(sc + 2) >> 1];
            if (!(p0 | p1)) continue;
            uint16_t packed = (uint16_t)p0 | ((uint16_t)p1 << 8);
            uint8x8_t pb = vreinterpret_u8_u16(vdup_n_u16(packed));
            uint8x8_t lo = vand_u8(pb, vdup_n_u8(0x0F));
            uint8x8_t hi = vshr_n_u8(pb, 4);
            uint8x8_t alpha = vzip_u8(lo, hi).val[0];
            uint8x8_t q = vshl_n_u8(alpha, 1);
            uint8x8_t idx8 = vzip_u8(q, vadd_u8(q, vdup_n_u8(1))).val[0];
            uint8x16_t idx = vcombine_u8(idx8, vdup_n_u8(0));
            uint8x8_t res = vget_low_u8(vqtbl2q_u8(table, idx));
            vst1_u16(&drow[col], vreinterpret_u16_u8(res));
        }
#endif
        for (; col + 1 < x1; col += 2) {
            int32_t sc = col - sx;
            uint8_t packed = src[sc >> 1];
            if (!packed) continue;
            uint8_t a0 = packed & 0x0F;
            uint8_t a1 = packed >> 4;
            if (a0) drow[col] = lut[a0];
            if (a1) drow[col + 1] = lut[a1];
        }
        if (col < x1) {
            int32_t sc = col - sx;
            uint8_t a = glyph_nibble((uint8_t)(src[sc >> 1]), sc);
            if (a) drow[col] = lut[a];
        }
    }
}

#endif
