#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "persist.h"

static int dir_fd = -1;
static int dir_syncs;
static int fail_dir_sync;

int __real_open(const char *path, int flags, ...);
int __real_fsync(int fd);
int __real_close(int fd);

int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    int fd = (flags & O_CREAT) ? __real_open(path, flags, mode)
                               : __real_open(path, flags);
#ifdef O_DIRECTORY
    if (fd >= 0 && (flags & O_DIRECTORY)) dir_fd = fd;
#else
    (void)path;
#endif
    return fd;
}

int __wrap_fsync(int fd)
{
    if (fd == dir_fd) {
        dir_syncs++;
        if (fail_dir_sync) return -1;
    }
    return __real_fsync(fd);
}

int __wrap_close(int fd)
{
    int n = __real_close(fd);
    if (fd == dir_fd) dir_fd = -1;
    return n;
}

int main(void)
{
    char dir[] = "/tmp/ptext-persist-dir-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chdir(dir) == 0);

    assert(persist_write_atomic("state.bin", "abc", 3) == 0);
    assert(dir_syncs > 0);
    assert(access("state.bin", F_OK) == 0);
    assert(access("state.bin.tmp", F_OK) != 0);

    assert(mkdir("sub", 0755) == 0);
    dir_syncs = 0;
    assert(persist_write_atomic("sub/state.bin", "abc", 3) == 0);
    assert(dir_syncs > 0);
    assert(access("sub/state.bin", F_OK) == 0);

    dir_syncs = 0;
    fail_dir_sync = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) == 0);
    assert(dir_syncs > 0);
    fail_dir_sync = 0;

    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("persist dir sync tests passed");
    return 0;
}
