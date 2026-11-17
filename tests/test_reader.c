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

    const char *blank_path = "blank.txt";
    const char *blank_text = "\n\n1. 2. 第一行\n第二行\n";
    write_file(blank_path, blank_text, strlen(blank_text));
    assert(rd_open_file(blank_path) == 0);
    rd_toggle_bookmark();
    Bookmark blank_bm[1];
    assert(rd_read_bookmarks(blank_path, blank_bm, 1) == 1);
    assert(strstr(blank_bm[0].preview, "第一行") != NULL);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_jump_to_offset(bookmark_offset);
    rd_next_page();
    rd_next_page();
    rd_jump_to_offset(bookmark_offset);
    assert(rd_get_page_no() == 2);

    rd_set_gap_idx(1);
    rd_goto_offset(rd_get_page()->start_offset);
    rd_jump_to_offset(bookmark_offset);
    assert(rd_get_page_no() > 0);

    rd_close();
    FILE *pf = fopen("../.prog/long.txt.bin", "wb");
    assert(pf != NULL);
    int64_t progress[3] = {bookmark_offset, -1, 2};
    assert(fwrite(progress, sizeof(progress[0]), 3, pf) == 3);
    fclose(pf);
    assert(rd_open_file(long_path) == 0);
    int32_t before_prev = rd_get_page_no();
    assert(before_prev > 0);
    rd_prev_page();
    if (before_prev > 1) assert(rd_get_page_no() == before_prev - 1);
    assert(rd_get_page()->start_offset >= 0);

    write_file("stale.txt.part", "x", 1);
    rd_scan_dir();
    assert(access("stale.txt.part", F_OK) != 0);

    FILE *bf = fopen("../.bm/long.txt.bin", "r+b");
    assert(bf != NULL);
    assert(fseek(bf, 20, SEEK_SET) == 0);
    int byte = fgetc(bf);
    assert(byte != EOF);
    assert(fseek(bf, 20, SEEK_SET) == 0);
    fputc(byte ^ 0xFF, bf);
    fclose(bf);
    rd_scan_dir();
    files = rd_get_files(&file_count);
    marked = 0;
    for (int32_t i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, long_path) == 0) marked = files[i].bookmarks;
    }
    assert(marked == 0);

    for (int32_t i = 0; i < 101; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%03d.txt", i);
        write_file(name, "x", 1);
    }
    rd_scan_dir();
    rd_get_files(&file_count);
    assert(file_count == 100);
    assert(rd_file_limit_reached() == 1);

    rd_close();
    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);

    puts("reader tests passed");
    return 0;
}
