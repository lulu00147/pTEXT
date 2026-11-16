#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utf8.h"
#include "gbk_table.h"

static uint32_t gbk_pair(unsigned char b1, unsigned char b2)
{
    if (b1 >= 0x81 && b1 <= 0xFE && b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F)
        return g_gbk_table[(b1 - 0x81) * 191 + (b2 - 0x40)];
    return 0;
}

int main(void)
{
    uint32_t rng = 0x13579BDFu;
    uint8_t buf[512];
    unsigned long long cases = 0;

    for (int b1 = 0; b1 < 256; b1++) {
        for (int b2 = 0; b2 < 256; b2++) {
            uint32_t cp = gbk_pair((unsigned char)b1, (unsigned char)b2);
            if (cp > 0x10FFFF) {
                fprintf(stderr, "bad GBK codepoint: %02X %02X => %X\n", b1, b2, cp);
                return 1;
            }
            cases++;
        }
    }

    for (int round = 0; round < 1000000; round++) {
        rng = rng * 1664525u + 1013904223u;
        size_t len = (size_t)(rng % 257);
        for (size_t i = 0; i < len; i++) {
            rng = rng * 1664525u + 1013904223u;
            buf[i] = (uint8_t)(rng >> 24);
        }
        const uint8_t *p = buf;
        const uint8_t *end = buf + len;
        while (p < end) {
            const uint8_t *before = p;
            uint32_t cp = 0;
            Utf8Result r = utf8_next(&p, end, &cp);
            if (p < before || p > end) {
                fprintf(stderr, "UTF-8 pointer escaped\n");
                return 1;
            }
            if (r == UTF8_END) break;
            if (r != UTF8_OK && cp != 0xFFFD) {
                fprintf(stderr, "invalid UTF-8 result without replacement\n");
                return 1;
            }
        }
        cases++;
    }

    printf("tests passed: %llu cases\n", cases);
    return 0;
}
