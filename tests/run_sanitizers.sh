#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc="${CC:-cc}"
flags="-std=c11 -O1 -g -D_GNU_SOURCE -DPTEXT_HOST_TEST -fsanitize=address,undefined,leak -fno-omit-frame-pointer"
inc="-I../app/src/main/cpp -I."
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

$cc $flags $inc test_reader.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_test_reader_asan
/tmp/ptext_test_reader_asan

$cc $flags $inc test_glyph_blit.c -o /tmp/ptext_test_glyph_asan
/tmp/ptext_test_glyph_asan

$cc $flags $inc test_index.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_test_index_asan
/tmp/ptext_test_index_asan

$cc $flags $inc test_persist.c ../app/src/main/cpp/persist.c \
    -o /tmp/ptext_test_persist_asan
/tmp/ptext_test_persist_asan

$cc $flags $inc test_persist_fault.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=rename -Wl,--wrap=close \
    -Wl,--wrap=snprintf -o /tmp/ptext_test_persist_fault_asan
/tmp/ptext_test_persist_fault_asan

$cc $flags $inc fuzz_reader.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_fuzz_reader_asan
/tmp/ptext_fuzz_reader_asan

$cc $flags $inc -DPTEXT_HOST_TEST test_ui_settings.c \
    ../app/src/main/cpp/ui_settings.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -o /tmp/ptext_test_ui_asan
/tmp/ptext_test_ui_asan

$cc $flags $inc -DPTEXT_HOST_TEST test_ui_lists.c \
    ../app/src/main/cpp/ui_lists.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -o /tmp/ptext_test_ui_lists_asan
/tmp/ptext_test_ui_lists_asan

$cc $flags $inc -DPTEXT_HOST_TEST test_ui_reading.c \
    ../app/src/main/cpp/ui_reading.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -o /tmp/ptext_test_ui_reading_asan
/tmp/ptext_test_ui_reading_asan

$cc $flags $inc -DPTEXT_HOST_TEST -DPTEXT_MT -pthread test_ui_reading.c \
    ../app/src/main/cpp/ui_reading.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -Wl,--wrap=pthread_create \
    -o /tmp/ptext_test_ui_reading_mt_asan
/tmp/ptext_test_ui_reading_mt_asan

$cc $flags $inc -DPTEXT_HOST_TEST test_reader_io_fault.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -Wl,--wrap=fseek -Wl,--wrap=fread \
    -Wl,--wrap=snprintf -Wl,--wrap=open -Wl,--wrap=mmap \
    -o /tmp/ptext_test_reader_io_fault_asan
/tmp/ptext_test_reader_io_fault_asan

$cc $flags $inc test_utf8_gbk.c ../app/src/main/cpp/encoding.c \
    -o /tmp/ptext_test_utf8_asan
/tmp/ptext_test_utf8_asan

rm -f /tmp/ptext_test_reader_asan /tmp/ptext_test_glyph_asan \
      /tmp/ptext_test_index_asan /tmp/ptext_test_persist_asan \
      /tmp/ptext_test_persist_fault_asan /tmp/ptext_test_ui_asan \
      /tmp/ptext_test_ui_lists_asan /tmp/ptext_test_ui_reading_asan \
      /tmp/ptext_test_ui_reading_mt_asan /tmp/ptext_test_reader_io_fault_asan \
      /tmp/ptext_fuzz_reader_asan /tmp/ptext_test_utf8_asan
