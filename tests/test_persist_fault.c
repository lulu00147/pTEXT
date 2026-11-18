#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "persist.h"

static int fail_write;
static int fail_write_other;
static int zero_write;
static int fail_fsync;
static int fail_rename;
static int fail_close;
static int fail_snprintf;

ssize_t __real_write(int fd, const void *buf, size_t count);
int __real_fsync(int fd);
int __real_rename(const char *oldpath, const char *newpath);
int __real_close(int fd);
int __wrap_snprintf(char *buf, size_t size, const char *fmt, ...);

int __wrap_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    if (fail_snprintf) return -1;
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return result;
}

ssize_t __wrap_write(int fd, const void *buf, size_t count);

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    if (fail_write) {
        fail_write = 0;
        errno = EINTR;
        return -1;
    }
    if (fail_write_other) {
        errno = EIO;
        return -1;
    }
    if (zero_write) return 0;
    return __real_write(fd, buf, count);
}

int __wrap_fsync(int fd)
{
    if (fail_fsync) return -1;
    return __real_fsync(fd);
}

int __wrap_rename(const char *oldpath, const char *newpath)
{
    if (fail_rename) return -1;
    return __real_rename(oldpath, newpath);
}

int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (fail_close) return -1;
    return result;
}

int main(void)
{
    char dir[] = "/tmp/ptext-persist-fault-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chdir(dir) == 0);

    fail_snprintf = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    fail_snprintf = 0;

    fail_write = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) == 0);
    assert(access("state.bin", F_OK) == 0);

    fail_write_other = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    assert(access("state.bin.tmp", F_OK) != 0);
    fail_write_other = 0;

    zero_write = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    assert(access("state.bin.tmp", F_OK) != 0);
    zero_write = 0;

    fail_fsync = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    assert(access("state.bin.tmp", F_OK) != 0);
    fail_fsync = 0;

    fail_rename = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    assert(access("state.bin.tmp", F_OK) != 0);
    fail_rename = 0;

    fail_close = 1;
    assert(persist_write_atomic("state.bin", "abc", 3) != 0);
    assert(access("state.bin.tmp", F_OK) != 0);
    fail_close = 0;

    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("persist fault tests passed");
    return 0;
}
