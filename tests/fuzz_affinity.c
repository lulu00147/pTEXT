#include <stddef.h>
#include <stdint.h>

#include "affinity.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size < 1) return 0;
    affinity_set_mode((int32_t)data[0] - 128);
    affinity_bind_current();
    return 0;
}
