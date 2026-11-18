#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reader.h"

static uint32_t rng_state = 0x9E3779B9u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void write_bytes(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    if (len) assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

int main(void)
{
    char dir[] = "/tmp/ptext-fuzz-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    rd_init(dir);

    char files_dir[512], bm_dir[512], text_path[512], bm_path[512];
    snprintf(files_dir, sizeof(files_dir), "%s/files", dir);
    snprintf(bm_dir, sizeof(bm_dir), "%s/.bm", dir);
    snprintf(text_path, sizeof(text_path), "%s/fuzz.txt", files_dir);
    snprintf(bm_path, sizeof(bm_path), "%s/fuzz.txt.bin", bm_dir);
    assert(chdir(files_dir) == 0);

    uint8_t text[2048];
    uint8_t bm[2048];
    for (int32_t round = 0; round < 5000; round++) {
        rd_set_font_idx((int32_t)(rnd() % 7));
        rd_set_indent_idx((int32_t)(rnd() % 3));
        rd_set_gap_idx((int32_t)(rnd() % 3));
        rd_set_theme((int32_t)(rnd() & 1));

        size_t len = rnd() % sizeof(text);
        for (size_t i = 0; i < len; i++) text[i] = (uint8_t)rnd();
        write_bytes(text_path, text, len);

        size_t blen = rnd() % sizeof(bm);
        for (size_t i = 0; i < blen; i++) bm[i] = (uint8_t)rnd();
        write_bytes(bm_path, bm, blen);

        if (rd_open_file("fuzz.txt") == 0) {
            int32_t pages = (int32_t)(rnd() % 8);
            for (int32_t i = 0; i < pages && rd_can_next(); i++) rd_next_page();
            for (int32_t op = 0; op < 8; op++) {
                switch (rnd() % 6) {
                case 0:
                    if (rd_can_next()) rd_next_page();
                    break;
                case 1:
                    if (rd_can_prev()) rd_prev_page();
                    break;
                case 2:
                    rd_goto_offset((int64_t)(rnd() % (uint32_t)(len + 1)));
                    break;
                case 3:
                    rd_jump_to_offset((int64_t)(rnd() % (uint32_t)(len + 1)));
                    break;
                case 4:
                    rd_toggle_bookmark();
                    break;
                default: {
                    Bookmark b[8];
                    (void)rd_read_bookmarks("fuzz.txt", b, 8);
                    break;
                }
                }
            }
            Bookmark b[8];
            (void)rd_read_bookmarks("fuzz.txt", b, 8);
            rd_close();
        }
    }

    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);

    puts("reader fuzz passed: 5000 rounds");
    return 0;
}
