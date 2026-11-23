#!/usr/bin/env bash
# Installs the BitBuddy APK on a running Android emulator, launches it, checks it
# is still alive after 15 s (no crash) and saves a screenshot to dist/.
#
#   bash scripts/android-smoke-test.sh dist/BitBuddy-<version>-Android.apk
set -euo pipefail

apk="$1"
pkg="com.bitbuddylabs.bitbuddy"

adb install -r "$apk"
adb logcat -c
adb shell am start -W -n "$pkg/com.rmsl.juce.JuceActivity"
sleep 15

mkdir -p dist
adb exec-out screencap -p > dist/android-screenshot.png

if adb shell pidof "$pkg" > /dev/null; then
    echo "PASS: BitBuddy is running on Android"
else
    echo "FAIL: BitBuddy crashed or quit. Last log lines:"
    adb logcat -d | tail -200
    exit 1
fi
