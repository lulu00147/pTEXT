#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reader.h"

static char s_text_path[512];
static char s_bm_path[512];

static void write_bytes(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    if (len) (void)fwrite(data, 1, len, f);
    fclose(f);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    char dir[] = "/tmp/ptext-lfuzz-XXXXXX";
    if (!mkdtemp(dir)) abort();
    rd_init(dir);

    char files[512], bm[512];
    snprintf(files, sizeof(files), "%s/files", dir);
    snprintf(bm, sizeof(bm), "%s/.bm", dir);
    snprintf(s_text_path, sizeof(s_text_path), "%s/fuzz.txt", files);
    snprintf(s_bm_path, sizeof(s_bm_path), "%s/fuzz.txt.bin", bm);
    if (chdir(files) != 0) abort();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size > 8192) return 0;

    write_bytes(s_text_path, data, size);
    write_bytes(s_bm_path, data, size);

    if (rd_open_file("fuzz.txt") == 0) {
        for (int32_t i = 0; i < 4 && rd_can_next(); i++) rd_next_page();
        rd_close();
    }

    Bookmark b[8];
    (void)rd_read_bookmarks("fuzz.txt", b, 8);
    return 0;
}
