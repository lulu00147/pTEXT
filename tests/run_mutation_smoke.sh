#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMP="${TMPDIR:-/tmp}/ptext-mutation"
CC="${CC:-cc}"

fail=0

reset_tree() {
    rm -rf "$TMP"
    mkdir -p "$TMP"
    cp -a "$ROOT/app" "$TMP/app"
    cp -a "$ROOT/tests" "$TMP/tests"
}

run_mutation() {
    name="$1"
    file="$2"
    test_name="$3"
    reset_tree
    case "$name" in
    glyph-left-clip)
        sed -i '/if (x1 <= x0 || y1 <= y0) return;/d' "$TMP/$file"
        ;;
    settings-row-boundary)
        sed -i 's|rel / SETTINGS_ROW_H|(rel + SETTINGS_ROW_H / 2) / SETTINGS_ROW_H|' "$TMP/$file"
        ;;
    persist-zero-write)
        sed -i 's|if (w == 0)|if (w == 2)|' "$TMP/$file"
        ;;
    glyph-right-clip)
        sed -i '/if (x1 > dst_w) x1 = dst_w;/d' "$TMP/$file"
        ;;
    encoding-boundary)
        sed -i 's|if (cp < 0x80)|if (cp <= 0x80)|' "$TMP/$file"
        ;;
    esac
    cd "$TMP/tests"
    case "$test_name" in
    glyph)
        compile="cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. test_glyph_blit.c -o test_mut"
        ;;
    settings)
        compile="cc -std=c11 -O2 -D_GNU_SOURCE -DPTEXT_HOST_TEST -I../app/src/main/cpp -I. test_ui_settings.c ../app/src/main/cpp/ui_settings.c ../app/src/main/cpp/ui_util.c ../app/src/main/cpp/encoding.c -o test_mut"
        ;;
    encoding)
        compile="cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. test_utf8_gbk.c ../app/src/main/cpp/encoding.c -o test_mut"
        ;;
    persist)
        compile="cc -std=c11 -O2 -D_GNU_SOURCE -I../app/src/main/cpp -I. test_persist_fault.c ../app/src/main/cpp/persist.c -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=rename -Wl,--wrap=close -o test_mut"
        ;;
    esac
    if ! eval "$compile" >/dev/null 2>&1; then
        echo "$name: compile-failed"
        return
    fi
    if timeout 10 ./test_mut >/dev/null 2>&1; then
        echo "$name: SURVIVED"
        fail=1
    else
        echo "$name: killed"
    fi
}

run_mutation "glyph-left-clip" app/src/main/cpp/glyph_blit.h glyph
run_mutation "glyph-right-clip" app/src/main/cpp/glyph_blit.h glyph
run_mutation "settings-row-boundary" app/src/main/cpp/ui_settings.c settings
run_mutation "encoding-boundary" app/src/main/cpp/encoding.c encoding
run_mutation "persist-zero-write" app/src/main/cpp/persist.c persist

rm -rf "$TMP"
if [ "$fail" -ne 0 ]; then
    echo "mutation smoke failed" >&2
    exit 1
fi
echo "mutation smoke passed"
