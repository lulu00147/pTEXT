#include <assert.h>
#include <stdio.h>
#include "affinity.h"

int main(void)
{
    affinity_set_mode(-1);
    assert(affinity_test_mode() == 0);
    affinity_bind_current();

    affinity_set_mode(0);
    assert(affinity_test_mode() == 0);
    affinity_set_mode(1);
    assert(affinity_test_mode() == 1);
    affinity_set_mode(2);
    assert(affinity_test_mode() == 2);
    affinity_set_mode(3);
    assert(affinity_test_mode() == 2);
    affinity_bind_current();
    assert(affinity_test_ready() == 1);

    puts("affinity tests passed");
    return 0;
}
