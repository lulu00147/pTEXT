#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "reader.h"

static int fail_ftell;
static int fail_fstat;
static int fail_opendir;
static int fail_fopen;
static int fail_access;

int __real_ftell(FILE *stream);
int __real_fstat(int fd, struct stat *st);
DIR *__real_opendir(const char *path);
FILE *__real_fopen(const char *path, const char *mode);
int __real_access(const char *path, int mode);

long __wrap_ftell(FILE *stream)
{
    if (fail_ftell) return -1;
    return __real_ftell(stream);
}

int __wrap_fstat(int fd, struct stat *st)
{
    if (fail_fstat) return -1;
    return __real_fstat(fd, st);
}

DIR *__wrap_opendir(const char *path)
{
    if (fail_opendir) return NULL;
    return __real_opendir(path);
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (fail_fopen) return NULL;
    return __real_fopen(path, mode);
}

int __wrap_access(const char *path, int mode)
{
    if (fail_access) return -1;
    return __real_access(path, mode);
}

static int fseek_calls;
static int fread_calls;
static int fail_fseek_at;
static int fail_fread_at;
static int fail_snprintf;
static int fail_mmap;
static int fail_open;
static int snprintf_calls;
static int snprintf_fail_at;
static int snprintf_forced;

int __real_fseek(FILE *stream, long offset, int whence);
size_t __real_fread(void *ptr, size_t size, size_t count, FILE *stream);
int __real_open(const char *path, int flags, ...);
void *__real_mmap(void *addr, size_t length, int prot, int flags, int fd,
                  off_t offset);
int __wrap_open(const char *path, int flags, ...);
void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd,
                  off_t offset);
int __wrap_snprintf(char *buf, size_t size, const char *fmt, ...);

int __wrap_open(const char *path, int flags, ...)
{
    if (fail_open) {
        errno = EIO;
        return -1;
    }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __real_open(path, flags, mode);
}

void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd,
                  off_t offset)
{
    if (fail_mmap) return MAP_FAILED;
    return __real_mmap(addr, length, prot, flags, fd, offset);
}

int __wrap_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    snprintf_calls++;
    if (fail_snprintf) return -1;
    if (snprintf_fail_at > 0 && snprintf_calls == snprintf_fail_at) {
        return snprintf_forced;
    }
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return result;
}

int __wrap_fseek(FILE *stream, long offset, int whence)
{
    fseek_calls++;
    if (fail_fseek_at > 0 && fseek_calls == fail_fseek_at) {
        errno = EIO;
        return -1;
    }
    return __real_fseek(stream, offset, whence);
}

size_t __wrap_fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    fread_calls++;
    if (fail_fread_at > 0 && fread_calls == fail_fread_at) return 0;
    return __real_fread(ptr, size, count, stream);
}

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

