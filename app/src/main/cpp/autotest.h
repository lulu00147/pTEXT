#ifndef AUTOTEST_H
#define AUTOTEST_H

#include <stdint.h>
#include "android_native_app_glue.h"

int32_t autotest_begin(struct android_app *app);
void autotest_draw_start(void);
int32_t autotest_after_draw(struct android_app *app);

#endif
