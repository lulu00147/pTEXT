#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "persist.h"

static void write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1, strlen(data), f) == strlen(data));
    fclose(f);
}

int main(void)
{
    char dir[] = "/tmp/ptext-persist-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chdir(dir) == 0);

    write_file("state.bin", "old");
    assert(persist_write_atomic("state.bin", "new", 3) == 0);
    FILE *f = fopen("state.bin", "rb");
    assert(f != NULL);
    char buf[8] = {0};
    assert(fread(buf, 1, 3, f) == 3);
    fclose(f);
    assert(memcmp(buf, "new", 3) == 0);

    write_file("state.bin.tmp", "stale");
    assert(persist_write_atomic("state.bin", "ok", 2) == 0);
    assert(access("state.bin.tmp", F_OK) != 0);

    assert(mkdir("blocked", 0755) == 0);
    assert(persist_write_atomic("blocked", "x", 1) != 0);
    assert(access("blocked.tmp", F_OK) != 0);
    assert(access("blocked", F_OK) == 0);

    assert(persist_write_atomic("missing/state.bin", "x", 1) != 0);
    assert(access("missing/state.bin", F_OK) != 0);

    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);

    puts("persist tests passed");
    return 0;
}
