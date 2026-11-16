#ifndef UI_LISTS_H
#define UI_LISTS_H

#include <stdint.h>
#include "reader.h"

void ui_draw_file_list(int32_t scroll, int32_t error);
void ui_draw_bookmarks(const Bookmark *bms, int32_t count, int32_t scroll);
void ui_draw_delete_dialog(const char *name);

int32_t ui_file_max_scroll(int32_t count);
int32_t ui_file_index_at(int32_t y, int32_t scroll);
int32_t ui_file_mark_left(void);
int32_t ui_file_import_hit(int32_t y);
int32_t ui_bm_max_scroll(int32_t count);
int32_t ui_bm_index_at(int32_t y, int32_t scroll);
void ui_get_delete_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b);

#endif
