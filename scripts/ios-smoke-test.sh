#!/usr/bin/env bash
# Boots an iPad simulator, installs the BitBuddy app built for the simulator,
# launches it, checks it is still alive after 15 s and saves a screenshot to dist/.
#
#   bash scripts/ios-smoke-test.sh        (after building into build-ios/)
set -euo pipefail

bundle_id="com.bitbuddylabs.bitbuddy"
# JUCE puts it in build-ios/BitBuddy_artefacts/<config>/Standalone/ (the AUv3 lives inside it)
app="$(find build-ios -path '*Standalone*' -name 'BitBuddy.app' -type d -not -path '*/PlugIns/*' | head -1)"
[ -n "$app" ] || { echo "FAIL: no simulator BitBuddy.app found in build-ios"; exit 1; }
echo "App: $app"

# Pick an iPad and the newest iOS runtime installed with this Xcode
devtype="$(xcrun simctl list devicetypes -j | python3 -c 'import json,sys; print(next(t["identifier"] for t in json.load(sys.stdin)["devicetypes"] if "iPad" in t["name"]))')"
runtime="$(xcrun simctl list runtimes -j | python3 -c 'import json,sys; r=[x for x in json.load(sys.stdin)["runtimes"] if x["platform"]=="iOS" and x["isAvailable"]]; print(r[-1]["identifier"])')"
echo "Simulator: $devtype / $runtime"

udid="$(xcrun simctl create bitbuddy-test "$devtype" "$runtime")"
xcrun simctl boot "$udid"
xcrun simctl bootstatus "$udid" -b
xcrun simctl install "$udid" "$app"
xcrun simctl launch "$udid" "$bundle_id"
sleep 15

mkdir -p dist
xcrun simctl io "$udid" screenshot dist/ios-screenshot.png

if xcrun simctl spawn "$udid" launchctl list | grep -q "$bundle_id"; then
    echo "PASS: BitBuddy is running on the iPad simulator"
else
    echo "FAIL: BitBuddy crashed or quit"
    exit 1
fi
