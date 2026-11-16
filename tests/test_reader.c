#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reader.h"

static void write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

int main(void)
{
    char dir[] = "/tmp/ptext-reader-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chdir(dir) == 0);
    rd_init(dir);
    char files_dir[512];
    snprintf(files_dir, sizeof(files_dir), "%s/files", dir);
    assert(chdir(files_dir) == 0);

    const char *empty_path = "empty.txt";
    write_file(empty_path, "", 0);
    assert(rd_open_file(empty_path) != 0);

    const char *long_path = "long.txt";
    char *text = malloc(40001);
    assert(text != NULL);
    for (int i = 0; i < 40000; i++) text[i] = 'a';
    write_file(long_path, text, 40000);
    free(text);

    assert(rd_open_file(long_path) == 0);
    Page *p = rd_get_page();
    PageLayout layout = rd_get_layout();
    assert(layout.margin_x > 0);
    assert(layout.text_width == 600 - layout.margin_x * 2);
    assert(layout.max_lines > 0 && layout.max_lines <= MAX_LINES);
    assert(p->count > 0);
    assert(p->count <= MAX_LINES);
    assert(p->lines[0].len <= MAX_CHARS);

    int32_t pages = 1;
    int64_t last = p->start_offset;
    while (rd_can_next() && pages < 200) {
        rd_next_page();
        p = rd_get_page();
        assert(p->count > 0);
        assert(p->start_offset > last);
        last = p->start_offset;
        pages++;
    }
    assert(pages > 1);
    rd_close();
    assert(unlink("../.prog/long.txt.bin") == 0);

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(0);
    int32_t normal_lines = rd_get_page()->count;
    assert(normal_lines > 0);

    rd_set_gap_idx(2);
    rd_goto_offset(0);
    int32_t gap_lines = rd_get_page()->count;
    assert(gap_lines > 0 && gap_lines <= normal_lines);

    rd_set_indent_idx(2);
    rd_goto_offset(0);
    assert(rd_get_page()->count > 0);

    rd_next_page();
    assert(rd_get_page_no() == 2);
    rd_toggle_bookmark();
    assert(rd_page_bookmarked());
    Bookmark bms[50];
    int32_t bm_count = rd_read_bookmarks(long_path, bms, 50);
    assert(bm_count == 1);
    assert(bms[0].offset >= 0);
    int64_t bookmark_offset = bms[0].offset;

    rd_close();
    rd_scan_dir();
    int32_t file_count = 0;
    FileItem *files = rd_get_files(&file_count);
    int32_t marked = 0;
    for (int32_t i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, long_path) == 0 && files[i].bookmarks == 1) marked = 1;
    }
    assert(marked == 1);

    assert(rd_open_file(long_path) == 0);
    rd_jump_to_offset(bookmark_offset);
    assert(rd_get_page_no() == 2);
    assert(rd_get_page()->start_offset == bookmark_offset);

    write_file("stale.txt.part", "x", 1);
    rd_scan_dir();
    assert(access("stale.txt.part", F_OK) != 0);

    rd_close();
    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);

    puts("reader tests passed");
    return 0;
}
