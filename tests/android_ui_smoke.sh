#!/bin/sh
set -eu
# Requires an installed APK, one file in the app, and one adb device.
adb="${ADB:-adb}"
serial="${ANDROID_SERIAL:-}"
if [ -n "$serial" ]; then
    adb="$adb -s $serial"
fi

$adb shell am force-stop com.ptext
$adb shell am start -n com.ptext/.MainActivity >/dev/null
sleep 2

size=$($adb shell wm size | sed -n 's/.*: \([0-9]*\)x\([0-9]*\).*/\1 \2/p')
w=$(printf '%s\n' "$size" | awk '{print $1}')
h=$(printf '%s\n' "$size" | awk '{print $2}')
[ -n "$w" ] && [ -n "$h" ]

# First file row, then center long-press for settings.
$adb shell input tap $((w / 5)) $((h * 15 / 100))
sleep 2
$adb shell input swipe $((w / 2)) $((h / 2)) $((w / 2)) $((h / 2)) 3500
sleep 1

# Settings -> reading -> file list -> launcher.
$adb shell input keyevent 4
sleep 1
$adb shell input keyevent 4
sleep 1
$adb shell input keyevent 4
sleep 1

focus=$($adb shell dumpsys window 2>/dev/null |
    sed -n 's/.*mCurrentFocus=Window{[^ ]* [^ ]* \([^ ]*\).*/\1/p' | head -1)
case "$focus" in
    *com.android.launcher*|*launcher*) echo "android ui smoke test passed" ;;
    *) echo "android ui smoke test failed: focus=$focus" >&2; exit 1 ;;
esac
