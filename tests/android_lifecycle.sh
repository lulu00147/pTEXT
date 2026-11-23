#!/bin/sh
set -eu
# Requires an installed APK and one adb device.
adb="${ADB:-adb}"
serial="${ANDROID_SERIAL:-}"
if [ -n "$serial" ]; then
    adb="$adb -s $serial"
fi

start_app() {
    $adb shell am start -n com.ptext/.MainActivity >/dev/null
    sleep 2
}

assert_focus() {
    focus=$($adb shell dumpsys window 2>/dev/null |
        sed -n 's/.*mCurrentFocus=Window{[^ ]* [^ ]* \([^ ]*\).*/\1/p' | head -1)
    case "$focus" in
        *com.ptext*MainActivity*) ;;
        *) echo "android lifecycle test failed: focus=$focus" >&2; exit 1 ;;
    esac
}

$adb shell am force-stop com.ptext
start_app

# Back finishes the Activity, then a fresh launch must not stick on splash.
$adb shell input keyevent 4
sleep 1
start_app
assert_focus

# Home keeps the Activity; resume must still draw.
$adb shell input keyevent 3
sleep 1
start_app
assert_focus

# Configuration change while paused.
$adb shell settings put system accelerometer_rotation 0
$adb shell settings put system user_rotation 1
sleep 1
assert_focus
$adb shell settings put system user_rotation 0

# Process death and recreation.
$adb shell am force-stop com.ptext
start_app
assert_focus

echo "android lifecycle test passed"
