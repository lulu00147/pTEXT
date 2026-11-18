#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang="${CLANG:-clang}"
flags="-std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer"
inc="-I../app/src/main/cpp -I."
corpus="${TMPDIR:-/tmp}/ptext-fuzz-corpus"
rm -rf "$corpus"
mkdir -p "$corpus"

printf 'pTEXT fuzz seed\nsecond line\n' > "$corpus/ascii"
printf '中文测试，标点。换行\n第二行。\n' > "$corpus/utf8"
printf '\xd6\xd0\xce\xc4\xb2\xe2\xca\xd4\n' > "$corpus/gbk"
printf '\xe4\xb8' > "$corpus/truncated"
printf '\xc0\x80\xed\xa0\x80' > "$corpus/invalid"
awk 'BEGIN { for (i = 0; i < 512; i++) printf "a"; printf "\n" }' > "$corpus/long"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

$clang $flags $inc fuzz_encoding.c ../app/src/main/cpp/encoding.c \
    -o /tmp/ptext_fuzz_encoding
/tmp/ptext_fuzz_encoding -runs=50000 -max_len=4096 "$corpus" \
    > /tmp/ptext_fuzz_encoding.log 2>&1
tail -20 /tmp/ptext_fuzz_encoding.log

$clang $flags -DPTEXT_HOST_TEST $inc fuzz_reader_libfuzzer.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_fuzz_reader
/tmp/ptext_fuzz_reader -runs=5000 -max_len=4096 "$corpus" \
    > /tmp/ptext_fuzz_reader.log 2>&1
tail -20 /tmp/ptext_fuzz_reader.log

rm -f /tmp/ptext_fuzz_encoding /tmp/ptext_fuzz_reader
rm -rf "$corpus"
