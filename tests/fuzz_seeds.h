#ifndef FUZZ_SEEDS_H
#define FUZZ_SEEDS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t *data;
    size_t len;
} FuzzSeed;

static const uint8_t seed_ascii[] = {'p', 'T', 'E', 'X', 'T', '\n'};
static const uint8_t seed_utf8[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87, 0};
static const uint8_t seed_overlong[] = {0xC0, 0x80, 0xE0, 0x80, 0x80, 0};
static const uint8_t seed_surrogate[] = {0xED, 0xA0, 0x80, 0};
static const uint8_t seed_truncated[] = {0xE4, 0xB8, 0};
static const uint8_t seed_bad_cont[] = {0xE4, 0x41, 0};
static const uint8_t seed_gbk_ascii[] = {'G', 'B', 'K', '\n'};
static const uint8_t seed_gbk_pair[] = {0xD6, 0xD0, 0xCE, 0xC4, 0};
static const uint8_t seed_gbk_euro[] = {0x80, 0x41, 0};
static const uint8_t seed_gbk_bad[] = {0x81, 0x7F, 0xFF, 0};

static const FuzzSeed fuzz_seeds[] = {
    {seed_ascii, sizeof(seed_ascii) - 1},
    {seed_utf8, sizeof(seed_utf8) - 1},
    {seed_overlong, sizeof(seed_overlong) - 1},
    {seed_surrogate, sizeof(seed_surrogate) - 1},
    {seed_truncated, sizeof(seed_truncated) - 1},
    {seed_bad_cont, sizeof(seed_bad_cont) - 1},
    {seed_gbk_ascii, sizeof(seed_gbk_ascii) - 1},
    {seed_gbk_pair, sizeof(seed_gbk_pair) - 1},
    {seed_gbk_euro, sizeof(seed_gbk_euro) - 1},
    {seed_gbk_bad, sizeof(seed_gbk_bad) - 1},
};

#endif
