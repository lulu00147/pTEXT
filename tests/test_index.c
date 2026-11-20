#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reader.h"

#define IDX_MAGIC 0x32445850u
#define IDX_VERSION 1u
#define IDX_STEP 64
#define IDX_MAX_RECORDS 1024

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t step;
    uint32_t count;
    uint32_t layout_hash;
    uint32_t checksum;
    uint32_t reserved;
    uint32_t reserved2;
    int64_t file_size;
    int64_t file_mtime;
} TestHeader;

typedef struct {
    int64_t off;
    int32_t page_no;
    int32_t reserved;
} TestRecord;

_Static_assert(sizeof(TestHeader) == 48, "test header layout");
_Static_assert(sizeof(TestRecord) == 16, "test record layout");

static uint32_t checksum_records(const TestRecord *r, uint32_t count)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < (size_t)count * sizeof(*r); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static size_t build(uint8_t *buf, size_t cap, uint32_t count,
                    const TestRecord *records, uint32_t layout_hash,
                    int64_t file_size, int64_t file_mtime, int corrupt)
{
    size_t len = sizeof(TestHeader) + (size_t)count * sizeof(TestRecord);
    assert(len <= cap);
    memset(buf, 0, cap);
    TestHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = IDX_MAGIC;
    h.version = IDX_VERSION;
    h.step = IDX_STEP;
    h.count = count;
    h.layout_hash = layout_hash;
    h.checksum = checksum_records(records, count);
    h.file_size = file_size;
    h.file_mtime = file_mtime;
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), records, (size_t)count * sizeof(*records));
    if (corrupt && len > sizeof(h)) buf[len - 1] ^= 0x01;
    return len;
}

static void expect_ok(const uint8_t *buf, size_t len, int64_t target,
                      uint32_t layout_hash, int64_t file_size,
                      int64_t file_mtime, int64_t want_off,
                      int32_t want_page, uint32_t want_prefix)
{
    int64_t off = -1;
    int32_t page = -1;
    uint32_t prefix = 0;
    assert(rd_test_idx_parse(buf, len, file_size, file_mtime, layout_hash,
                             target, &off, &page, &prefix) == 1);
    assert(off == want_off);
    assert(page == want_page);
    assert(prefix == want_prefix);
}

static void expect_bad(const uint8_t *buf, size_t len, int64_t target,
                       uint32_t layout_hash, int64_t file_size,
                       int64_t file_mtime)
{
    int64_t off = -1;
    int32_t page = -1;
    uint32_t prefix = 0;
    assert(rd_test_idx_parse(buf, len, file_size, file_mtime, layout_hash,
                             target, &off, &page, &prefix) == 0);
}

int main(void)
{
    uint8_t buf[sizeof(TestHeader) + 8 * sizeof(TestRecord)];
    TestRecord records[8];
    const uint32_t hash = 0xA1B2C3D4u;
    const int64_t size = 100000;
    const int64_t mtime = 123456;

    memset(records, 0, sizeof(records));
    records[0].off = 0;
    records[0].page_no = 1;
    records[1].off = 1000;
    records[1].page_no = 65;
    size_t len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);

    expect_ok(buf, len, 0, hash, size, mtime, 0, 1, 1);
    expect_ok(buf, len, 999, hash, size, mtime, 0, 1, 1);
    expect_ok(buf, len, 1000, hash, size, mtime, 1000, 65, 2);
    expect_ok(buf, len, 5000, hash, size, mtime, 1000, 65, 2);
    expect_bad(buf, len, -1, hash, size, mtime);

    expect_bad(buf, len, 0, hash + 1, size, mtime);
    expect_bad(buf, len, 0, hash, size + 1, mtime);
    expect_bad(buf, len, 0, hash, size, mtime + 1);
    expect_bad(buf, len - 1, 0, hash, size, mtime);
    expect_bad(buf, len + 1, 0, hash, size, mtime);
    expect_bad(buf, 0, 0, hash, size, mtime);
    expect_bad(NULL, len, 0, hash, size, mtime);

    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 1);
    expect_bad(buf, len, 0, hash, size, mtime);
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);

    records[0].off = -1;
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);
    records[0].off = 0;

    records[1].off = 0;
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);
    records[1].off = size;
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);
    records[1].off = 1000;

    records[1].page_no = 1;
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);
    records[1].page_no = 0;
    len = build(buf, sizeof(buf), 2, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);
    records[1].page_no = 65;

    records[0].page_no = 0;
    len = build(buf, sizeof(buf), 1, records, hash, size, mtime, 0);
    expect_bad(buf, len, 0, hash, size, mtime);

    TestHeader h;
    memcpy(&h, buf, sizeof(h));
    h.magic = 0;
    memcpy(buf, &h, sizeof(h));
    expect_bad(buf, len, 0, hash, size, mtime);
    h.magic = IDX_MAGIC;
    h.version = 2;
    memcpy(buf, &h, sizeof(h));
    expect_bad(buf, len, 0, hash, size, mtime);
    h.version = IDX_VERSION;
    h.step = 1;
    memcpy(buf, &h, sizeof(h));
    expect_bad(buf, len, 0, hash, size, mtime);
    h.step = IDX_STEP;
    h.count = 0;
    memcpy(buf, &h, sizeof(h));
    expect_bad(buf, len, 0, hash, size, mtime);
    h.count = IDX_MAX_RECORDS + 1;
    memcpy(buf, &h, sizeof(h));
    expect_bad(buf, len, 0, hash, size, mtime);

    records[0].off = 500;
    records[0].page_no = 1;
    records[1].off = 1000;
    records[1].page_no = 65;
    len = build(buf, sizeof(buf), 1, records, hash, size, mtime, 0);
    expect_ok(buf, len, 500, hash, size, mtime, 500, 1, 1);
    expect_bad(buf, len, 499, hash, size, mtime);

    records[0].page_no = 1;
    len = build(buf, sizeof(buf), 1, records, hash, size, mtime, 0);

    int64_t off = -1;
    int32_t page = -1;
    assert(rd_test_idx_parse(buf, len, size, mtime, hash, 500,
                             NULL, &page, NULL) == 0);
    assert(rd_test_idx_parse(buf, len, size, mtime, hash, 500,
                             &off, NULL, NULL) == 0);

    puts("index tests passed");
    return 0;
}
