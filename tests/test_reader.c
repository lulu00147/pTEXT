#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "reader.h"
#include "render_host.h"

static int count_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

static int count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) if (c == '\n') n++;
    fclose(f);
    return n;
}

static void write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static int32_t read_fifo_bookmarks(const char *fifo_path, const char *book_name,
                                   const unsigned char *data, size_t len)
{
    assert(mkfifo(fifo_path, 0600) == 0);
    pid_t writer = fork();
    assert(writer >= 0);
    if (writer == 0) {
        int fd = open(fifo_path, O_WRONLY);
        if (fd >= 0) {
            (void)write(fd, data, len);
            close(fd);
        }
        _exit(0);
    }
    Bookmark out[1];
    int32_t result = rd_read_bookmarks(book_name, out, 1);
    assert(waitpid(writer, NULL, 0) == writer);
    assert(unlink(fifo_path) == 0);
    return result;
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
    assert(rd_test_is_para_start(0) == 1);
    assert(rd_test_line_break_allowed('A', 'B') == 1);
    assert(rd_test_line_break_allowed(0x4E2D, 0x6587) == 1);
    assert(rd_test_line_break_allowed(0x4E2D, 0xFF0C) == 0);
    assert(rd_test_line_break_allowed(0x300A, 0x4E2D) == 0);
    assert(rd_test_line_break_allowed('A', 0x0301) == 0);
    assert(rd_open_file(empty_path) != 0);

    Settings saved_settings = *rd_get_settings();
    rd_set_indent_idx(0);
    rd_set_gap_idx(0);
    char cjk_text[256];
    int cjk_len = 0;
    for (int i = 0; i < 28; i++) {
        memcpy(cjk_text + cjk_len, "中", 3);
        cjk_len += 3;
    }
    memcpy(cjk_text + cjk_len, "文，后\n", 10);
    cjk_len += 10;
    write_file("cjk-break.txt", cjk_text, (size_t)cjk_len);
    assert(rd_open_file("cjk-break.txt") == 0);
    Page *cjk_page = rd_get_page();
    assert(cjk_page->count >= 2);
    assert(cjk_page->lines[0].len == 28);
    assert(cjk_page->lines[1].text[0] == 0x6587);
    assert(cjk_page->lines[1].text[1] == 0xFF0C);
    rd_close();
    (void)unlink("../.prog/cjk-break.txt.bin");

    cjk_len = 0;
    for (int i = 0; i < 28; i++) {
        memcpy(cjk_text + cjk_len, "中", 3);
        cjk_len += 3;
    }
    memcpy(cjk_text + cjk_len, "（文）\n", 10);
    cjk_len += 10;
    write_file("cjk-open.txt", cjk_text, (size_t)cjk_len);
    assert(rd_open_file("cjk-open.txt") == 0);
    cjk_page = rd_get_page();
    assert(cjk_page->count >= 2);
    assert(cjk_page->lines[0].len == 28);
    assert(cjk_page->lines[1].text[0] == 0xFF08);
    assert(cjk_page->lines[1].text[1] == 0x6587);
    rd_close();
    (void)unlink("../.prog/cjk-open.txt.bin");

    rd_set_indent_idx(saved_settings.indent_idx);
    rd_set_gap_idx(saved_settings.gap_idx);

    size_t index_len = 1200000;
    char *index_text = malloc(index_len);
    assert(index_text != NULL);
    const char *index_unit = "中文索引测试，句号。下一句。\n";
    size_t unit_len = strlen(index_unit);
    for (size_t i = 0; i < index_len; i++) index_text[i] = index_unit[i % unit_len];
    write_file("idx-large.txt", index_text, index_len);
    free(index_text);

    assert(rd_open_file("idx-large.txt") == 0);
    for (int i = 0; i < 200 && rd_can_next(); i++) rd_next_page();
    int32_t index_page = rd_get_page_no();
    assert(index_page > 1);
    rd_close();
    FILE *idx_file = fopen("../.idx/idx-large.txt.bin", "rb");
    assert(idx_file != NULL);
    fclose(idx_file);

    assert(unlink("../.idx/idx-large.txt.bin") == 0);
    assert(access("../.idx/idx-large.txt.bin", F_OK) != 0);
    assert(rd_open_file("idx-large.txt") == 0);
    for (int i = 0; i < 5 && rd_can_next(); i++) rd_next_page();
    rd_close();
    FILE *rebuilt_idx = fopen("../.idx/idx-large.txt.bin", "rb");
    assert(rebuilt_idx != NULL);
    fclose(rebuilt_idx);

    assert(rd_open_file("idx-large.txt") == 0);
    assert(rd_get_page_no() >= index_page);
    int64_t index_off = 0;
    int32_t index_no = 0;
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(rd_get_file_len() - 1, &index_off, &index_no) > 0);
    assert(index_off >= 0 && index_off < rd_get_file_len());
    assert(index_no > 0);

    rd_set_indent_idx((saved_settings.indent_idx + 1) % 3);
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(rd_get_file_len() - 1, &index_off, &index_no) == 0);
    rd_set_indent_idx(saved_settings.indent_idx);
    rd_set_gap_idx(saved_settings.gap_idx);
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(0, &index_off, &index_no) == 0);
    rd_close();

    write_file("../.idx/idx-large.txt.bin", "bad", 3);
    assert(rd_open_file("idx-large.txt") == 0);
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(rd_get_file_len() - 1, &index_off, &index_no) == 0);
    rd_close();

    char junk[20000];
    memset(junk, 0xA5, sizeof(junk));
    write_file("../.idx/idx-large.txt.bin", junk, sizeof(junk));
    assert(rd_open_file("idx-large.txt") == 0);
    rd_close();

    FILE *changed = fopen("idx-large.txt", "ab");
    assert(changed != NULL);
    assert(fputc('x', changed) != EOF);
    fclose(changed);
    assert(rd_open_file("idx-large.txt") == 0);
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(rd_get_file_len() - 1, &index_off, &index_no) == 0);
    rd_close();

    assert(rd_open_file("idx-large.txt") == 0);
    rd_close();
    assert(rd_delete_file("idx-large.txt") == 0);
    assert(access("../.prog/idx-large.txt.bin", F_OK) != 0);
    assert(access("../.idx/idx-large.txt.bin", F_OK) != 0);
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
    assert(access("../.idx/long.txt.bin", F_OK) != 0);
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
    Bookmark bms[MAX_BOOKMARKS];
    int32_t bm_count = rd_read_bookmarks(long_path, bms, MAX_BOOKMARKS);
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
    write_file("STALE.TXT.PART", "x", 1);
    rd_scan_dir();
    assert(access("stale.txt.part", F_OK) != 0);
    assert(access("STALE.TXT.PART", F_OK) != 0);

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
    assert(marked == 1);
    Bookmark corrupt[MAX_BOOKMARKS];
    assert(rd_read_bookmarks(long_path, corrupt, MAX_BOOKMARKS) == 0);

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
    int fds_before = count_dir("/proc/self/fd");
    int maps_before = count_lines("/proc/self/maps");
    for (int32_t i = 0; i < 50; i++) {
        assert(rd_open_file(long_path) == 0);
        rd_close();
    }
    int fds_after = count_dir("/proc/self/fd");
    int maps_after = count_lines("/proc/self/maps");
    assert(fds_before > 0 && maps_before > 0);
    assert(fds_after <= fds_before + 1);
    assert(maps_after <= maps_before + 4);

    const char *bom_path = "bom.txt";
    static const unsigned char bom[] = {0xEF, 0xBB, 0xBF, 'A', '\r', '\n', 'B'};
    write_file(bom_path, (const char *)bom, sizeof(bom));
    assert(rd_open_file(bom_path) == 0);
    assert(rd_get_file_len() == (int64_t)sizeof(bom));
    Page *bom_page = rd_get_page();
    assert(bom_page->start_offset == 3);
    assert(bom_page->count == 2);
    assert(bom_page->lines[0].len == 1);
    assert(bom_page->lines[0].text[0] == 'A');
    assert(bom_page->lines[1].len == 1);
    assert(bom_page->lines[1].text[0] == 'B');
    assert(strcmp(rd_get_book_name(), "bom") == 0);
    assert(!rd_can_next());
    rd_toggle_bookmark();
    assert(rd_page_bookmarked());
    Bookmark bom_bm[1];
    assert(rd_read_bookmarks(bom_path, bom_bm, 1) == 1);
    assert(bom_bm[0].preview[0] != '\0');
    rd_toggle_bookmark();
    assert(!rd_page_bookmarked());
    rd_close();

    const char *noext_path = "noext";
    write_file(noext_path, "x", 1);
    assert(rd_open_file(noext_path) == 0);
    assert(strcmp(rd_get_book_name(), "noext") == 0);
    rd_close();

    const char *upper_path = "UPPER.TXT";
    write_file(upper_path, "x", 1);
    assert(rd_open_file(upper_path) == 0);
    assert(strcmp(rd_get_book_name(), "UPPER") == 0);
    rd_close();

    const char *bin_path = "file.bin";
    write_file(bin_path, "x", 1);
    assert(rd_open_file(bin_path) == 0);
    assert(strcmp(rd_get_book_name(), "file.bin") == 0);
    rd_close();

    static const unsigned char bom1[] = {0xEF};
    static const unsigned char bom2[] = {0xEF, 0xBB};
    static const unsigned char bom3[] = {0xEF, 0xBB, 0x00};
    write_file("bom1.txt", (const char *)bom1, sizeof(bom1));
    assert(rd_open_file("bom1.txt") == 0);
    rd_close();
    write_file("bom2.txt", (const char *)bom2, sizeof(bom2));
    assert(rd_open_file("bom2.txt") == 0);
    rd_close();
    write_file("bom3.txt", (const char *)bom3, sizeof(bom3));
    assert(rd_open_file("bom3.txt") == 0);
    rd_close();

    const char *bom_only_path = "bomonly.txt";
    static const unsigned char bom_only[] = {0xEF, 0xBB, 0xBF};
    write_file(bom_only_path, (const char *)bom_only, sizeof(bom_only));
    int64_t bom_progress[3] = {3, 0, 1};
    FILE *bpf = fopen("../.prog/bomonly.txt.bin", "wb");
    assert(bpf != NULL);
    assert(fwrite(bom_progress, sizeof(bom_progress[0]), 3, bpf) == 3);
    fclose(bpf);
    assert(rd_open_file(bom_only_path) == 0);
    assert(rd_get_page_no() >= 1);
    rd_close();

    int64_t bom_prev_progress[3] = {3, 2, 1};
    bpf = fopen("../.prog/bomonly.txt.bin", "wb");
    assert(bpf != NULL);
    assert(fwrite(bom_prev_progress, sizeof(bom_prev_progress[0]), 3, bpf) == 3);
    fclose(bpf);
    assert(rd_open_file(bom_only_path) == 0);
    assert(rd_get_page_no() >= 1);
    rd_close();

    Bookmark empty_bm[1];
    assert(rd_read_bookmarks("empty.txt", empty_bm, 1) == 0);
    Bookmark empty_raw;
    memset(&empty_raw, 0, sizeof(empty_raw));
    empty_raw.offset = 0;
    snprintf(empty_raw.preview, sizeof(empty_raw.preview), "raw");
    FILE *empty_bmf = fopen("../.bm/empty.txt.bin", "wb");
    assert(empty_bmf != NULL);
    assert(fwrite(&empty_raw, sizeof(empty_raw), 1, empty_bmf) == 1);
    fclose(empty_bmf);
    assert(rd_read_bookmarks("empty.txt", empty_bm, 1) == 1);

    const char *fifo_path = "../.bm/fifo.txt.bin";
    uint8_t fifo_raw[64] = {0};
    assert(read_fifo_bookmarks(fifo_path, "fifo.txt", fifo_raw, sizeof(fifo_raw)) == 0);

    uint8_t fifo_header[16 + sizeof(Bookmark)];
    memset(fifo_header, 0, sizeof(fifo_header));
    uint32_t valid_header[4] = {0x314D4250u, 1, 1, 0};
    memcpy(fifo_header, valid_header, sizeof(valid_header));
    assert(read_fifo_bookmarks("../.bm/fifo-valid.txt.bin", "fifo-valid.txt",
                               fifo_header, sizeof(fifo_header)) == 0);

    const char *punct_path = "punct.txt";
    write_file(punct_path, ".\nA\n", 4);
    assert(rd_open_file(punct_path) == 0);
    assert(rd_get_page()->count == 1);
    assert(rd_get_page()->lines[0].text[0] == 'A');
    rd_close();

    const char *spaces_path = "spaces.txt";
    write_file(spaces_path, "   \nA\n", 6);
    assert(rd_open_file(spaces_path) == 0);
    assert(rd_get_page()->count == 1);
    assert(rd_get_page()->lines[0].text[0] == 'A');
    rd_close();

    const char *longline_path = "longline.txt";
    char longline[301];
    memset(longline, 'a', 300);
    longline[300] = '\0';
    write_file(longline_path, longline, 300);
    assert(rd_open_file(longline_path) == 0);
    assert(rd_get_page()->lines[0].justify == 1);
    rd_close();

    const char *justify_path = "justify.txt";
    rd_set_indent_idx(0);
    rd_set_gap_idx(0);
    unsigned char justify_text[64];
    memset(justify_text, 'a', 58);
    justify_text[58] = 0xE4;
    justify_text[59] = 0xB8;
    justify_text[60] = 0x81;
    justify_text[61] = 'a';
    write_file(justify_path, (const char *)justify_text, 62);
    assert(rd_open_file(justify_path) == 0);
    assert(rd_get_page()->lines[0].justify == 1);
    assert(rd_get_page()->lines[0].len == 60);
    rd_close();

    const char *huge_path = "huge.txt";
    char *huge = malloc(200000);
    assert(huge != NULL);
    memset(huge, 'a', 200000);
    write_file(huge_path, huge, 200000);
    free(huge);
    assert(rd_open_file(huge_path) == 0);
    for (int32_t guard = 0; guard < 70 && rd_can_next(); guard++) rd_next_page();
    assert(rd_get_page_no() >= 64);
    rd_prev_page();
    rd_close();

    assert(rd_open_file(huge_path) == 0);
    rd_goto_offset(0);
    for (int32_t i = 0; i < MAX_BOOKMARKS; i++) {
        rd_toggle_bookmark();
        assert(rd_can_next());
        rd_next_page();
    }
    Bookmark many_bms[MAX_BOOKMARKS];
    assert(rd_read_bookmarks(huge_path, many_bms, MAX_BOOKMARKS) == MAX_BOOKMARKS);
    rd_toggle_bookmark();
    assert(rd_read_bookmarks(huge_path, many_bms, MAX_BOOKMARKS) == MAX_BOOKMARKS);
    rd_close();

    const char *blank_preview_path = "blankpreview.txt";
    write_file(blank_preview_path, "   \n \n", 6);
    assert(rd_open_file(blank_preview_path) == 0);
    rd_toggle_bookmark();
    Bookmark blank_bm2[1];
    assert(rd_read_bookmarks(blank_preview_path, blank_bm2, 1) == 1);
    assert(blank_bm2[0].preview[0] == '\0');
    rd_close();

    const char *bad_header_path = "badheader.txt";
    write_file(bad_header_path, "x\n", 2);
    uint32_t bad_header[4] = {0x314D4250u, 1u, MAX_BOOKMARKS + 1, 0};
    FILE *bh = fopen("../.bm/badheader.txt.bin", "wb");
    assert(bh != NULL);
    assert(fwrite(bad_header, sizeof(bad_header), 1, bh) == 1);
    fclose(bh);
    Bookmark bad_header_out[1];
    assert(rd_read_bookmarks(bad_header_path, bad_header_out, 1) == 0);

    const char *size_header_path = "sizeheader.txt";
    write_file(size_header_path, "x\n", 2);
    uint32_t size_header[4] = {0x314D4250u, 1u, 0u, 0u};
    FILE *sh = fopen("../.bm/sizeheader.txt.bin", "wb");
    assert(sh != NULL);
    assert(fwrite(size_header, sizeof(size_header), 1, sh) == 1);
    assert(fputc('x', sh) != EOF);
    fclose(sh);
    Bookmark size_header_out[1];
    assert(rd_read_bookmarks(size_header_path, size_header_out, 1) == 0);

    const char *one_path = "one.txt";
    write_file(one_path, "A", 1);
    assert(rd_open_file(one_path) == 0);
    assert(rd_get_page()->count == 1);
    assert(!rd_can_next());
    rd_close();

    const char *truncated_path = "truncated.txt";
    static const unsigned char truncated[] = {0xE4, 0xB8};
    write_file(truncated_path, (const char *)truncated, sizeof(truncated));
    assert(rd_open_file(truncated_path) == 0);
    assert(rd_get_page()->count == 1);
    rd_close();

    write_file("../.bm/truncated-bookmark.txt.bin", "", 0);
    Bookmark truncated_bm[1];
    assert(rd_read_bookmarks("truncated-bookmark.txt", truncated_bm, 1) == 0);

    uint32_t two_bookmark_header[4] = {0x314D4250u, 1, 2, 0};
    uint8_t two_bookmarks[sizeof(two_bookmark_header) + 2 * sizeof(Bookmark)];
    memset(two_bookmarks, 0, sizeof(two_bookmarks));
    memcpy(two_bookmarks, two_bookmark_header, sizeof(two_bookmark_header));
    write_file("../.bm/two-bookmarks.txt.bin", (const char *)two_bookmarks,
               sizeof(two_bookmarks));
    assert(rd_read_bookmarks("two-bookmarks.txt", truncated_bm, 1) == 0);
    assert(rd_read_bookmarks("two-bookmarks.txt", NULL, 1) == 0);
    assert(rd_read_bookmarks("empty.txt", truncated_bm, 0) == 0);

    uint8_t checksum_data[16 + sizeof(Bookmark)];
    memset(checksum_data, 0, sizeof(checksum_data));
    uint32_t checksum_header[4] = {0x314D4250u, 1, 1, 0};
    memcpy(checksum_data, checksum_header, sizeof(checksum_header));
    write_file("../.bm/checksum.txt.bin", (const char *)checksum_data,
               sizeof(checksum_data));
    assert(rd_read_bookmarks("checksum.txt", truncated_bm, 1) == 0);

    uint32_t bad_version_header[4] = {0x314D4250u, 2, 1, 0};
    write_file("../.bm/bad-version.txt.bin", (const char *)bad_version_header,
               sizeof(bad_version_header));
    assert(rd_read_bookmarks("bad-version.txt", truncated_bm, 1) == 0);

    const char *punct2_path = "punct2.txt";
    write_file(punct2_path, ",.!?\nA\n", 7);
    assert(rd_open_file(punct2_path) == 0);
    assert(rd_get_page()->count == 1);
    assert(rd_get_page()->lines[0].text[0] == 'A');
    rd_close();

    const char *punct5_path = "punct5.txt";
    write_file(punct5_path, ",.!?;\nA\n", 8);
    assert(rd_open_file(punct5_path) == 0);
    assert(rd_get_page()->count == 2);
    rd_close();

    static const char *punct_cases[] = {
        "、。！？\nA\n",
        "，：；…\nA\n",
        "。！？…\nA\n",
        "—\nA\n",
        "，：；—\nA\n",
    };
    for (size_t i = 0; i < sizeof(punct_cases) / sizeof(punct_cases[0]); i++) {
        char name[32];
        snprintf(name, sizeof(name), "punct-case-%zu.txt", i);
        write_file(name, punct_cases[i], strlen(punct_cases[i]));
        assert(rd_open_file(name) == 0);
        assert(rd_get_page()->count == 1);
        assert(rd_get_page()->lines[0].text[0] == 'A');
        rd_close();
    }

    const char *cr_path = "cr.txt";
    write_file(cr_path, "A\rB\r\nC\n", 7);
    assert(rd_open_file(cr_path) == 0);
    assert(rd_get_page()->count == 3);
    assert(rd_get_page()->lines[0].text[0] == 'A');
    assert(rd_get_page()->lines[1].text[0] == 'B');
    assert(rd_get_page()->lines[2].text[0] == 'C');
    rd_close();

    Line punct_line;
    memset(&punct_line, 0, sizeof(punct_line));
    assert(rd_test_line_punct_only(&punct_line) == 0);
    punct_line.len = 5;
    punct_line.text[0] = ',';
    assert(rd_test_line_punct_only(&punct_line) == 0);
    punct_line.len = 1;
    punct_line.text[0] = ',';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = '.';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = '!';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = '?';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = ';';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = ':';
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0xFF0C;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0x3002;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0xFF01;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0xFF1F;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0xFF1B;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0xFF1A;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0x3001;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0x2026;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 0x2014;
    assert(rd_test_line_punct_only(&punct_line) == 1);
    punct_line.text[0] = 'A';
    assert(rd_test_line_punct_only(&punct_line) == 0);

    const char *para_path = "para.txt";
    write_file(para_path, "A\nB\rC", 5);
    assert(rd_open_file(para_path) == 0);
    assert(rd_test_is_para_start(0) == 1);
    assert(rd_test_is_para_start(2) == 1);
    assert(rd_test_is_para_start(4) == 1);
    assert(rd_test_is_para_start(3) == 0);
    assert(rd_test_is_para_start(100) == 0);
    rd_close();

    Bookmark hook_bm;
    char hook_buf[32];
    assert(rd_test_load_bookmarks(NULL, &hook_bm, 1) == 0);
    assert(rd_test_load_bookmarks("empty.txt", NULL, 1) == 0);
    assert(rd_test_load_bookmarks("empty.txt", &hook_bm, 0) == 0);
    assert(rd_test_read_name_sidecar("empty.txt", hook_buf, 0) == 0);
    char side_buf[1024];
    assert(rd_test_sidecar_path(side_buf, sizeof(side_buf), ".prog", "x") == 1);
    assert(rd_test_name_sidecar_path(side_buf, sizeof(side_buf), "x") == 1);
    write_file("named2.txt.name", "Name\n", 5);
    assert(rd_test_read_name_sidecar("named2.txt", hook_buf, sizeof(hook_buf)) == 1);
    assert(strcmp(hook_buf, "Name") == 0);
    write_file("named3.txt.name", "", 0);
    assert(rd_test_read_name_sidecar("named3.txt", hook_buf, sizeof(hook_buf)) == 0);
    write_file("named4.txt.name", "Name", 4);
    assert(rd_test_read_name_sidecar("named4.txt", hook_buf, sizeof(hook_buf)) == 1);
    assert(strcmp(hook_buf, "Name") == 0);
    write_file("named5.txt.name", "Name\r", 5);
    assert(rd_test_read_name_sidecar("named5.txt", hook_buf, sizeof(hook_buf)) == 1);
    assert(strcmp(hook_buf, "Name") == 0);

    render_host_set_scale(476.0f / 160.0f);
    rd_set_gap_idx(0);
    render_host_set_size(1240, 2772);
    assert(rd_test_page_max_lines() == 37);
    render_host_set_size(1264, 2780);
    assert(rd_test_page_max_lines() == 37);
    render_host_set_scale(1.0f);
    render_host_set_size(600, 800);

    assert(rd_test_page_max_lines() >= 1);
    assert(rd_test_page_max_lines() <= MAX_LINES);
    assert(rd_test_page_max_x() >= 1);
    hook_bm.offset = 0;
    rd_test_refresh_bookmark_previews(NULL, &hook_bm, 1);
    rd_test_refresh_bookmark_previews(long_path, NULL, 1);
    rd_test_refresh_bookmark_previews(long_path, &hook_bm, 0);
    rd_test_refresh_bookmark_previews(long_path, &hook_bm, 1);

    rd_reset_page_cache();
    rd_test_page_checkpoint_add(0, 0);
    rd_test_page_checkpoint_add(0, 1);
    rd_test_page_checkpoint_add(0, 1);
    rd_test_page_checkpoint_add(64, 64);
    int64_t checkpoint_offset = -1;
    assert(rd_test_page_checkpoint_find(64, &checkpoint_offset) >= 1);
    assert(checkpoint_offset >= 0);
    rd_test_hist_reset(0);
    for (int32_t i = 0; i < 80; i++) rd_test_hist_push(i);

    assert(rd_open_file(long_path) == 0);
    rd_test_paginate_init(0);
    assert(rd_test_prefetch_state() == 1);
    rd_test_paginate_step();
    for (int32_t i = 0; i < 10000 && rd_test_prefetch_state() == 1; i++) {
        rd_test_paginate_step();
    }
    assert(rd_test_prefetch_state() == 2);
    rd_test_paginate_init(rd_get_file_len());
    rd_test_paginate_step();
    assert(rd_test_prefetch_state() == 2);
    rd_close();

    const char *punct_step_path = "punct-step.txt";
    write_file(punct_step_path, ",\nA\n", 4);
    assert(rd_open_file(punct_step_path) == 0);
    rd_test_paginate_init(0);
    rd_test_paginate_step();
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_test_set_prefetch(1, -1);
    rd_next_page();
    rd_test_set_page_no(0);
    rd_next_page();
    rd_close();

    assert(rd_open_file(long_path) == 0);
    int64_t bad_start = 0;
    rd_test_set_bad_paginate(1);
    (void)rd_test_page_for_offset(10, &bad_start);
    rd_jump_to_offset(10);
    rd_test_set_bad_paginate(2);
    (void)rd_test_page_for_offset(10, &bad_start);
    rd_jump_to_offset(10);
    rd_test_set_bad_paginate(0);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(rd_get_file_len() / 2);
    rd_test_set_prev_injection(INT64_MAX, -1);
    rd_prev_page();
    rd_test_set_prev_injection(-1, -1);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(rd_get_file_len() / 2);
    rd_test_set_prev_injection(0, -1);
    rd_prev_page();
    rd_test_set_prev_injection(-1, -1);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(rd_get_file_len() / 2);
    rd_test_set_prev_injection(0, 10);
    rd_prev_page();
    rd_test_set_prev_injection(-1, -1);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) > 0);
    assert(rd_test_bookmark_preview(0, NULL, sizeof(hook_buf)) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, 0) == 0);
    assert(rd_test_bookmark_preview(rd_get_file_len(), hook_buf,
                                    sizeof(hook_buf)) == 0);
    rd_close();

    const char *preview_path = "preview.txt";
    write_file(preview_path, " \t　A\r\nB\n", 11);
    assert(rd_open_file(preview_path) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) > 0);
    assert(strcmp(hook_buf, "A") == 0);
    rd_close();

    const char *preview_lf_path = "preview-lf.txt";
    write_file(preview_lf_path, "A\nB\n", 4);
    assert(rd_open_file(preview_lf_path) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) > 0);
    assert(strcmp(hook_buf, "A") == 0);
    rd_close();

    const char *preview_cr_eof_path = "preview-cr-eof.txt";
    write_file(preview_cr_eof_path, "A\r", 2);
    assert(rd_open_file(preview_cr_eof_path) == 0);
    assert(rd_get_page()->lines[0].len == 1);
    assert(rd_get_page()->lines[0].text[0] == 'A');
    rd_close();

    const char *preview_nul_path = "preview-nul.txt";
    static const unsigned char preview_nul[] = {'A', 0, 'B'};
    write_file(preview_nul_path, (const char *)preview_nul, sizeof(preview_nul));
    assert(rd_open_file(preview_nul_path) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) > 0);
    assert(strcmp(hook_buf, "A") == 0);
    rd_close();

    const char *preview_short_path = "preview-short.txt";
    write_file(preview_short_path, "中", 3);
    assert(rd_open_file(preview_short_path) == 0);
    assert(rd_test_bookmark_preview(0, hook_buf, 1) == 0);
    rd_close();

    rd_goto_offset(0);
    rd_jump_to_offset(0);
    rd_toggle_bookmark();
    rd_save_progress();
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) == 0);

    rd_test_set_state("", 0, 0);
    rd_goto_offset(0);
    rd_jump_to_offset(0);
    rd_toggle_bookmark();
    rd_save_progress();
    assert(rd_test_bookmark_preview(0, hook_buf, sizeof(hook_buf)) == 0);
    rd_test_set_state("name", 1, -1);
    rd_save_progress();
    rd_test_clear_state();

    assert(rd_open_file(preview_short_path) == 0);
    assert(rd_test_bookmark_preview(-1, hook_buf, sizeof(hook_buf)) > 0);
    rd_close();

    render_host_set_scale(0.0f);
    assert(rd_open_file(preview_lf_path) == 0);
    assert(rd_get_layout().line_height >= 1);
    rd_close();
    render_host_set_scale(1.0f);
    render_host_set_cn_advance(0);
    (void)rd_get_indent_w();
    render_host_set_cn_advance(20);

    char long_line[5001];
    memset(long_line, 'a', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\n';
    write_file("longline.txt", long_line, sizeof(long_line));
    assert(rd_open_file("longline.txt") == 0);
    assert(rd_get_page()->count >= 1);
    rd_close();

    assert(rd_open_file(preview_short_path) == 0);
    rd_next_page();
    assert(!rd_can_next());
    rd_close();

    assert(mkdir("sub", 0700) == 0);
    write_file("sub/slash.txt", "x", 1);
    assert(rd_open_file("sub/slash.txt") == 0);
    assert(strcmp(rd_get_book_name(), "slash") == 0);
    rd_close();

    write_file("progress-empty.txt", "x", 1);
    write_file("../.prog/progress-empty.txt.bin", "", 0);
    assert(rd_open_file("progress-empty.txt") == 0);
    rd_close();
    int64_t negative_progress[3] = {-1, -1, 1};
    write_file("../.prog/progress-empty.txt.bin",
               (const char *)negative_progress, sizeof(negative_progress));
    assert(rd_open_file("progress-empty.txt") == 0);
    rd_close();

    rd_init(NULL);
    rd_init(dir);

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(-1);
    rd_goto_offset(INT64_MAX);
    rd_jump_to_offset(-1);
    rd_jump_to_offset(INT64_MAX);
    rd_close();

    assert(rd_open_file(preview_short_path) == 0);
    rd_goto_offset(rd_get_file_len() - 1);
    rd_next_page();
    assert(!rd_can_next());
    rd_test_paginate_step();
    rd_close();

    const char *blank_line_path = "blank-line.txt";
    write_file(blank_line_path, "\nA\n", 3);
    assert(rd_open_file(blank_line_path) == 0);
    rd_test_paginate_init(0);
    rd_test_paginate_step();
    rd_close();

    rd_test_set_state(NULL, 1, 0);
    rd_test_clear_state();

    const char *invalid_bm_path = "invalid-bm.txt";
    write_file(invalid_bm_path, "abcd", 4);
    Bookmark invalid_bm[2];
    memset(invalid_bm, 0, sizeof(invalid_bm));
    invalid_bm[0].offset = -5;
    invalid_bm[1].offset = 1000;
    FILE *invalid_f = fopen("../.bm/invalid-bm.txt.bin", "wb");
    assert(invalid_f != NULL);
    assert(fwrite(invalid_bm, sizeof(Bookmark), 2, invalid_f) == 2);
    fclose(invalid_f);
    assert(rd_open_file(invalid_bm_path) == 0);
    Bookmark invalid_out[2];
    assert(rd_read_bookmarks(invalid_bm_path, invalid_out, 2) == 2);
    assert(invalid_out[0].offset == -5);
    assert(invalid_out[1].offset == 1000);
    rd_close();

    rd_reset_page_cache();
    for (int32_t i = 0; i < 1100; i++) {
        rd_test_page_checkpoint_add((int64_t)i * 64, (i + 1) * 64);
    }
    int64_t ckpt_out = -1;
    (void)rd_test_page_checkpoint_find(-1, &ckpt_out);

    render_host_set_size(100000, 800);
    write_file("longline2.txt", long_line, sizeof(long_line));
    assert(rd_open_file("longline2.txt") == 0);
    assert(rd_get_page()->lines[0].len >= MAX_CHARS - 1);
    rd_close();
    render_host_set_size(600, 800);

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(rd_get_file_len() / 2);
    rd_test_set_bad_paginate(1);
    rd_prev_page();
    rd_test_set_bad_paginate(0);
    rd_prefetch_step();
    rd_close();

    const char *bm_notpage_path = "bm-notpage.txt";
    write_file(bm_notpage_path, "abcdef", 6);
    Bookmark notpage_bm;
    memset(&notpage_bm, 0, sizeof(notpage_bm));
    notpage_bm.offset = 5;
    FILE *notpage_f = fopen("../.bm/bm-notpage.txt.bin", "wb");
    assert(notpage_f != NULL);
    assert(fwrite(&notpage_bm, sizeof(notpage_bm), 1, notpage_f) == 1);
    fclose(notpage_f);
    assert(rd_open_file(bm_notpage_path) == 0);
    rd_goto_offset(4);
    assert(!rd_page_bookmarked());
    (void)rd_read_bookmarks("sub/slash.txt", invalid_out, 2);
    rd_close();

    const char *refresh_bom_path = "refresh-bom.txt";
    static const unsigned char refresh_bom[] = {0xEF, 0xBB, 0xBF, 'A'};
    write_file(refresh_bom_path, (const char *)refresh_bom, sizeof(refresh_bom));
    hook_bm.offset = 0;
    rd_test_refresh_bookmark_previews(refresh_bom_path, &hook_bm, 1);
    const char *refresh_plain_path = "refresh-plain.txt";
    write_file(refresh_plain_path, "plain", 5);
    rd_test_refresh_bookmark_previews(refresh_plain_path, &hook_bm, 1);
    write_file("refresh-short", "x", 1);
    rd_test_refresh_bookmark_previews("refresh-short", &hook_bm, 1);
    static const unsigned char refresh_bom_partial1[] = {0xEF, 0xBB, 0x00};
    write_file("refresh-bom1.txt", (const char *)refresh_bom_partial1,
               sizeof(refresh_bom_partial1));
    rd_test_refresh_bookmark_previews("refresh-bom1.txt", &hook_bm, 1);
    static const unsigned char refresh_bom_partial2[] = {0xEF, 0x00, 0xBB};
    write_file("refresh-bom2.txt", (const char *)refresh_bom_partial2,
               sizeof(refresh_bom_partial2));
    rd_test_refresh_bookmark_previews("refresh-bom2.txt", &hook_bm, 1);

    const char *delete_dir_path = "deleteside-dir.txt";
    write_file(delete_dir_path, "x", 1);
    assert(mkdir("deleteside-dir.txt.name", 0700) == 0);
    assert(mkdir("../.prog/deleteside-dir.txt.bin", 0700) == 0);
    assert(mkdir("../.bm/deleteside-dir.txt.bin", 0700) == 0);
    assert(rd_delete_file(delete_dir_path) != 0);

    const char *delete_ok_path = "delete-ok.txt";
    write_file(delete_ok_path, "x", 1);
    assert(rd_delete_file(delete_ok_path) == 0);

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(0);
    assert(rd_can_next());
    rd_next_page();
    int64_t second_page_start = rd_get_page()->start_offset;
    assert(second_page_start > 0);
    rd_close();
    FILE *pp = fopen("../.prog/long.txt.bin", "wb");
    assert(pp != NULL);
    int64_t previous_progress[3] = {second_page_start, 0, 2};
    assert(fwrite(previous_progress, sizeof(previous_progress[0]), 3, pp) == 3);
    fclose(pp);
    assert(rd_open_file(long_path) == 0);
    assert(rd_get_page()->start_offset == second_page_start);
    rd_prev_page();
    assert(rd_get_page_no() >= 1);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    for (int32_t guard = 0; guard < 10000 && rd_is_prefetching(); guard++) {
        rd_prefetch_step();
    }
    assert(!rd_is_prefetching());
    if (rd_can_next()) {
        int64_t prefetched_start = rd_get_page()->next_offset;
        rd_next_page();
        assert(rd_get_page()->start_offset == prefetched_start);
        rd_prev_page();
        assert(rd_get_page()->start_offset < prefetched_start);
    }
    rd_close();

    rd_set_font_idx(6);
    assert(rd_get_settings()->font_idx == 6);
    rd_set_font_idx(0);
    assert(rd_get_settings()->font_idx == 0);
    rd_set_theme(1);
    assert(rd_get_settings()->theme == 1);
    rd_set_theme(0);
    assert(rd_get_settings()->theme == 0);

    const char *gbk_path = "gbk.txt";
    static const unsigned char gbk[] = {0xC4, 0xE3, 0xBA, 0xC3, '\n'};
    write_file(gbk_path, (const char *)gbk, sizeof(gbk));
    assert(rd_open_file(gbk_path) == 0);
    Page *gbk_page = rd_get_page();
    assert(gbk_page->count == 1);
    assert(gbk_page->lines[0].len == 2);
    assert(gbk_page->lines[0].text[0] == 0x4F60);
    assert(gbk_page->lines[0].text[1] == 0x597D);
    rd_close();

    const char *named_path = "named.txt";
    write_file(named_path, "x\n", 2);
    write_file("named.txt.name", "显示名\n", strlen("显示名\n"));
    rd_scan_dir();
    FileItem *named_files = rd_get_files(&file_count);
    int32_t named_seen = 0;
    for (int32_t i = 0; i < file_count; i++) {
        if (strcmp(named_files[i].name, "显示名") == 0) named_seen = 1;
    }
    assert(named_seen == 1);
    assert(rd_open_file(named_path) == 0);
    assert(strcmp(rd_get_book_name(), "显示名") == 0);
    rd_close();

    const char *legacy_path = "legacy.txt";
    write_file(legacy_path, "legacy-preview\n", 15);
    Bookmark legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.offset = 0;
    snprintf(legacy.preview, sizeof(legacy.preview), "%s", "legacy-preview");
    FILE *lf = fopen("../.bm/legacy.txt.bin", "wb");
    assert(lf != NULL);
    assert(fwrite(&legacy, sizeof(legacy), 1, lf) == 1);
    fclose(lf);
    assert(rd_open_file(legacy_path) == 0);
    Bookmark legacy_out[2];
    assert(rd_read_bookmarks(legacy_path, legacy_out, 2) == 1);
    assert(legacy_out[0].offset == 0);
    rd_scan_dir();
    named_files = rd_get_files(&file_count);
    int32_t legacy_marked = 0;
    for (int32_t i = 0; i < file_count; i++) {
        if (strcmp(named_files[i].name, legacy_path) == 0) legacy_marked = named_files[i].bookmarks;
    }
    assert(legacy_marked == 1);
    rd_close();

    const char *delete_path = "delete.txt";
    write_file(delete_path, "delete\n", 7);
    write_file("delete.txt.name", "delete name\n", 12);
    write_file("../.prog/delete.txt.bin", "p", 1);
    write_file("../.bm/delete.txt.bin", "b", 1);
    assert(rd_delete_file(delete_path) == 0);
    assert(access(delete_path, F_OK) != 0);
    assert(access("delete.txt.name", F_OK) != 0);
    assert(access("../.prog/delete.txt.bin", F_OK) != 0);
    assert(access("../.bm/delete.txt.bin", F_OK) != 0);
    rd_scan_dir();
    named_files = rd_get_files(&file_count);
    for (int32_t i = 0; i < file_count; i++) assert(strcmp(named_files[i].name, delete_path) != 0);

    const char *delete_side_path = "deleteside.txt";
    write_file(delete_side_path, "delete\n", 7);
    assert(mkdir("deleteside.txt.name", 0755) == 0);
    assert(rd_delete_file(delete_side_path) == -2);
    assert(access(delete_side_path, F_OK) != 0);
    assert(rmdir("deleteside.txt.name") == 0);

    assert(rd_open_file(long_path) == 0);
    rd_goto_offset(0);
    assert(rd_can_next());
    rd_next_page();
    second_page_start = rd_get_page()->start_offset;
    assert(second_page_start > 0);
    rd_goto_offset(second_page_start);
    assert(rd_can_prev());
    rd_prev_page();
    rd_goto_offset(second_page_start + 1);
    assert(rd_can_prev());
    rd_prev_page();
    rd_goto_offset(second_page_start);
    rd_reset_page_cache();
    rd_jump_to_offset(0);
    assert(rd_get_page_no() >= 1);
    rd_close();

    assert(rd_open_file(long_path) == 0);
    for (int32_t guard = 0; guard < 80 && rd_can_next(); guard++) rd_next_page();
    rd_prev_page();
    rd_close();

    assert(rd_open_file(long_path) == 0);
    rd_test_paginate_full(-1);
    assert(rd_get_page()->start_offset == 0);
    rd_test_paginate_full(INT64_MAX);
    assert(rd_get_page()->start_offset == INT64_MAX);
    assert(rd_get_page()->next_offset == INT64_MAX);
    rd_goto_offset(0);
    rd_close();

    assert(rd_delete_file("missing-delete.txt") != 0);

    static const unsigned char short_settings[11] = {0};
    write_file("../.prog/settings.bin", (const char *)short_settings, sizeof(short_settings));
    rd_init(dir);
    Settings *settings = rd_get_settings();
    assert(settings->font_idx == 3);
    assert(settings->indent_idx == 1);
    assert(settings->gap_idx == 0);
    assert(settings->version == 14);

    int32_t settings12[3] = {1, 2, 1};
    write_file("../.prog/settings.bin", (const char *)settings12, sizeof(settings12));
    rd_init(dir);
    settings = rd_get_settings();
    assert(settings->font_idx == 3);
    assert(settings->indent_idx == 1);
    assert(settings->gap_idx == 0);

    int32_t settings16[4] = {1, 2, 1, 1};
    write_file("../.prog/settings.bin", (const char *)settings16, sizeof(settings16));
    rd_init(dir);
    settings = rd_get_settings();
    assert(settings->font_idx == 3);
    assert(settings->indent_idx == 1);
    assert(settings->gap_idx == 0);

    Settings valid_settings = {4, 2, 1, 1, 14};
    write_file("../.prog/settings.bin", (const char *)&valid_settings, sizeof(valid_settings));
    rd_init(dir);
    settings = rd_get_settings();
    assert(settings->font_idx == 4);
    assert(settings->indent_idx == 2);
    assert(settings->gap_idx == 1);
    assert(settings->theme == 1);
    assert(settings->version == 14);
    assert(settings->affinity_idx == 0);

    int32_t old_settings20[5] = {5, 0, 2, 0, 14};
    write_file("../.prog/settings.bin", (const char *)old_settings20,
               sizeof(old_settings20));
    rd_init(dir);
    settings = rd_get_settings();
    assert(settings->font_idx == 5);
    assert(settings->indent_idx == 0);
    assert(settings->gap_idx == 2);
    assert(settings->affinity_idx == 2);

    Settings affinity_settings = {4, 2, 1, 1, 14, 1};
    write_file("../.prog/settings.bin", (const char *)&affinity_settings,
               sizeof(affinity_settings));
    rd_init(dir);
    assert(rd_get_settings()->affinity_idx == 1);

    static const unsigned char bad_settings[16] = {0};
    write_file("../.prog/settings.bin", (const char *)bad_settings, sizeof(bad_settings));
    rd_init(dir);
    settings = rd_get_settings();
    assert(settings->font_idx == 3);
    assert(settings->indent_idx == 1);
    assert(settings->gap_idx == 0);
    assert(settings->version == 14);

    Line blank_line;
    memset(&blank_line, 0, sizeof(blank_line));
    assert(rd_line_blank(&blank_line) == 1);
    blank_line.len = 3;
    blank_line.text[0] = ' ';
    blank_line.text[1] = '\t';
    blank_line.text[2] = 0x3000;
    assert(rd_line_blank(&blank_line) == 1);
    blank_line.text[1] = 'A';
    assert(rd_line_blank(&blank_line) == 0);

    assert(rd_delete_file(NULL) != 0);
    assert(rd_open_file(NULL) != 0);
    assert(rd_read_bookmarks(NULL, empty_bm, 1) == 0);
    assert(rd_get_files(NULL) != NULL);
    rd_close();
    rd_goto_offset(-1);
    rd_jump_to_offset(INT64_MAX);
    rd_save_progress();

    rd_set_font_idx(-1);
    assert(rd_get_settings()->font_idx == 0);
    rd_set_font_idx(99);
    assert(rd_get_settings()->font_idx == 6);
    rd_set_indent_idx(-1);
    assert(rd_get_settings()->indent_idx == 0);
    rd_set_indent_idx(99);
    assert(rd_get_settings()->indent_idx == 2);
    rd_set_gap_idx(-1);
    assert(rd_get_settings()->gap_idx == 0);
    rd_set_gap_idx(99);
    assert(rd_get_settings()->gap_idx == 2);
    rd_set_theme(-1);
    assert(rd_get_settings()->theme == 1);
    rd_set_theme(99);
    assert(rd_get_settings()->theme == 1);
    rd_set_theme(0);
    rd_set_affinity_idx(99);
    assert(rd_get_settings()->affinity_idx == 2);
    rd_set_affinity_idx(-1);
    assert(rd_get_settings()->affinity_idx == 0);

    rd_set_gap_idx(0);
    int32_t line_h0 = rd_get_line_h();
    rd_set_gap_idx(2);
    int32_t line_h2 = rd_get_line_h();
    assert(line_h0 > 0 && line_h2 > line_h0);
    rd_set_gap_idx(0);

    rd_set_indent_idx(0);
    assert(rd_get_indent_w() == 0);
    rd_set_indent_idx(2);
    assert(rd_get_indent_w() > 0);
    rd_set_indent_idx(1);

    assert(rd_open_file(".") != 0);

    rd_close();
    int64_t short_progress[1] = {0};
    FILE *spf = fopen("../.prog/one.txt.bin", "wb");
    assert(spf != NULL);
    assert(fwrite(short_progress, sizeof(short_progress[0]), 1, spf) == 1);
    fclose(spf);
    assert(rd_open_file(one_path) == 0);
    assert(rd_get_page()->start_offset == 0);
    rd_close();

    int64_t huge_progress[1] = {INT64_MAX};
    FILE *hpf = fopen("../.prog/one.txt.bin", "wb");
    assert(hpf != NULL);
    assert(fwrite(huge_progress, sizeof(huge_progress[0]), 1, hpf) == 1);
    fclose(hpf);
    assert(rd_open_file(one_path) == 0);
    assert(rd_get_page()->start_offset >= 0);
    rd_close();

    assert(rd_read_bookmarks("empty.txt", empty_bm, 0) == 0);
    assert(rd_read_bookmarks("empty.txt", NULL, 1) == 0);
    assert(rd_test_bookmark_preview_null() == 0);
    assert(rd_test_bookmark_preview_bad_decode() == 0);
    assert(rd_test_page_needs_checkpoint(64) == 0);
    assert(rd_test_page_needs_checkpoint(65) == 0);
    assert(rd_test_has_break(0) == 0);
    assert(rd_test_has_break(1) == 1);
    rd_test_apply_break(0);
    rd_test_apply_break(1);
    assert(rd_test_prev_safety_limit(2) < rd_test_prev_safety_limit(1));
    rd_test_layout_break();
    assert(rd_open_file(long_path) == 0);
    rd_test_set_page_no(64);
    rd_jump_to_offset(rd_get_file_len() / 2);
    rd_close();
    rd_test_idx_save_guard();
    rd_test_cleanup_sidecars(".");
    assert(rd_test_bookmark_count("missing.bin") == 0);
    assert(rd_open_file("missing-file.txt") != 0);
    int64_t no_idx_off = -1;
    int32_t no_idx_page = -1;
    assert(rd_test_idx_lookup(0, &no_idx_off, &no_idx_page) == 0);
    rd_prefetch_step();

    write_file("basename.txt", "x", 1);
    assert(rd_delete_file("basename.txt") == 0);
    assert(access("basename.txt", F_OK) != 0);

    assert(mkdir("deldir", 0755) == 0);
    write_file("deldir/del.txt", "x", 1);
    assert(rd_delete_file("deldir/del.txt") == 0);
    assert(rmdir("deldir") == 0);

    write_file("delside.txt", "x", 1);
    assert(mkdir("../.prog/delside.txt.bin", 0755) == 0);
    assert(mkdir("../.bm/delside.txt.bin", 0755) == 0);
    assert(mkdir("../.idx/delside.txt.bin", 0755) == 0);
    assert(rd_delete_file("delside.txt") == -2);
    assert(rmdir("../.prog/delside.txt.bin") == 0);
    assert(rmdir("../.bm/delside.txt.bin") == 0);
    assert(rmdir("../.idx/delside.txt.bin") == 0);

    const char *break_text = "aaaa bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    write_file("break.txt", break_text, (uint32_t)strlen(break_text));
    render_host_set_size(80, 800);
    assert(rd_open_file("break.txt") == 0);
    assert(rd_get_page()->count > 0);
    rd_close();
    render_host_set_size(600, 800);

    char nodir[512];
    char nodir_files[600];
    snprintf(nodir, sizeof(nodir), "%s/nodir", dir);
    snprintf(nodir_files, sizeof(nodir_files), "%s/files", nodir);
    assert(mkdir(nodir, 0755) == 0);
    rd_init(nodir);
    assert(rmdir(nodir_files) == 0);
    rd_scan_dir();
    int32_t nodir_count = -1;
    (void)rd_get_files(&nodir_count);
    assert(nodir_count == 0);
    rd_init(dir);

    render_host_set_size(1, 1);
    assert(rd_open_file(one_path) == 0);
    PageLayout tiny_layout = rd_get_layout();
    assert(tiny_layout.text_width >= 1);
    assert(tiny_layout.line_height >= 1);
    Page *tiny_page = rd_get_page();
    assert(tiny_page->count <= MAX_LINES);
    for (int32_t i = 0; i < tiny_page->count; i++) {
        assert(tiny_page->lines[i].len >= 0);
        assert(tiny_page->lines[i].len < MAX_CHARS);
    }
    rd_close();

    render_host_set_size(600, 100000);
    assert(rd_open_file(one_path) == 0);
    assert(rd_get_page()->count <= MAX_LINES);
    rd_close();
    render_host_set_size(600, 800);

    const char *punct_sweep = "([{)]},.:;!?，。！？；：、“”‘’（）《》〈〉「」『』【】〔〕〖〗〘〚〛〉》」』】〕〗〙〛！）．：；？］｝»’”—…、。";
    write_file("punct-sweep.txt", punct_sweep, (uint32_t)strlen(punct_sweep));
    assert(rd_open_file("punct-sweep.txt") == 0);
    assert(rd_get_page()->count > 0);
    rd_close();

    static const uint32_t break_chars[] = {
        '(', '[', '{', ')', ']', '}', ',', '.', ':', ';', '!', '?',
        0x00AB, 0x2018, 0x2019, 0x201C, 0x201D, 0x2014, 0x2026,
        0x3001, 0x3002, 0x3008, 0x3009, 0x300A, 0x300B, 0x300C, 0x300D,
        0x300E, 0x300F, 0x3010, 0x3011, 0x3014, 0x3015, 0x3016, 0x3017,
        0x3018, 0x3019, 0x301A, 0x301B, 0xFF01, 0xFF08, 0xFF09, 0xFF0C,
        0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3B, 0xFF3D, 0xFF5B, 0xFF5D,
    };
    for (size_t i = 0; i < sizeof(break_chars) / sizeof(break_chars[0]); i++) {
        (void)rd_test_line_break_allowed('A', break_chars[i]);
        (void)rd_test_line_break_allowed(break_chars[i], 'A');
    }
    static const uint32_t combining[] = {
        0x0300, 0x036F, 0x1AB0, 0x1AFF, 0x1DC0, 0x1DFF,
        0x20D0, 0x20FF, 0xFE20, 0xFE2F, 0xFE00, 0xFE0F,
        0xE0100, 0xE01EF,
    };
    assert(rd_test_line_break_allowed('A', 0x1DBF) == 1);
    assert(rd_test_line_break_allowed('A', 0xFE10) == 1);
    for (size_t i = 0; i < sizeof(combining) / sizeof(combining[0]); i++) {
        assert(rd_test_line_break_allowed('A', combining[i]) == 0);
        assert(rd_test_line_break_allowed(combining[i], 'A') == 1);
    }
    assert(rd_test_line_break_allowed(0, 0) == 1);
    assert(rd_test_line_break_allowed(0, 'A') == 1);
    assert(rd_test_line_break_allowed('A', 0) == 1);

    write_file("cleanup.txt", "keep", 4);
    write_file("cleanup.txt.name", "keep-name", 9);
    write_file("../.prog/cleanup.txt.bin", "p", 1);
    write_file("../.bm/cleanup.txt.bin", "b", 1);
    write_file("../.idx/cleanup.txt.bin", "i", 1);
    write_file("../.prog/orphan.bin", "p", 1);
    write_file("../.bm/orphan.bin", "b", 1);
    write_file("../.idx/orphan.bin", "i", 1);
    write_file("orphan.txt.name", "orphan", 6);
    write_file("../.prog/settings.bin", "settings", 8);
    rd_scan_dir();
    assert(access("cleanup.txt", F_OK) == 0);
    assert(access("cleanup.txt.name", F_OK) == 0);
    assert(access("../.prog/cleanup.txt.bin", F_OK) == 0);
    assert(access("../.bm/cleanup.txt.bin", F_OK) == 0);
    assert(access("../.idx/cleanup.txt.bin", F_OK) == 0);
    assert(access("../.prog/settings.bin", F_OK) == 0);
    assert(access("../.prog/orphan.bin", F_OK) != 0);
    assert(access("../.bm/orphan.bin", F_OK) != 0);
    assert(access("../.idx/orphan.bin", F_OK) != 0);
    assert(access("orphan.txt.name", F_OK) != 0);

    assert(chdir("/") == 0);
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("reader tests passed");
    return 0;
}
