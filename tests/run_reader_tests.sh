#!/bin/sh
set -eu
cd "$(dirname "$0")"
cc="${CC:-cc}"
$cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. \
    test_reader.c render_host.c ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o test_reader
./test_reader
$cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. \
    test_persist.c ../app/src/main/cpp/persist.c -o test_persist
./test_persist
rm -f test_reader test_persist
