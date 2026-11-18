#include "ui_util.h"
#ifdef PTEXT_HOST_TEST
#include "render_host.h"
#else
#include "render.h"
#endif
#include "utf8.h"
#include <string.h>

int32_t ui_px(int32_t px)
{
    return (int32_t)(px * r_ui_scale() + 0.5f);
}

int32_t ui_fit_text(uint32_t *text, int32_t n, int32_t max_width)
{
    if (!text || n <= 0) return 0;
    if (n == 1) return 1;

    int32_t width = 0;
    int32_t fit = 0;
    while (fit < n) {
        int32_t adv = r_char_advance(text[fit]);
        if (width + adv > max_width) break;
        width += adv;
        fit++;
    }
    if (fit >= n) return n;
    if (fit < 1) fit = 1;
    text[fit - 1] = 0x2026;
    return fit;
}

int32_t ui_utf8_to_utf32(uint32_t *dst, int32_t max, const char *src)
{
    if (!dst || !src || max <= 0) return 0;
    const uint8_t *p = (const uint8_t *)src;
    const uint8_t *end = p + strlen(src);
    int32_t n = 0;

    while (n < max && p < end) {
        uint32_t cp = 0;
        Utf8Result result = utf8_next(&p, end, &cp);
        dst[n++] = result == UTF8_OK ? cp : 0xFFFD;
    }
    return n;
}
