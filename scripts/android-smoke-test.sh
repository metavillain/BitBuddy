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
# A freshly booted emulator is still busy with its own start-up jobs for a while and can
# drop a launch on the floor: let it settle, and try the launch up to 3 times
sleep 20
pid=""
for attempt in 1 2 3; do
    adb shell am start -W -n "$activity"
    sleep 5
    pid="$(adb shell pidof "$pkg" | tr -d '\r' || true)"
    [ -n "$pid" ] && break
    echo "BitBuddy isn't running yet (launch attempt $attempt) - the emulator may still be starting up"
    sleep 10
done
echo "BitBuddy pid after launch: ${pid:-none}"
sleep 10   # then it must still be running (a crash or quit fails the test below)

mkdir -p dist
adb exec-out screencap -p > dist/android-screenshot.png

if adb shell pidof "$pkg" > /dev/null; then
    echo "PASS: BitBuddy is running on Android"

    # Phone trick: shake the (emulated) phone -> BitBuddy rolls a new random sound
    if adb emu sensor set acceleration 0:9.81:0 2>&1 | grep -q "KO"; then
        echo "SKIP: this emulator can't fake the motion sensor"
    else
        adb logcat -c
        for i in 1 2 3 4 5 6; do
            adb emu sensor set acceleration 30:0:9.81 > /dev/null
            sleep 0.2
            adb emu sensor set acceleration -30:0:9.81 > /dev/null
            sleep 0.2
        done
        adb emu sensor set acceleration 0:9.81:0 > /dev/null
        sleep 2
        if adb logcat -d | grep -q "BitBuddy: shake"; then
            echo "PASS: shaking the phone rolled a new random sound"
        else
            echo "FAIL: shaking the phone did nothing"
            adb logcat -d | grep -iE "bitbuddy|JUCE|sensor" | tail -40 || true
            exit 1
        fi
    fi
else
    echo "FAIL: BitBuddy crashed or quit."
    echo "--- everything BitBuddy logged (pid ${pid:-?}), plus why Android closed it:"
    adb logcat -d -v threadtime \
        | grep -E "(^.{18} +${pid:-NOPID} )|ActivityTaskManager|ActivityManager|WindowManager|AndroidRuntime|DEBUG|libc|JUCE|bitbuddy" \
        | grep -vE "ModernMediaScanner|CarrierSvc" | tail -200 || true
    exit 1
fi
