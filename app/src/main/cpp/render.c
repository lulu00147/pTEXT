#include "render.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/font.h>
#include <android/font_matcher.h>

/* 零堆保障：强制 16 字节对齐的静态分配器，扩大至 16 MB 以支持中文字体 */
static uint8_t s_stb_pool[16 * 1024 * 1024] __attribute__((aligned(16)));
static uint32_t s_stb_pool_ptr = 0;

static void* pool_alloc(size_t size) {
    if (size == 0 || size > sizeof(s_stb_pool)) return NULL;
    /* 强制向上对齐到 16 字节 */
    uint32_t aligned_size = (uint32_t)((size + 15) & ~15);
    if (aligned_size > sizeof(s_stb_pool) - s_stb_pool_ptr) return NULL;
    void *ptr = &s_stb_pool[s_stb_pool_ptr];
    s_stb_pool_ptr += aligned_size;
    return ptr;
}

/* 消除浮点转整数的未定义行为 */
static inline int32_t safe_ifloor(float x) {
    if (x != x) return 0;
    if (x >= 2147483647.0f) return 2147483647;
    if (x <= -2147483648.0f) return -2147483648;
    return (int32_t)x;
}
static inline int32_t safe_iceil(float x) {
    return safe_ifloor(x + 0.99999f);
}

#define STBTT_STATIC
#define STBTT_assert(x)
#define STBTT_ifloor(x) safe_ifloor(x)
#define STBTT_iceil(x) safe_iceil(x)
#define STBTT_fabs(x)    ((x) < 0 ? -(x) : (x))
#define STBTT_sqrt(x)    __builtin_sqrtf(x)
#define STBTT_pow(x,y)   __builtin_powf(x,y)
#define STBTT_fmod(x,y)  __builtin_fmodf(x,y)
#define STBTT_cos(x)     __builtin_cosf(x)
#define STBTT_acos(x)    __builtin_acosf(x)
#define STBTT_malloc(x,u) pool_alloc(x)
#define STBTT_free(x,u) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define GC_SIZE  512
#define GC_ASSOC 4
#define GC_MASK  (GC_SIZE - 1)
#define GLYPH_MAX 128

typedef struct {
    uint32_t cp;
    float sz;
    uint8_t bmp[GLYPH_MAX * GLYPH_MAX / 2];  /* 4-bit alpha */
    uint8_t w, h;
    int16_t xo, yo;
    int32_t adv;
    uint32_t lru;
} G;

static ANativeWindow *s_win;
static ANativeWindow_Buffer s_buf;
static uint16_t *s_px;
static int32_t s_w, s_h, s_stride, s_locked;
static stbtt_fontinfo s_fi;
static const void *s_font_map = NULL;
static size_t s_font_len = 0;
static float s_fsz = 36.0f, s_sc;
static float s_ui_scale = 1.0f;
static G s_gc[GC_SIZE];
static uint32_t s_lru;
static int32_t s_font_ok = 0;
static uint8_t s_glyph_tmp[GLYPH_MAX * GLYPH_MAX];  /* 字形光栅化复用缓冲区 */

/* 主题色：0=暗色(黑底白字), 1=亮色(白底黑字) */
static int32_t s_theme = 0;
static uint16_t s_fg_color = 0xFFFF;
static uint16_t s_bg_color = 0x0000;
static uint8_t  s_bg_byte  = 0x00;   /* memset 用的字节值 */
static uint16_t s_blend_lut[16];
static uint16_t s_lut_fg;
static uint16_t s_lut_bg;
static int32_t s_lut_valid;

static int32_t get_glyph_advance_fast(uint32_t cp)
{
    if (!s_font_ok || !cp) return 0;
    int32_t adv, lsb;
    stbtt_GetCodepointHMetrics(&s_fi, cp, &adv, &lsb);
    return (int32_t)(adv * s_sc);
}

