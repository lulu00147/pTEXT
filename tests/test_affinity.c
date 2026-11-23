#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "affinity.h"

int main(void)
{
    for (int32_t mode = -300; mode <= 300; mode++) {
        affinity_set_mode(mode);
        int32_t want = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
        assert(affinity_test_mode() == want);
    }
    affinity_bind_current();
    assert(affinity_test_ready() == 1);

    puts("affinity tests passed");
    return 0;
}
