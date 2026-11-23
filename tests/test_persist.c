#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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

    assert(mkdir("targetdir", 0755) == 0);
    assert(persist_write_atomic("targetdir", "x", 1) != 0);
    assert(access("targetdir.tmp", F_OK) != 0);

    assert(persist_write_atomic(NULL, "x", 1) != 0);
    assert(persist_write_atomic("null.bin", NULL, 1) != 0);
    assert(persist_write_atomic("empty.bin", NULL, 0) == 0);
    struct stat empty_st;
    assert(stat("empty.bin", &empty_st) == 0 && empty_st.st_size == 0);

    enum { EX_LEN = 1024 };
    uint8_t payload[EX_LEN];
    for (int32_t i = 0; i < EX_LEN; i++) payload[i] = (uint8_t)(i * 37 + 11);
    for (size_t n = 0; n <= EX_LEN; n++) {
        assert(persist_write_atomic("exhaust.bin", payload, n) == 0);
        FILE *ef = fopen("exhaust.bin", "rb");
        assert(ef != NULL);
        uint8_t roundtrip[EX_LEN];
        assert(fread(roundtrip, 1, n, ef) == n);
        assert(memcmp(roundtrip, payload, n) == 0);
        assert(fgetc(ef) == EOF);
        fclose(ef);
    }
    char too_long[1200];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1] = '\0';
    assert(persist_write_atomic(too_long, "x", 1) != 0);

    struct rlimit old_limit;
    if (getrlimit(RLIMIT_FSIZE, &old_limit) == 0) {
        struct rlimit limit = old_limit;
        limit.rlim_cur = 1;
        if (setrlimit(RLIMIT_FSIZE, &limit) == 0) {
            signal(SIGXFSZ, SIG_IGN);
            assert(persist_write_atomic("limited.bin", "abc", 3) != 0);
            setrlimit(RLIMIT_FSIZE, &old_limit);
        }
    }

    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);

    puts("persist tests passed");
    return 0;
}