static G *gc_get(uint32_t cp)
{
    if (!s_font_ok || !cp) return NULL;
    uint32_t h = (cp * 2654435761u) & GC_MASK;
    G *old = NULL;
    for (int32_t i = 0; i < GC_ASSOC; i++) {
        G *g = &s_gc[(h + i) & GC_MASK];
        if (g->cp == cp && g->sz == s_fsz && g->w) {
            g->lru = ++s_lru;
            return g;
        }
        if (!old || g->lru < old->lru) old = g;
    }
    G *g = old;
    g->cp = cp;
    g->sz = s_fsz;
    g->lru = ++s_lru;

    /* stb 的字形分配在单次光栅化内完成；本次结束回滚水位，避免单调池耗尽。 */
    uint32_t pool_mark = s_stb_pool_ptr;

    int32_t ix0, iy0, ix1, iy1;
    stbtt_GetCodepointBitmapBox(&s_fi, cp, s_sc, s_sc,
                                &ix0, &iy0, &ix1, &iy1);

    int32_t gw = ix1 - ix0;
    int32_t gh = iy1 - iy0;
    g->w = 0;
    g->h = 0;
    g->xo = 0;
    g->yo = 0;

    if (gw > 0 && gh > 0 && gw <= GLYPH_MAX && gh <= GLYPH_MAX) {
        g->w = (uint8_t)gw;
        g->h = (uint8_t)gh;
        g->xo = (int16_t)ix0;
        g->yo = (int16_t)iy0;

        stbtt_MakeCodepointBitmap(&s_fi, s_glyph_tmp, g->w, g->h,
                                  g->w, s_sc, s_sc, cp);
        memset(g->bmp, 0, sizeof(g->bmp));
        int32_t stride = (g->w + 1) >> 1;
        for (int32_t y = 0; y < g->h; y++) {
            for (int32_t x = 0; x < g->w; x++) {
                uint8_t a = (uint8_t)(s_glyph_tmp[y * g->w + x] >> 4);
                if (!a) continue;
                uint8_t *cell = &g->bmp[y * stride + (x >> 1)];
                if (x & 1) *cell = (uint8_t)((*cell & 0x0F) | (a << 4));
                else       *cell = (uint8_t)((*cell & 0xF0) | a);
            }
        }
    }

    g->adv = get_glyph_advance_fast(cp);
    s_stb_pool_ptr = pool_mark;
    return g;
}

static void unmap_font(void)
{
    if (s_font_map) {
        munmap((void *)s_font_map, s_font_len);
        s_font_map = NULL;
    }
    s_font_len = 0;
    s_font_ok = 0;
}

static int32_t load_font_file(const char *path, size_t collection,
                              int32_t require_cjk,
                              const unsigned char **out_map, size_t *out_len,
                              stbtt_fontinfo *out_fi)
{
    if (!path || collection > (size_t)INT_MAX) return 0;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;

    off_t len = lseek(fd, 0, SEEK_END);
    if (len <= 0) {
        close(fd);
        return 0;
    }

    void *map = mmap(NULL, (size_t)len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return 0;

    int offset = stbtt_GetFontOffsetForIndex((const unsigned char *)map,
                                             (int)collection);
    if (offset < 0) {
        munmap(map, (size_t)len);
        return 0;
    }

    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, (const unsigned char *)map, offset)) {
        munmap(map, (size_t)len);
        return 0;
    }

    if (require_cjk && stbtt_FindGlyphIndex(&fi, 0x4E00) == 0) {
        munmap(map, (size_t)len);
        return 0;
    }

    *out_map = (const unsigned char *)map;
    *out_len = (size_t)len;
    *out_fi = fi;
    return 1;
}

static int32_t try_font_match(const char *locales, int32_t require_cjk)
{
    AFontMatcher *matcher = AFontMatcher_create();
    if (!matcher) return 0;

    AFontMatcher_setStyle(matcher, AFONT_WEIGHT_NORMAL, false);
    if (locales) AFontMatcher_setLocales(matcher, locales);

    const uint16_t text[] = {0x4E00};
    AFont *font = AFontMatcher_match(matcher, "sans-serif", text, 1, NULL);

    int32_t ok = 0;
    if (font) {
        const unsigned char *map = NULL;
        size_t len = 0;
        stbtt_fontinfo fi;
        if (load_font_file(AFont_getFontFilePath(font),
                           AFont_getCollectionIndex(font),
                           require_cjk, &map, &len, &fi)) {
            s_font_map = map;
            s_font_len = len;
            s_fi = fi;
            s_font_ok = 1;
            ok = 1;
        }
        AFont_close(font);
    }

    AFontMatcher_destroy(matcher);
    return ok;
}

