#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <stdint.h>

void ui_get_settings_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b);
int32_t ui_settings_contains(int32_t x, int32_t y);
int32_t ui_settings_row_at(int32_t y);
int32_t ui_settings_button_at(int32_t x);
void ui_draw_settings(void);

#endif
