#ifndef UI_UTIL_H
#define UI_UTIL_H

#include <stdint.h>

int32_t ui_px(int32_t px);
int32_t ui_utf8_to_utf32(uint32_t *dst, int32_t max, const char *src);

#endif
