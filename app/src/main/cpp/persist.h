#ifndef PERSIST_H
#define PERSIST_H

#include <stddef.h>
#include <stdint.h>

int32_t persist_write_atomic(const char *path, const void *buf, size_t len);

#endif
