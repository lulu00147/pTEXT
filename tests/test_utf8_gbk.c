#include <stdint.h>
#include <stdio.h>

#include "encoding.h"
#include "fuzz_seeds.h"

int main(void)
{
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

    for (int round = 0; round < 1000000; round++) {
        rng = rng * 1664525u + 1013904223u;
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

    printf("tests passed: %llu cases\n", cases);
    return 0;
}