static int32_t load_system_font(void)
{
    if (try_font_match("zh-CN,zh-TW,ja-JP", 1)) return 1;
    if (try_font_match("ja-JP,zh-CN", 1)) return 1;
    if (try_font_match("en-US", 0)) return 1;
    return 0;
}

void r_set_ui_scale(float scale)
{
    if (scale < 0.75f) scale = 0.75f;
    if (scale > 4.0f) scale = 4.0f;
    s_ui_scale = scale;
}

float r_ui_scale(void)
{
    return s_ui_scale;
}

void r_init(ANativeWindow *w)
{
    if (!w) return;

    s_win = w;
    s_w = ANativeWindow_getWidth(w);
    s_h = ANativeWindow_getHeight(w);
    if (s_w <= 0 || s_h <= 0) {
        s_w = 720;
        s_h = 1280;
    }
    ANativeWindow_setBuffersGeometry(w, 0, 0, WINDOW_FORMAT_RGB_565);

    unmap_font();
    r_clear_cache();
    load_system_font();
    r_set_font_size(s_fsz);
}

void r_deinit(void)
{
    if (s_locked) {
        ANativeWindow_unlockAndPost(s_win);
        s_locked = 0;
    }
    unmap_font();
    s_win = NULL;
    s_px = NULL;
}

__attribute__((weak)) extern void ANativeWindow_setDirtyRect(ANativeWindow *window, const ARect *rect);

void r_set_dirty(const ARect *rect) {
    if (s_win && rect && ANativeWindow_setDirtyRect) {
        ANativeWindow_setDirtyRect(s_win, rect);
    }
}

int32_t r_begin(void)
{
    if (!s_win) return 0;
    if (ANativeWindow_lock(s_win, &s_buf, NULL) != 0) return 0;

    if (!s_buf.bits ||
        s_buf.width <= 0 || s_buf.height <= 0 ||
        s_buf.stride < s_buf.width) {
        ANativeWindow_unlockAndPost(s_win);
        return 0;
    }

    s_px = (uint16_t *)s_buf.bits;
    s_stride = s_buf.stride;
    s_w = s_buf.width;
    s_h = s_buf.height;
    s_locked = 1;
    return 1;
}

void r_end(void)
{
    if (s_locked && s_win) {
        ANativeWindow_unlockAndPost(s_win);
        s_locked = 0;
        s_px = NULL;
    }
}

void r_clear(void)
{
    if (s_locked) memset(s_px, s_bg_byte, s_stride * s_h * 2);
}

void r_clear_rect(int32_t l, int32_t t, int32_t r, int32_t b)
{
    if (!s_locked) return;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > s_w) r = s_w;
    if (b > s_h) b = s_h;
    if (r <= l || b <= t) return;

    for (int32_t y = t; y < b; y++) {
        memset(&s_px[y * s_stride + l], s_bg_byte, (size_t)(r - l) * 2);
    }
}

void r_stroke_rect(int32_t l, int32_t t, int32_t r, int32_t b, int32_t width)
{
    if (!s_locked || !s_px || width <= 0) return;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > s_w) r = s_w;
    if (b > s_h) b = s_h;
    if (r <= l || b <= t) return;

    int32_t max_w = (r - l < b - t ? r - l : b - t) / 2;
    if (width > max_w) width = max_w;
    if (width <= 0) return;
    uint16_t color = s_theme ? 0x0000 : 0xFFFF;

    for (int32_t y = t; y < t + width; y++) {
        for (int32_t x = l; x < r; x++) s_px[y * s_stride + x] = color;
    }
    for (int32_t y = b - width; y < b; y++) {
        for (int32_t x = l; x < r; x++) s_px[y * s_stride + x] = color;
    }
    for (int32_t y = t + width; y < b - width; y++) {
        for (int32_t x = l; x < l + width; x++) s_px[y * s_stride + x] = color;
        for (int32_t x = r - width; x < r; x++) s_px[y * s_stride + x] = color;
    }
}

static inline uint16_t blend565(uint16_t bg, uint16_t fg, int32_t a)
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

