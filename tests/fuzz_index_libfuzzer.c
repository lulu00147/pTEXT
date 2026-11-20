#include <stddef.h>
#include <stdint.h>

#include "reader.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int64_t off = -1;
    int32_t page = -1;
    uint32_t prefix = 0;
    (void)rd_test_idx_parse(data, size, 1048576, 123456,
                            0xA1B2C3D4u, 524288,
                            &off, &page, &prefix);
    return 0;
}
