#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc="${CC:-cc}"
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_reader.c render_host.c ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o test_reader
./test_reader
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_glyph_blit.c -o test_glyph_blit
./test_glyph_blit
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_glyph_blit8.c -o test_glyph_blit8
./test_glyph_blit8
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_index.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c \
    -o test_index
./test_index
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_persist.c ../app/src/main/cpp/persist.c -o test_persist
./test_persist
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_persist_fault.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=rename -Wl,--wrap=close \
    -Wl,--wrap=snprintf -o test_persist_fault
./test_persist_fault
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_ui_settings.c ../app/src/main/cpp/ui_settings.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o test_ui_settings
./test_ui_settings
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_ui_lists.c ../app/src/main/cpp/ui_lists.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o test_ui_lists
./test_ui_lists
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_ui_reading.c ../app/src/main/cpp/ui_reading.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o test_ui_reading
./test_ui_reading
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -DPTEXT_MT -pthread \
    -I../app/src/main/cpp -I. test_ui_reading.c \
    ../app/src/main/cpp/ui_reading.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -Wl,--wrap=pthread_create \
    -o test_ui_reading_mt
./test_ui_reading_mt
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_reader_io_fault.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=fseek -Wl,--wrap=fread -Wl,--wrap=snprintf \
    -Wl,--wrap=open -Wl,--wrap=mmap -o test_reader_io_fault
./test_reader_io_fault
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_utf8_gbk.c ../app/src/main/cpp/encoding.c -o test_utf8_gbk
./test_utf8_gbk
rm -f test_reader test_glyph_blit test_glyph_blit8 test_index test_persist test_persist_fault \
      test_ui_settings test_ui_lists test_ui_reading test_ui_reading_mt \
      test_reader_io_fault test_utf8_gbk
