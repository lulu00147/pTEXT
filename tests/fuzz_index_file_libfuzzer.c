#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reader.h"

static char s_index_path[512];
static char s_prog_path[512];

static void write_bytes(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    if (len) (void)fwrite(data, 1, len, f);
    fclose(f);
}

static void make_text(const char *path)
{
    const char *unit = "分页索引文件级模糊测试，标点。下一句。\n";
    FILE *f = fopen(path, "wb");
    if (!f) abort();
    size_t n = strlen(unit);
    for (int i = 0; i < 3000; i++) {
        if (fwrite(unit, 1, n, f) != n) abort();
    }
    fclose(f);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    char dir[] = "/tmp/ptext-index-fuzz-XXXXXX";
    if (!mkdtemp(dir)) abort();
    rd_init(dir);
    char files[512];
    snprintf(files, sizeof(files), "%s/files", dir);
    if (chdir(files) != 0) abort();
    snprintf(s_index_path, sizeof(s_index_path), "../.idx/fuzz-large.txt.bin");
    snprintf(s_prog_path, sizeof(s_prog_path), "../.prog/fuzz-large.txt.bin");
    make_text("fuzz-large.txt");
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size > 20000) return 0;

    rd_set_font_idx(0);
    rd_set_indent_idx(0);
    rd_set_gap_idx(0);
    write_bytes(s_index_path, data, size);
    (void)unlink(s_prog_path);

    uint64_t raw = 0;
    if (size >= sizeof(raw)) memcpy(&raw, data, sizeof(raw));
    int64_t target = 300000 + (int64_t)(raw % 900000);
    if (rd_open_file("fuzz-large.txt") == 0) {
        rd_goto_offset(target);
        rd_close();
    }

    if (rd_open_file("fuzz-large.txt") == 0) {
        int64_t off = -1;
        int32_t page = -1;
        rd_reset_page_cache();
        (void)rd_test_idx_lookup(target, &off, &page);
        rd_close();
    }

    (void)unlink(s_index_path);
    (void)unlink(s_prog_path);
    return 0;
}
