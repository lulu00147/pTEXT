#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "persist.h"

static char s_path[512];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    char dir[] = "/tmp/ptext-persist-fuzz-XXXXXX";
    if (!mkdtemp(dir) || chdir(dir) != 0) abort();
    snprintf(s_path, sizeof(s_path), "fuzz.bin");
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size > 4096) return 0;
    (void)persist_write_atomic(s_path, data, size);
    return 0;
}
