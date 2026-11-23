#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    UTF8_END = 0,
    UTF8_OK = 1,
    UTF8_INVALID = -1,
    UTF8_TRUNCATED = -2
} Utf8Result;

/* Decode one codepoint from [*p, end). Always advances *p. */
static inline Utf8Result utf8_next(const uint8_t **p, const uint8_t *end,
                                   uint32_t *cp)
{
    const uint8_t *s = *p;
    if (s >= end) return UTF8_END;

    uint8_t c = *s++;
    if (c < 0x80) {
        *p = s;
        *cp = c;
        return UTF8_OK;
    }

    int32_t need;
    uint32_t value;
    if ((c & 0xE0) == 0xC0) {
        need = 1;
        value = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        value = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        value = c & 0x07;
    } else {
        *p = s;
        *cp = 0xFFFD;
        return UTF8_INVALID;
    }

    if ((size_t)(end - s) < (size_t)need) {
        *p = end;
        *cp = 0xFFFD;
        return UTF8_TRUNCATED;
    }

    for (int32_t i = 0; i < need; i++) {
        uint8_t b = s[i];
        if ((b & 0xC0) != 0x80) {
            *p = s + i;
            *cp = 0xFFFD;
            return UTF8_INVALID;
        }
        value = (value << 6) | (b & 0x3F);
    }
    *p = s + need;

    if ((need == 1 && value < 2) ||
        (need == 2 && value < 0x800) ||
        (need == 3 && value < 0x10000) ||
        value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF) ||
        (value & 0xFFFE) == 0xFFFE) {
        *cp = 0xFFFD;
        return UTF8_INVALID;
    }

    *cp = value;
    return UTF8_OK;
}

#endif
