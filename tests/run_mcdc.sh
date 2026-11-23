#!/bin/sh
set -eu
cd "$(dirname "$0")"

CC="${CC:-clang}"
PROFDATA="${LLVM_PROFDATA:-llvm-profdata-19}"
LLVMCOV="${LLVM_COV:-llvm-cov-19}"
OUT="${MCDC_OUT:-${TMPDIR:-/tmp}/ptext-mcdc}"
FLAGS="-std=c11 -O0 -g -D_GNU_SOURCE -DPTEXT_HOST_TEST -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc -I../app/src/main/cpp -I."

rm -rf "$OUT"
mkdir -p "$OUT"
export LLVM_PROFILE_FILE="$OUT/%p.profraw"

$CC $FLAGS test_reader.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c -o "$OUT/test_reader"
"$OUT/test_reader"

$CC $FLAGS test_utf8_gbk.c ../app/src/main/cpp/encoding.c -o "$OUT/test_utf8"
"$OUT/test_utf8"

$CC $FLAGS test_index.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c -o "$OUT/test_index"
"$OUT/test_index"

$CC $FLAGS test_index_fault.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=open -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=close \
    -Wl,--wrap=rename -o "$OUT/test_index_fault"
"$OUT/test_index_fault"

$CC $FLAGS test_index_crash.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=open -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=close \
    -Wl,--wrap=rename -o "$OUT/test_index_crash"
"$OUT/test_index_crash"

$CC $FLAGS fuzz_reader.c render_host.c ../app/src/main/cpp/reader.c \
    ../app/src/main/cpp/encoding.c ../app/src/main/cpp/persist.c -o "$OUT/fuzz_reader"
"$OUT/fuzz_reader"

$CC $FLAGS test_glyph_blit.c -o "$OUT/test_glyph"
"$OUT/test_glyph"

$CC $FLAGS test_glyph_blit8.c -o "$OUT/test_glyph8"
"$OUT/test_glyph8"

$CC $FLAGS test_persist.c ../app/src/main/cpp/persist.c -o "$OUT/test_persist"
"$OUT/test_persist"

$CC $FLAGS test_persist_fault.c ../app/src/main/cpp/persist.c \
    -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=rename -Wl,--wrap=close \
    -Wl,--wrap=snprintf -o "$OUT/test_persist_fault"
"$OUT/test_persist_fault"

$CC $FLAGS test_ui_settings.c ../app/src/main/cpp/ui_settings.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o "$OUT/test_ui_settings"
"$OUT/test_ui_settings"

$CC $FLAGS test_ui_lists.c ../app/src/main/cpp/ui_lists.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o "$OUT/test_ui_lists"
"$OUT/test_ui_lists"

$CC $FLAGS test_ui_reading.c ../app/src/main/cpp/ui_reading.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o "$OUT/test_ui_reading"
"$OUT/test_ui_reading"

$CC $FLAGS -DPTEXT_MT -pthread test_ui_reading.c ../app/src/main/cpp/ui_reading.c \
    ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c \
    -Wl,--wrap=pthread_create -o "$OUT/test_ui_reading_mt"
"$OUT/test_ui_reading_mt"

$CC $FLAGS test_reader_io_fault.c render_host.c \
    ../app/src/main/cpp/reader.c ../app/src/main/cpp/encoding.c \
    ../app/src/main/cpp/persist.c -Wl,--wrap=fseek -Wl,--wrap=fread \
    -Wl,--wrap=snprintf -Wl,--wrap=open -Wl,--wrap=mmap \
    -o "$OUT/test_reader_io_fault"
"$OUT/test_reader_io_fault"

"$PROFDATA" merge -sparse "$OUT"/*.profraw -o "$OUT/all.profdata"
REPORT="$OUT/mcdc.txt"
"$LLVMCOV" report \
    -object "$OUT/test_reader" -object "$OUT/test_utf8" \
    -object "$OUT/test_glyph" -object "$OUT/test_glyph8" -object "$OUT/test_persist" \
    -object "$OUT/test_index" -object "$OUT/test_index_fault" \
    -object "$OUT/test_index_crash" -object "$OUT/fuzz_reader" \
    -object "$OUT/test_persist_fault" -object "$OUT/test_ui_settings" \
    -object "$OUT/test_ui_lists" -object "$OUT/test_ui_reading" \
    -object "$OUT/test_ui_reading_mt" -object "$OUT/test_reader_io_fault" \
    -instr-profile "$OUT/all.profdata" --show-mcdc-summary | tee "$REPORT"

check_mcdc() {
    file="$1"
    minimum="$2"
    value=$(awk -v f="$file" '$1 == f { v=$NF; sub("%", "", v); print v; exit }' "$REPORT")
    if [ -z "$value" ]; then
        echo "missing MC/DC data: $file" >&2
        exit 1
    fi
    awk -v f="$file" -v v="$value" -v m="$minimum" \
        'BEGIN { if (v + 0 < m + 0) { printf "%s MC/DC %.2f%% < %.2f%%\n", f, v, m; exit 1 } }'
}

check_mcdc app/src/main/cpp/reader.c 100
check_mcdc app/src/main/cpp/encoding.c 100
check_mcdc app/src/main/cpp/persist.c 100
check_mcdc app/src/main/cpp/glyph_blit.h 100
check_mcdc app/src/main/cpp/glyph_blit8.h 100
check_mcdc app/src/main/cpp/ui_lists.c 100
check_mcdc app/src/main/cpp/ui_reading.c 100
check_mcdc app/src/main/cpp/ui_settings.c 100
check_mcdc app/src/main/cpp/ui_util.c 100