static void ensure_blend_lut(uint16_t fg)
{
    if (s_lut_valid && s_lut_fg == fg && s_lut_bg == s_bg_color) return;
    s_lut_fg = fg;
    s_lut_bg = s_bg_color;
    for (int32_t a = 0; a < 16; a++) s_blend_lut[a] = blend565(s_bg_color, fg, a);
    s_lut_valid = 1;
}

static inline void draw_glyph(int32_t x, int32_t y, const G *g, uint16_t fg)
{
    int32_t sx = x + g->xo, sy = y + g->yo;
    int32_t stride = (g->w + 1) >> 1;
    int32_t y0 = sy < 0 ? 0 : sy;
    int32_t y1 = sy + g->h;
    if (y1 > s_h) y1 = s_h;
    int32_t x0 = sx < 0 ? 0 : sx;
    int32_t x1 = sx + g->w;
    if (x1 > s_w) x1 = s_w;
    for (int32_t row = y0; row < y1; row++) {
        int32_t src_row = row - sy;
        uint16_t *dst = &s_px[row * s_stride];
        const uint8_t *src = &g->bmp[src_row * stride];
        for (int32_t col = x0; col < x1; col++) {
            int32_t src_col = col - sx;
            uint8_t packed = src[src_col >> 1];
            if (!packed) {
                if (!(src_col & 1) && col + 1 < x1) col++;
                continue;
            }
            int32_t a = (src_col & 1) ? (packed >> 4) : (packed & 0x0F);
            if (a == 15) {
                dst[col] = fg;
            } else if (a) {
                uint16_t bg = dst[col];
                dst[col] = bg == s_bg_color ? s_blend_lut[a] : blend565(bg, fg, a);
            }
        }
    }
}

void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n)
{
    if (!s_locked || !s_font_ok) return;
    uint16_t fg = s_fg_color;
    ensure_blend_lut(fg);
    for (int32_t i = 0; i < n; i++) {
        G *g = gc_get(s[i]);
        if (!g || !g->w) {
            x += (g && g->adv) ? g->adv : r_font_h();
            continue;
        }
        draw_glyph(x, y, g, fg);
        x += g->adv;
    }
}

static int justify_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == 0x3000;
}

static int justify_punct(uint32_t cp)
{
    if (cp < 0x80) return (cp == ',' || cp == '.' || cp == ';' || cp == ':' ||
                           cp == '!' || cp == '?' || cp == ')');
    return cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF1A ||
           cp == 0xFF1B || cp == 0xFF01 || cp == 0xFF1F ||
           cp == 0x300A || cp == 0x300B || cp == 0x201C || cp == 0x201D ||
           cp == 0x2018 || cp == 0x2019 || cp == 0x2014 || cp == 0x2026;
}

void r_draw_text_justified(int32_t x, int32_t y, const uint32_t *s, int32_t n, int32_t target_width)
{
    if (!s_locked || !s_font_ok) return;
    if (n < 2) {
        r_draw_text(x, y, s, n);
        return;
    }
    if (n > 256) {
        r_draw_text(x, y, s, n);
        return;
    }
    int32_t natural = r_text_width(s, n);
    int32_t diff = target_width - natural;
    if (diff == 0) {
        r_draw_text(x, y, s, n);
        return;
    }

    int32_t gaps[256];
    int32_t total_w = 0;
    for (int32_t i = 0; i < n - 1; i++) {
        int32_t w = 1;
        if (diff < 0) {
            int comp = justify_punct(s[i]) || justify_punct(s[i + 1]) ||
                       justify_space(s[i]) || justify_space(s[i + 1]);
            w = comp ? 3 : 1;
        }
        gaps[i] = w;
        total_w += w;
    }
    if (total_w < 1) {
        r_draw_text(x, y, s, n);
        return;
    }

    int32_t q = diff / total_w;
    int32_t rem = diff - q * total_w;
    for (int32_t i = 0; i < n - 1; i++) {
        int32_t w = gaps[i];
        int32_t g = q * w;
        if (rem > 0) {
            int32_t add = rem < w ? rem : w;
            g += add;
            rem -= add;
        } else if (rem < 0) {
            int32_t sub = (-rem) < w ? (-rem) : w;
            g -= sub;
            rem += sub;
        }
        gaps[i] = g;
    }

    int32_t px = x;
    uint16_t fg = s_fg_color;
    ensure_blend_lut(fg);
    for (int32_t i = 0; i < n; i++) {
        G *g = gc_get(s[i]);
        if (!g || !g->w) {
            px += (g && g->adv) ? g->adv : r_font_h();
        } else {
            draw_glyph(px, y, g, fg);
            px += g->adv;
        }
        if (i < n - 1) px += gaps[i];
    }
}

