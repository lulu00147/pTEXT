#include <stddef.h>
#include <stdint.h>

#include "encoding.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size > 4096) return 0;

    int64_t off = 0;
    while (off < (int64_t)size) {
        int64_t before = off;
        uint32_t cp = enc_decode_utf8(data, (int64_t)size, &off);
        if (off <= before || off > (int64_t)size) return 0;
        if (cp && cp != 0xFFFD) {
            char enc[4];
            int32_t n = enc_utf8_encode(cp, enc);
            if (n > 0) {
                int64_t back_off = 0;
                (void)enc_decode_utf8((const uint8_t *)enc, n, &back_off);
            }
        }
    }

    off = 0;
    while (off < (int64_t)size) {
        int64_t before = off;
        (void)enc_decode_gbk(data, (int64_t)size, &off);
        if (off <= before || off > (int64_t)size) return 0;
    }

    (void)enc_utf8_prefix_ok(data, (int64_t)size);
    return 0;
}
