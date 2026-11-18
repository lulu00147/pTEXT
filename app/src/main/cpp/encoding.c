#include "encoding.h"
#include "utf8.h"
#include "gbk_table.h"
#include "gb18030_table.h"

static uint32_t gb18030_lookup(uint32_t idx)
{
    int32_t lo = 0, hi = GB18030_RANGE_COUNT - 1;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        const Gb18030Range *r = &g_gb18030_ranges[mid];
        if (idx < r->idx) {
            hi = mid - 1;
        } else if (idx - r->idx >= r->count) {
            lo = mid + 1;
        } else {
            return r->cp + (idx - r->idx);
        }
    }
    return '?';
}

static uint32_t gbk_decode_fast(unsigned char b1, unsigned char b2)
{
    if (b1 <= 0xFE && b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F) {
        uint16_t cp = g_gbk_table[(b1 - 0x81) * 191 + (b2 - 0x40)];
        if (cp) return cp;
    }
    return '?';
}

uint32_t enc_decode_utf8(const uint8_t *data, int64_t len, int64_t *off)
{
    if (!data || !off || *off < 0 || *off >= len) return 0;
    const uint8_t *base = data;
    const uint8_t *p = base + *off;
    uint32_t cp = 0;

    utf8_next(&p, base + len, &cp);
    *off = p - base;
    return cp;
}

uint32_t enc_decode_gbk(const uint8_t *data, int64_t len, int64_t *off)
{
    if (!data || !off || *off < 0 || *off >= len) return 0;

    unsigned char b1 = data[(*off)++];
    if (b1 < 0x80) return b1;
    if (b1 == 0x80) return 0x20AC;
    if (*off >= len) return '?';

    unsigned char b2 = data[*off];
    if (b2 >= 0x30 && b2 <= 0x39) {
        (*off)++;
        if (*off >= len) return '?';
        unsigned char b3 = data[(*off)++];
        if (b3 < 0x81 || b3 > 0xFE) return '?';
        if (*off >= len) return '?';
        unsigned char b4 = data[(*off)++];
        if (b4 < 0x30 || b4 > 0x39) return '?';
        uint32_t idx = (uint32_t)((b1 - 0x81) * 10 * 126 * 10 +
                                  (b2 - 0x30) * 126 * 10 +
                                  (b3 - 0x81) * 10 +
                                  (b4 - 0x30));
        return gb18030_lookup(idx);
    }

    (*off)++;
    return gbk_decode_fast(b1, b2);
}

int32_t enc_utf8_prefix_ok(const uint8_t *data, int64_t len)
{
    if (!data || len <= 0) return 1;
    int64_t limit = len < 4096 ? len : 4096;
    const uint8_t *p = data;
    const uint8_t *end = p + limit;

    while (p < end) {
        uint32_t cp;
        Utf8Result result = utf8_next(&p, end, &cp);
        if (result == UTF8_INVALID) return 0;
        if (result == UTF8_TRUNCATED) return 1;
    }
    return 1;
}

int32_t enc_utf8_encode(uint32_t cp, char out[4])
{
    if (!out || cp == 0) return 0;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF) ||
        (cp & 0xFFFE) == 0xFFFE) return 0;
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
