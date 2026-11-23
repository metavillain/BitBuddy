#!/usr/bin/env bash
# Builds a self-contained zip for Mac users (Apple Silicon + Intel, macOS 10.13+).
#
#   bash scripts/make-share-package-mac.sh
#
# Output: dist/BitBuddy-<version>-macOS.zip
# Needs a Release build first (build/BitBuddy_artefacts/Release/...), run on a Mac.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="$(sed -n 's/^project(BitBuddy VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
name="BitBuddy-$version-macOS"
rel="$root/build/BitBuddy_artefacts/Release"
pkg="$root/dist/$name"

rm -rf "$pkg" "$root/dist/$name.zip"
mkdir -p "$pkg/Demo" "$pkg/Source"

# --- app + plugins ------------------------------------------------------------
cp -R "$rel/Standalone/BitBuddy.app" "$rel/AU/BitBuddy.component" "$rel/VST3/BitBuddy.vst3" "$pkg/"

# Ad-hoc signature: Apple Silicon Macs refuse to run completely unsigned code.
# (A paid Apple Developer ID + notarization would remove the first-run warning.)
for bundle in "$pkg/BitBuddy.app" "$pkg/BitBuddy.component" "$pkg/BitBuddy.vst3"; do
    codesign --force --deep --sign - "$bundle"
done

# --- demo, guide, source, licenses -----------------------------------------------
cp -R demo/. "$pkg/Demo/"
cp docs/BEGINNERS-GUIDE.md "$pkg/Beginners-Guide.md"
# (the demo WAV is already in Demo/, so leave it out of the source copy)
git archive --format=zip -o "$pkg/Source/BitBuddy-$version-source.zip" HEAD -- . ':(exclude)demo/*.wav'

{
    echo "BitBuddy $version is free software: GNU Affero General Public License v3 (see LICENSE)."
    echo "Built with JUCE (https://juce.com), used under the AGPLv3."
    echo "Complete source code is in the Source folder."
    echo
    echo "Pixel font: \"Press Start 2P\" by CodeMan38, SIL Open Font License 1.1:"
    cat assets/PressStart2P-OFL.txt
    echo
    echo "VST is a trademark of Steinberg Media Technologies GmbH."
    echo "Audio Units, Logic Pro and GarageBand are trademarks of Apple Inc."
} > "$pkg/LICENSES.txt"
cp LICENSE "$pkg/LICENSE"

# --- installer ----------------------------------------------------------------------
cat > "$pkg/Install BitBuddy.command" <<'EOF'
#!/bin/bash
# Installs BitBuddy for your user account (no admin password needed).
cd "$(dirname "$0")"

AU="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"
mkdir -p "$AU" "$VST3" "$APPS"

rm -rf "$AU/BitBuddy.component" "$VST3/BitBuddy.vst3" "$APPS/BitBuddy.app"
cp -R BitBuddy.component "$AU/"
cp -R BitBuddy.vst3 "$VST3/"
cp -R BitBuddy.app "$APPS/"

# Clear the "downloaded from the internet" flag so macOS lets BitBuddy run
xattr -dr com.apple.quarantine "$AU/BitBuddy.component" "$VST3/BitBuddy.vst3" "$APPS/BitBuddy.app" 2>/dev/null

# Make GarageBand / Logic notice the new Audio Unit
killall -9 AudioComponentRegistrar 2>/dev/null

echo
echo "  BitBuddy is installed!"
echo
echo "  App:     ~/Applications/BitBuddy.app"
echo "  Plugins: ~/Library/Audio/Plug-Ins (Audio Unit + VST3)"
echo
echo "  Open GarageBand, Logic or your DAW and look for BitBuddy under instruments."
echo
open "$APPS/BitBuddy.app"
EOF
chmod +x "$pkg/Install BitBuddy.command"

# --- read me ------------------------------------------------------------------------
cat > "$pkg/READ ME FIRST.txt" <<EOF
BitBuddy $version for Mac - an easy, fun chiptune / 8-bit synth
=============================================================

Works on Apple Silicon and Intel Macs, macOS 10.13 or newer.


1) INSTALL (one double-click)
-----------------------------
- Double-click "Install BitBuddy.command".
- macOS will probably say it "can't be verified" or "cannot be opened",
  because BitBuddy isn't from the App Store or a paid Apple developer. To allow it:
    macOS 15 (Sequoia) and newer:
        click "Done", open System Settings > Privacy & Security, scroll down,
        click "Open Anyway" next to "Install BitBuddy.command", then confirm.
    macOS 14 and older:
        right-click (or Control-click) the file > Open > Open.
- A Terminal window shows "BitBuddy is installed!" and the app opens.

  Prefer Terminal? This does the same, then double-click the installer:
      xattr -dr com.apple.quarantine ~/Downloads/$name


2) JUST PLAY
------------
- Open BitBuddy from your Applications folder (the one in your home folder).
- Pick your speakers or headphones under "Options" (top-left).
- Play with the mouse, or your computer keys:
      A W S E D F T G Y H U J K  = one octave      Z / X = octave down / up
  Bottom-right says KEYS ON when your keyboard is playing BitBuddy.


3) USE IT IN GARAGEBAND, LOGIC OR ANOTHER DAW
---------------------------------------------
- GarageBand / Logic: make a Software Instrument track, click the instrument
  slot > AU Instruments > BitBuddy Labs > BitBuddy.
- Ableton, REAPER, Bitwig, FL Studio ...: re-scan plugins, then add BitBuddy
  (VST3 or AU).


4) HAVE FUN
-----------
- Try the presets at the top, or press SURPRISE ME! for random sounds.
- Hover over any knob - the bar at the bottom explains what it does.
- Instant 8-bit chords: set section 3 to CHORD and hold just ONE key.
- Drum Kit preset: every C is a kick, D a snare, F# a hi-hat.
- Demo/ has a song made with BitBuddy, plus each part as a MIDI file to remix.
- Beginners-Guide.md explains chiptune from scratch.
EOF

# --- zip (ditto keeps the app/plugin bundles intact) --------------------------------
cd "$root/dist"
ditto -c -k --sequesterRsrc --keepParent "$name" "$name.zip"
echo "Created dist/$name.zip ($(du -h "$name.zip" | cut -f1))"
