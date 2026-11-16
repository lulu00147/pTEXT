#include "ui_util.h"
#include "render.h"
#include "utf8.h"
#include <string.h>

int32_t ui_px(int32_t px)
{
    return (int32_t)(px * r_ui_scale() + 0.5f);
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
        if (result == UTF8_END) break;
        dst[n++] = result == UTF8_OK ? cp : 0xFFFD;
    }
    return n;
}
