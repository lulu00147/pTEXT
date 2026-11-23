#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "fuzz_seeds.h"
#include "utf8.h"

int main(void)
{
    const uint8_t *empty = (const uint8_t *)"";
    uint32_t empty_cp = 0;
    if (utf8_next(&empty, empty, &empty_cp) != UTF8_END) return 1;
    static const uint8_t bad_over2[] = {0xC0, 0x80};
    static const uint8_t bad_over3[] = {0xE0, 0x80, 0x80};
    static const uint8_t bad_over4[] = {0xF0, 0x80, 0x80, 0x80};
    static const uint8_t bad_sur[] = {0xED, 0xA0, 0x80};
    static const uint8_t bad_max[] = {0xF4, 0x90, 0x80, 0x80};
    static const uint8_t bad_nonchar[] = {0xEF, 0xBF, 0xBE};
    const uint8_t *p = bad_over2;
    if (utf8_next(&p, p + sizeof(bad_over2), &empty_cp) != UTF8_INVALID) return 1;
    p = bad_over3;
    if (utf8_next(&p, p + sizeof(bad_over3), &empty_cp) != UTF8_INVALID) return 1;
    p = bad_over4;
    if (utf8_next(&p, p + sizeof(bad_over4), &empty_cp) != UTF8_INVALID) return 1;
    p = bad_sur;
    if (utf8_next(&p, p + sizeof(bad_sur), &empty_cp) != UTF8_INVALID) return 1;
    p = bad_max;
    if (utf8_next(&p, p + sizeof(bad_max), &empty_cp) != UTF8_INVALID) return 1;
    p = bad_nonchar;
    if (utf8_next(&p, p + sizeof(bad_nonchar), &empty_cp) != UTF8_INVALID) return 1;
    static const uint8_t valid4[] = {0xF0, 0x90, 0x80, 0x80};
    p = valid4;
    if (utf8_next(&p, p + sizeof(valid4), &empty_cp) != UTF8_OK || empty_cp != 0x10000) return 1;
    uint32_t rng = 0x13579BDFu;
    uint8_t buf[512];
    unsigned long long cases = 0;

    for (int b1 = 0; b1 < 256; b1++) {
        for (int b2 = 0; b2 < 256; b2++) {
            uint8_t pair[2] = {(uint8_t)b1, (uint8_t)b2};
            int64_t off = 0;
            uint32_t cp = enc_decode_gbk(pair, 2, &off);
            if (cp > 0x10FFFF || off < 1 || off > 2) {
                fprintf(stderr, "bad GBK decode: %02X %02X => %X off=%lld\n",
                        b1, b2, cp, (long long)off);
                return 1;
            }
            cases++;
        }
    }

    for (size_t i = 0; i < sizeof(fuzz_seeds) / sizeof(fuzz_seeds[0]); i++) {
        int64_t off = 0;
        while (off < (int64_t)fuzz_seeds[i].len) {
            int64_t before = off;
            uint32_t cp = enc_decode_utf8(fuzz_seeds[i].data, (int64_t)fuzz_seeds[i].len, &off);
            if (off <= before || off > (int64_t)fuzz_seeds[i].len || cp > 0x10FFFF) {
                fprintf(stderr, "bad seed decode: %zu\n", i);
                return 1;
            }
        }
        cases++;
    }

    static const struct {
        uint8_t bytes[4];
        uint32_t cp;
    } gb4[] = {
        {{0x81,0x30,0x81,0x30}, 0x80},
        {{0x90,0x30,0x81,0x30}, 0x10000},
        {{0x94,0x39,0xFC,0x36}, 0x1F600},
        {{0x95,0x32,0x82,0x36}, 0x20000},
        {{0x84,0x31,0xA4,0x39}, 0xFFFF},
    };
    for (size_t i = 0; i < sizeof(gb4) / sizeof(gb4[0]); i++) {
        int64_t off = 0;
        uint32_t cp = enc_decode_gbk(gb4[i].bytes, 4, &off);
        if (cp != gb4[i].cp || off != 4) {
            fprintf(stderr, "bad GB18030: %X off=%lld\n", cp, (long long)off);
            return 1;
        }
        cases++;
    }

    for (int round = 0; round < 1000000; round++) {        rng = rng * 1664525u + 1013904223u;
        size_t len = (size_t)(rng % 257);
        for (size_t i = 0; i < len; i++) {
            rng = rng * 1664525u + 1013904223u;
            buf[i] = (uint8_t)(rng >> 24);
        }

        int64_t off = 0;
        while (off < (int64_t)len) {
            int64_t before = off;
            uint32_t cp = enc_decode_utf8(buf, (int64_t)len, &off);
            if (off < before || off > (int64_t)len || cp > 0x10FFFF) {
                fprintf(stderr, "bad UTF-8 decode\n");
                return 1;
            }
        }
        cases++;
    }

    for (uint32_t cp = 0; cp <= 0x10FFFF; cp += 4093) {
        if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) continue;
        char enc[4];
        if ((cp & 0xFFFE) == 0xFFFE) {
            if (enc_utf8_encode(cp, enc) != 0) {
                fprintf(stderr, "noncharacter encoded: %X\n", cp);
                return 1;
            }
            continue;
        }
        int32_t n = enc_utf8_encode(cp, enc);
        if (n < 1 || n > 4) {
            fprintf(stderr, "bad UTF-8 encode: %X\n", cp);
            return 1;
        }
        int64_t off = 0;
        uint32_t back = enc_decode_utf8((const uint8_t *)enc, n, &off);
        if (off != n || back != cp) {
            fprintf(stderr, "UTF-8 roundtrip failed: %X\n", cp);
            return 1;
        }
        cases++;
    }

    int64_t off = 0;
    if (enc_decode_utf8(NULL, 0, &off) != 0 || off != 0) return 1;
    off = -1;
    if (enc_decode_utf8((const uint8_t *)"x", 1, &off) != 0 || off != -1) return 1;
    off = 0;
    if (enc_decode_utf8((const uint8_t *)"\xFF", 1, &off) != 0xFFFD || off != 1) return 1;
    off = 0;
    if (enc_decode_utf8((const uint8_t *)"\xE4\xB8", 2, &off) != 0xFFFD || off != 2) return 1;
    off = 0;
    if (enc_decode_utf8((const uint8_t *)"\xC0\x80", 2, &off) != 0xFFFD || off != 2) return 1;
    if (enc_decode_utf8((const uint8_t *)"A", 1, NULL) != 0) return 1;
    off = 1;
    if (enc_decode_utf8((const uint8_t *)"A", 1, &off) != 0) return 1;
    off = 1;
    if (enc_decode_gbk((const uint8_t *)"A", 1, &off) != 0) return 1;
    off = -1;
    if (enc_decode_gbk((const uint8_t *)"A", 1, &off) != 0 || off != -1) return 1;
    off = 0;
    if (enc_decode_gbk(NULL, 0, &off) != 0 || off != 0) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x30\xFF\x30", 4, &off) != '?' || off != 3) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x30\x81\x3A", 4, &off) != '?' || off != 4) return 1;
    if (enc_utf8_prefix_ok(NULL, 0) != 1) return 1;
    if (enc_utf8_prefix_ok((const uint8_t *)"A", 0) != 1) return 1;
    if (enc_utf8_prefix_ok((const uint8_t *)"A", 1) != 1) return 1;
    if (enc_utf8_prefix_ok((const uint8_t *)"\xE4\xB8\xAD", 3) != 1) return 1;
    if (enc_utf8_prefix_ok((const uint8_t *)"\xFF", 1) != 0) return 1;
    if (enc_utf8_prefix_ok((const uint8_t *)"\xE4\xB8", 2) != 1) return 1;

    char enc[4];
    if (enc_utf8_encode('A', enc) != 1 || enc[0] != 'A') return 1;
    if (enc_utf8_encode(0x7FF, enc) != 2) return 1;
    if (enc_utf8_encode(0x800, enc) != 3) return 1;
    if (enc_utf8_encode(0x10000, enc) != 4) return 1;
    if (enc_utf8_encode(0x80, enc) != 2) return 1;
    if (enc_utf8_encode(0x10FFFF, enc) != 0) return 1;
    if (enc_utf8_encode(0x1FFFF, enc) != 0) return 1;
    if (enc_utf8_encode(0xFFFE, enc) != 0) return 1;
    if (enc_utf8_encode(0, enc) != 0) return 1;
    if (enc_utf8_encode(0xD800, enc) != 0) return 1;
    if (enc_utf8_encode(0x110000, enc) != 0) return 1;
    if (enc_utf8_encode('A', NULL) != 0) return 1;

    off = -1;
    if (enc_decode_gbk((const uint8_t *)"A", 1, &off) != 0 || off != -1) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\xFF\x40", 2, &off) != '?' || off != 2) return 1;

    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\xFF\x40", 2, &off) != '?' || off != 2) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x7F", 2, &off) != '?' || off != 2) return 1;

    off = 0;
    if (enc_decode_gbk((const uint8_t *)"A", 1, NULL) != 0) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x80", 1, &off) != 0x20AC || off != 1) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81", 1, &off) != '?' || off != 1) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x7F", 2, &off) != '?' || off != 2) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x30\x81", 3, &off) != '?' || off != 3) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x30\x80\x30", 4, &off) != '?' || off != 3) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\x81\x30\x81\x2F", 4, &off) != '?' || off != 4) return 1;
    off = 0;
    if (enc_decode_gbk((const uint8_t *)"\xFF\x39\xFE\x39", 4, &off) != '?' || off != 4) return 1;

    printf("tests passed: %llu cases\n", cases);
    return 0;
}
