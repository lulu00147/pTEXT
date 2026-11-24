#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang="${CLANG:-clang}"
flags="-std=c11 -O1 -g -D_GNU_SOURCE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer"
inc="-I../app/src/main/cpp -I."
seed="${TMPDIR:-/tmp}/ptext-fuzz-seed"
corpus="${TMPDIR:-/tmp}/ptext-fuzz-corpus"
rm -rf "$seed"
mkdir -p "$seed"

printf 'pTEXT fuzz seed\nsecond line\n' > "$seed/ascii"
printf '中文测试，标点。换行\n第二行。\n' > "$seed/utf8"
printf '\xd6\xd0\xce\xc4\xb2\xe2\xca\xd4\n' > "$seed/gbk"
printf '\xe4\xb8' > "$seed/truncated"
printf '\xc0\x80\xed\xa0\x80' > "$seed/invalid"
awk 'BEGIN { for (i = 0; i < 512; i++) printf "a"; printf "\n" }' > "$seed/long"
python3 - "$seed/index" <<'PY'
import struct, sys

def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h

records = [(0, 1), (1000, 65), (600000, 129)]
data = b''.join(struct.pack('<qii', off, page, 0) for off, page in records)
header = struct.pack('<8I2q', 0x32445850, 1, 64, len(records),
                     0xA1B2C3D4, fnv(data), 0, 0, 1048576, 123456)
open(sys.argv[1], 'wb').write(header + data)
PY

prepare_corpus() {
    rm -rf "$corpus"
    cp -a "$seed" "$corpus"
}
prepare_corpus

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

prepare_corpus
$clang $flags $inc fuzz_encoding.c ../app/src/main/cpp/encoding.c \
    -o /tmp/ptext_fuzz_encoding
/tmp/ptext_fuzz_encoding -runs=50000 -max_len=4096 "$corpus" \
    > /tmp/ptext_fuzz_encoding.log 2>&1
tail -20 /tmp/ptext_fuzz_encoding.log

prepare_corpus
$clang $flags $inc fuzz_glyph_blit8.c -o /tmp/ptext_fuzz_glyph8
/tmp/ptext_fuzz_glyph8 -runs=20000 -max_len=1024 "$corpus" \
    > /tmp/ptext_fuzz_glyph8.log 2>&1
tail -20 /tmp/ptext_fuzz_glyph8.log

prepare_corpus
$clang $flags $inc fuzz_glyph_blit.c -o /tmp/ptext_fuzz_glyph
/tmp/ptext_fuzz_glyph -runs=20000 -max_len=1024 "$corpus" \
    > /tmp/ptext_fuzz_glyph.log 2>&1
tail -20 /tmp/ptext_fuzz_glyph.log

prepare_corpus
$clang $flags -DPTEXT_HOST_TEST $inc fuzz_ui.c \
    ../app/src/main/cpp/ui_lists.c ../app/src/main/cpp/ui_settings.c \
    ../app/src/main/cpp/ui_reading.c ../app/src/main/cpp/ui_util.c \
    ../app/src/main/cpp/encoding.c -o /tmp/ptext_fuzz_ui
/tmp/ptext_fuzz_ui -runs=5000 -max_len=2048 "$corpus" \
    > /tmp/ptext_fuzz_ui.log 2>&1
tail -20 /tmp/ptext_fuzz_ui.log

prepare_corpus
$clang $flags $inc fuzz_persist.c ../app/src/main/cpp/persist.c \
    -o /tmp/ptext_fuzz_persist
/tmp/ptext_fuzz_persist -runs=5000 -max_len=4096 "$corpus" \
    > /tmp/ptext_fuzz_persist.log 2>&1
tail -20 /tmp/ptext_fuzz_persist.log

prepare_corpus
$clang $flags $inc fuzz_affinity.c ../app/src/main/cpp/affinity.c \
    -o /tmp/ptext_fuzz_affinity
/tmp/ptext_fuzz_affinity -runs=1000 -max_len=8 "$corpus" \
    > /tmp/ptext_fuzz_affinity.log 2>&1
tail -20 /tmp/ptext_fuzz_affinity.log

prepare_corpus
$clang $flags -DPTEXT_HOST_TEST $inc fuzz_reader_libfuzzer.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_fuzz_reader
/tmp/ptext_fuzz_reader -runs=5000 -max_len=4096 "$corpus" \
    > /tmp/ptext_fuzz_reader.log 2>&1
tail -20 /tmp/ptext_fuzz_reader.log

prepare_corpus
$clang $flags -DPTEXT_HOST_TEST $inc fuzz_index_file_libfuzzer.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_fuzz_index_file
/tmp/ptext_fuzz_index_file -runs=1000 -max_len=20000 "$corpus" \
    > /tmp/ptext_fuzz_index_file.log 2>&1
tail -20 /tmp/ptext_fuzz_index_file.log

prepare_corpus
$clang $flags -DPTEXT_HOST_TEST $inc fuzz_index_libfuzzer.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -o /tmp/ptext_fuzz_index
/tmp/ptext_fuzz_index -runs=100000 -max_len=20000 "$corpus" \
    > /tmp/ptext_fuzz_index.log 2>&1
tail -20 /tmp/ptext_fuzz_index.log

rm -f /tmp/ptext_fuzz_encoding /tmp/ptext_fuzz_reader /tmp/ptext_fuzz_index /tmp/ptext_fuzz_index_file /tmp/ptext_fuzz_glyph8 /tmp/ptext_fuzz_glyph /tmp/ptext_fuzz_ui /tmp/ptext_fuzz_persist /tmp/ptext_fuzz_affinity
rm -rf "$corpus"
