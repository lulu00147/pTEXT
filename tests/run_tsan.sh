#!/bin/sh
set -eu
cd "$(dirname "$0")"

CC="${CC:-clang}"
OUT="${TMPDIR:-/tmp}/ptext-tsan-ui"

$CC -std=c11 -O1 -g -D_GNU_SOURCE -DPTEXT_HOST_TEST -DPTEXT_MT -pthread \
    -fsanitize=thread -I../app/src/main/cpp -I. \
    test_ui_reading.c ../app/src/main/cpp/ui_reading.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c \
    -Wl,--wrap=pthread_create -o "$OUT"
TSAN_OPTIONS=halt_on_error=1 "$OUT"
rm -f "$OUT"
