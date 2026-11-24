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

# Ask Android which activity launches the app (its class name differs between JUCE versions)
activity="$(adb shell cmd package resolve-activity --brief -c android.intent.category.LAUNCHER "$pkg" | tr -d '\r' | tail -1)"
echo "Launching $activity"
case "$activity" in
    */*) ;;
    *) echo "FAIL: no launcher activity found for $pkg"; exit 1 ;;
esac
adb shell am start -W -n "$activity"
sleep 2
pid="$(adb shell pidof "$pkg" | tr -d '\r' || true)"
echo "BitBuddy pid after launch: ${pid:-none}"
sleep 13

mkdir -p dist
adb exec-out screencap -p > dist/android-screenshot.png

if adb shell pidof "$pkg" > /dev/null; then
    echo "PASS: BitBuddy is running on Android"
else
    echo "FAIL: BitBuddy crashed or quit."
    echo "--- everything BitBuddy logged (pid ${pid:-?}), plus why Android closed it:"
    adb logcat -d -v threadtime \
        | grep -E "(^.{18} +${pid:-NOPID} )|ActivityTaskManager|ActivityManager|WindowManager|AndroidRuntime|DEBUG|libc|JUCE|bitbuddy" \
        | grep -vE "ModernMediaScanner|CarrierSvc" | tail -200 || true
    exit 1
fi
