#!/bin/sh
set -eu
# Requires an installed APK and one adb device.
adb="${ADB:-adb}"
serial="${ANDROID_SERIAL:-}"
if [ -n "$serial" ]; then
    adb="$adb -s $serial"
fi

$adb shell am force-stop com.ptext
$adb shell am start -n com.ptext/.MainActivity >/dev/null
sleep 2
$adb shell input keyevent 4
sleep 1
$adb shell am start -n com.ptext/.MainActivity >/dev/null
sleep 2

focus=$($adb shell dumpsys window 2>/dev/null | sed -n 's/.*mCurrentFocus=Window{[^ ]* [^ ]* \([^ ]*\).*/\1/p' | head -1)
case "$focus" in
    *com.ptext*MainActivity*) echo "android lifecycle test passed" ;;
    *) echo "android lifecycle test failed: focus=$focus" >&2; exit 1 ;;
esac
