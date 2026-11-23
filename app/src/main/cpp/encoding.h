#ifndef ENCODING_H
#define ENCODING_H

#include <stdint.h>

typedef uint32_t (*TextDecodeFn)(const uint8_t *data, int64_t len, int64_t *off);

uint32_t enc_decode_utf8(const uint8_t *data, int64_t len, int64_t *off);
uint32_t enc_decode_gbk(const uint8_t *data, int64_t len, int64_t *off);
int32_t enc_utf8_prefix_ok(const uint8_t *data, int64_t len);
int32_t enc_utf8_encode(uint32_t cp, char out[4]);
#ifdef PTEXT_HOST_TEST
int32_t enc_test_utf8_next_empty(void);
#endif

#endif