int main(void)
{
    char dir[] = "/tmp/ptext-reader-io-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    rd_init(dir);

    char files_dir[512];
    char bm_path[512];
    snprintf(files_dir, sizeof(files_dir), "%s/files", dir);
    snprintf(bm_path, sizeof(bm_path), "%s/.bm/fault.txt.bin", dir);
    assert(chdir(files_dir) == 0);

    uint8_t bm[16 + sizeof(Bookmark)];
    memset(bm, 0, sizeof(bm));
    uint32_t header[4] = {0x314D4250u, 1, 1, 0};
    memcpy(bm, header, sizeof(header));
    write_file(bm_path, bm, sizeof(bm));

    Bookmark out[1];
    fail_fseek_at = 2;
    fail_fread_at = 0;
    fseek_calls = 0;
    fread_calls = 0;
    assert(rd_read_bookmarks("fault.txt", out, 1) == 0);

    fail_fseek_at = 0;
    fail_fread_at = 2;
    fseek_calls = 0;
    fread_calls = 0;
    assert(rd_read_bookmarks("fault.txt", out, 1) == 0);

    Settings settings = {3, 1, 0, 0, 14};
    char settings_path[512];
    snprintf(settings_path, sizeof(settings_path), "%s/.prog/settings.bin", dir);
    write_file(settings_path, &settings, sizeof(settings));

    fail_fseek_at = 1;
    fail_fread_at = 0;
    fseek_calls = 0;
    fread_calls = 0;
    rd_init(dir);
    assert(rd_get_settings()->version == 14);

    fail_fseek_at = 0;
    fail_fread_at = 1;
    fseek_calls = 0;
    fread_calls = 0;
    rd_init(dir);
    assert(rd_get_settings()->version == 14);

    settings.version = 13;
    write_file(settings_path, &settings, sizeof(settings));
    fail_fseek_at = 0;
    fail_fread_at = 0;
    fseek_calls = 0;
    fread_calls = 0;
    rd_init(dir);
    assert(rd_get_settings()->version == 14);

    fail_fseek_at = 2;
    fail_fread_at = 0;
    fseek_calls = 0;
    fread_calls = 0;
    rd_init(dir);

    fail_fseek_at = 0;
    fail_fread_at = 0;
    char side_buf[1024];
    fail_snprintf = 1;
    (void)rd_test_sidecar_path(side_buf, sizeof(side_buf), ".prog", "x");
    (void)rd_test_name_sidecar_path(side_buf, sizeof(side_buf), "x");
    fail_snprintf = 0;
    write_file("dead.part", "x", 1);
    write_file("scan.txt", "x", 1);

    snprintf_calls = 0;
    snprintf_fail_at = 2;
    snprintf_forced = 1400;
    rd_scan_dir();
    snprintf_calls = 0;
    snprintf_forced = -1;
    rd_scan_dir();
    unlink("dead.part");
    snprintf_calls = 0;
    snprintf_forced = 1024;
    rd_scan_dir();
    snprintf_calls = 0;
    snprintf_forced = -1;
    rd_scan_dir();
    snprintf_fail_at = 0;
    snprintf_forced = 0;

    write_file("mmap.txt", "x", 1);
    Bookmark hook_bm;
    memset(&hook_bm, 0, sizeof(hook_bm));
    fail_mmap = 1;
    assert(rd_open_file("mmap.txt") != 0);
    fail_mmap = 0;
    assert(rd_open_file("mmap.txt") == 0);
    rd_close();
    fail_open = 1;
    assert(rd_open_file("mmap.txt") != 0);
    fail_open = 0;
    hook_bm.offset = 0;
    fail_open = 1;
    rd_test_refresh_bookmark_previews("mmap.txt", &hook_bm, 1);
    fail_open = 0;
    fail_mmap = 1;
    rd_test_refresh_bookmark_previews("mmap.txt", &hook_bm, 1);
    fail_mmap = 0;

    char scan_dir[] = "/tmp/ptext-scan-XXXXXX";
    assert(mkdtemp(scan_dir) != NULL);
    rd_init(scan_dir);
    char scan_files[512];
    snprintf(scan_files, sizeof(scan_files), "%s/files", scan_dir);
    assert(chdir(scan_files) == 0);

    write_file("only.part", "x", 1);
    snprintf_calls = 0;
    snprintf_fail_at = 2;
    snprintf_forced = 1400;
    rd_scan_dir();
    snprintf_calls = 0;
    snprintf_forced = -1;
    rd_scan_dir();
    unlink("only.part");

    write_file("legacy.txt", "x", 1);
    Bookmark legacy_raw[1];
    memset(legacy_raw, 0, sizeof(legacy_raw));
    write_file("../.bm/legacy.txt.bin", legacy_raw, sizeof(legacy_raw));
    fail_ftell = 1;
    (void)rd_read_bookmarks("legacy.txt", legacy_raw, 1);
    fail_ftell = 0;

    fail_fseek_at = 1;
    fseek_calls = 0;
    (void)rd_read_bookmarks("legacy.txt", legacy_raw, 1);
    fail_fseek_at = 0;

    uint8_t many_raw[(MAX_BOOKMARKS + 1) * sizeof(Bookmark)];
    memset(many_raw, 0, sizeof(many_raw));
    write_file("../.bm/legacy-many.txt.bin", many_raw, sizeof(many_raw));
    (void)rd_read_bookmarks("legacy-many.txt", legacy_raw, 1);

    write_file("legacy-scan.txt", "x", 1);
    write_file("../.bm/legacy-scan.txt.bin", legacy_raw, sizeof(legacy_raw));
    fail_fseek_at = 1;
    fseek_calls = 0;
    rd_scan_dir();
    fail_fseek_at = 0;
    fail_ftell = 1;
    rd_scan_dir();
    fail_ftell = 0;
    write_file("legacy-many.txt", "x", 1);
    rd_scan_dir();

    fail_fstat = 1;
    write_file("only.txt", "x", 1);
    (void)rd_open_file("only.txt");
    fail_fstat = 0;
    rd_close();

    write_file("../.prog/orphan.bin", "x", 1);
    fail_access = 1;
    rd_scan_dir();
    fail_access = 0;

    fail_opendir = 1;
    rd_scan_dir();
    fail_opendir = 0;

    fail_fopen = 1;
    (void)rd_read_bookmarks("only.txt", legacy_raw, 1);
    fail_fopen = 0;

    write_file("only.txt", "x", 1);
    write_file("../.prog/notbin", "x", 1);
    snprintf_calls = 0;
    snprintf_forced = 1024;
    rd_scan_dir();
    snprintf_calls = 0;
    snprintf_forced = -1;
    rd_scan_dir();
    snprintf_fail_at = 0;
    snprintf_forced = 0;

    for (int side = 1; side <= 3; side++) {
        write_file("delete-line.txt", "x", 1);
        snprintf_calls = 0;
        snprintf_fail_at = side;
        snprintf_forced = -1;
        (void)rd_delete_file("delete-line.txt");
        snprintf_fail_at = 0;
    }
    snprintf_forced = 0;

    assert(chdir("/") == 0);
    char scan_cleanup[512];
    snprintf(scan_cleanup, sizeof(scan_cleanup), "rm -rf '%s'", scan_dir);
    assert(system(scan_cleanup) == 0);

    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("reader io fault tests passed");
    return 0;
}