int32_t r_text_width(const uint32_t *s, int32_t n)
{
    if (!s_font_ok) return 0;
    int32_t w = 0;
    for (int32_t i = 0; i < n; i++) w += r_char_advance(s[i]);
    return w;
}

int32_t r_text_width_at_size(const uint32_t *s, int32_t n, float size)
{
    if (!s_font_ok) return 0;
    if (size < 1.0f) size = 1.0f;
    if (size > 128.0f) size = 128.0f;
    float old_sz = s_fsz, old_sc = s_sc;
    s_fsz = size;
    s_sc = stbtt_ScaleForPixelHeight(&s_fi, size);
    int32_t w = r_text_width(s, n);
    s_fsz = old_sz;
    s_sc = old_sc;
    return w;
}

void r_draw_text_at_size(int32_t x, int32_t y, const uint32_t *s, int32_t n, float size)
{
    if (size < 1.0f) size = 1.0f;
    if (size > 128.0f) size = 128.0f;
    float old_sz = s_fsz, old_sc = s_sc;
    s_fsz = size;
    if (s_font_ok) s_sc = stbtt_ScaleForPixelHeight(&s_fi, size);
    r_draw_text(x, y, s, n);
    s_fsz = old_sz;
    s_sc = old_sc;
}

void r_draw_text_at_size_color(int32_t x, int32_t y, const uint32_t *s, int32_t n,
                               float size, uint16_t color)
{
    uint16_t old_fg = s_fg_color;
    s_fg_color = color;
    r_draw_text_at_size(x, y, s, n, size);
    s_fg_color = old_fg;
}

int32_t r_char_advance(uint32_t cp)
{
    if (!s_font_ok || !cp) return 0;
    uint32_t h = (cp * 2654435761u) & GC_MASK;
    for (int32_t i = 0; i < GC_ASSOC; i++) {
        G *g = &s_gc[(h + i) & GC_MASK];
        if (g->cp == cp && g->sz == s_fsz) return g->adv;
    }
    return get_glyph_advance_fast(cp);
}

int32_t r_font_h(void)
{
    int32_t h = (int32_t)(s_fsz * 1.2f);
    return h > 0 ? h : 1;
}

void r_set_font_size(float s)
{
    if (s < 1.0f) s = 1.0f;
    if (s > 128.0f) s = 128.0f;
    s_fsz = s;
    if (s_font_ok) s_sc = stbtt_ScaleForPixelHeight(&s_fi, s_fsz);
    r_clear_cache();
}

void r_clear_cache(void) {
    memset(s_gc, 0, sizeof(s_gc));
    s_lru = 0;
    s_stb_pool_ptr = 0;
}

int32_t r_w(void) { return s_w; }
int32_t r_h(void) { return s_h; }

void r_set_theme(int32_t theme)
{
    s_theme = theme ? 1 : 0;
    if (s_theme) {
        s_fg_color = 0x0000;   /* 黑字 */
        s_bg_color = 0xFFFF;
        s_bg_byte  = 0xFF;
    } else {
        s_fg_color = 0xAD55;   /* #A9A9A9，对齐 Legado 夜间正文 */
        s_bg_color = 0x0000;
        s_bg_byte  = 0x00;
    }
    s_lut_valid = 0;
}

void r_update_window(ANativeWindow *w)
{
    if (!w) return;
    s_win = w;
    int32_t nw = ANativeWindow_getWidth(w);
    int32_t nh = ANativeWindow_getHeight(w);
    if (nw > 0 && nh > 0) { s_w = nw; s_h = nh; }
    ANativeWindow_setBuffersGeometry(w, 0, 0, WINDOW_FORMAT_RGB_565);
}