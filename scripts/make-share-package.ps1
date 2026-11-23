# Builds a self-contained zip you can send to a friend (Windows 10/11, 64-bit).
#
#   powershell -ExecutionPolicy Bypass -File scripts\make-share-package.ps1
#
# Output: dist\BitBuddy-<version>-Windows.zip
# Needs a Release build first, and commits the source snapshot from git HEAD.

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$version = ([regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(BitBuddy VERSION ([0-9.]+)')).Groups[1].Value
$name    = "BitBuddy-$version-Windows"
$dist    = Join-Path $root 'dist'
$pkg     = Join-Path $dist $name
$rel     = Join-Path $root 'build\BitBuddy_artefacts\Release'

if (-not (Test-Path "$rel\VST3\BitBuddy.vst3")) { throw "Build Release first (VST3 not found)." }
if (Test-Path $pkg) { Remove-Item -LiteralPath $pkg -Recurse -Force }
New-Item -ItemType Directory -Force $pkg, "$pkg\Demo", "$pkg\Source" | Out-Null

# --- binaries ---------------------------------------------------------------
Copy-Item -Recurse "$rel\VST3\BitBuddy.vst3" $pkg
Copy-Item "$rel\Standalone\BitBuddy.exe" $pkg

# --- demo, guide, source -----------------------------------------------------
Copy-Item -Recurse "$root\demo\*" "$pkg\Demo"
Copy-Item "$root\docs\BEGINNERS-GUIDE.md" "$pkg\Beginners-Guide.md"
git archive --format=zip -o "$pkg\Source\BitBuddy-$version-source.zip" HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }

# --- installer -----------------------------------------------------------------
@'
# Installs BitBuddy.vst3 (from this folder) where every DAW can find it.
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'BitBuddy.vst3'
$binary = 'Contents\x86_64-win\BitBuddy.vst3'

$userDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
New-Item -ItemType Directory -Force $userDir | Out-Null
if (Test-Path "$userDir\BitBuddy.vst3") { Remove-Item -Recurse -Force "$userDir\BitBuddy.vst3" }
Copy-Item -Recurse -Force $src "$userDir\"
Get-ChildItem -Recurse "$userDir\BitBuddy.vst3" -File | Unblock-File
Write-Host "Installed for you:      $userDir\BitBuddy.vst3"

$sysDir = 'C:\Program Files\Common Files\VST3'
$cmd = "New-Item -ItemType Directory -Force '$sysDir' | Out-Null; " +
       "if (Test-Path '$sysDir\BitBuddy.vst3') { Remove-Item -Recurse -Force '$sysDir\BitBuddy.vst3' }; " +
       "Copy-Item -Recurse -Force '$src' '$sysDir\'; " +
       "Get-ChildItem -Recurse '$sysDir\BitBuddy.vst3' -File | Unblock-File"
try {
    Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile', '-Command', $cmd -Wait
    $installed = Join-Path "$sysDir\BitBuddy.vst3" $binary
    if ((Test-Path $installed) -and (Get-FileHash $installed).Hash -eq (Get-FileHash (Join-Path $src $binary)).Hash) {
        Write-Host "Installed system-wide: $sysDir\BitBuddy.vst3"
    } else {
        Write-Host "System-wide copy did not complete - run Install BitBuddy.bat again."
    }
} catch {
    Write-Host "System-wide install skipped (permission not given). Most DAWs still find the personal copy."
}
Write-Host "`nDone! Open your music software, re-scan plugins, and look for 'BitBuddy' under instruments."
'@ | Set-Content -Encoding utf8 "$pkg\install.ps1"

@"
@echo off
title Install BitBuddy
echo Installing BitBuddy... (Windows will ask for permission - click Yes)
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
"@ | Set-Content -Encoding ascii "$pkg\Install BitBuddy.bat"

# --- read me ---------------------------------------------------------------------
@"
BitBuddy $version - an easy, fun chiptune / 8-bit synth
=======================================================

Needs: Windows 10 or 11 (64-bit). Mac user? Get the macOS zip from the same download page.

TIP BEFORE YOU START: right-click the .zip > Properties > tick "Unblock" > OK,
then extract it. That avoids most Windows security pop-ups.


1) JUST PLAY - no music software needed
---------------------------------------
- Double-click BitBuddy.exe
- If Windows says "Windows protected your PC": click "More info" > "Run anyway".
  (Windows says that about any app that isn't from a big company.)
- Click "Options" (top-left) to pick your speakers or headphones.
- Play with the mouse, or your computer keys:
      A W S E D F T G Y H U J K  = one octave      Z / X = octave down / up
  Bottom-right says KEYS ON when your keyboard is playing BitBuddy.


2) USE IT IN MUSIC SOFTWARE (REAPER, FL Studio, Ableton, Cakewalk, Bitwig ...)
------------------------------------------------------------------------------
- Double-click "Install BitBuddy.bat" and click Yes when Windows asks.
- Open your music software, re-scan plugins, add "BitBuddy" as an instrument.


3) HAVE FUN
-----------
- Try the presets at the top, or press SURPRISE ME! for random sounds.
- Hover over any knob - the bar at the bottom explains what it does.
- Instant 8-bit chords: set section 3 to CHORD and hold just ONE key.
- Drum Kit preset: every C is a kick, D a snare, F# a hi-hat.
- Demo\ has a song made with BitBuddy, plus each part as a MIDI file to remix.
- Beginners-Guide.md (open with Notepad) explains chiptune from scratch.


What's in this folder
---------------------
  BitBuddy.exe            the app (plays on its own)
  BitBuddy.vst3           the plugin for music software
  Install BitBuddy.bat    copies the plugin where music software finds it
  Demo\                   demo song (WAV) + MIDI parts
  Beginners-Guide.md      how to make 8-bit music
  Source\                 BitBuddy's source code
  LICENSES.txt            licenses
"@ | Set-Content -Encoding utf8 "$pkg\READ ME FIRST.txt"

Copy-Item "$root\LICENSE" "$pkg\LICENSE.txt"
@"
BitBuddy $version is free software: GNU Affero General Public License v3 (see LICENSE.txt).
Built with JUCE (https://juce.com), used under the AGPLv3. BitBuddy's complete
source code is included in the Source folder.

Pixel font: "Press Start 2P" by CodeMan38, SIL Open Font License 1.1:
$(Get-Content "$root\assets\PressStart2P-OFL.txt" -Raw)

VST is a trademark of Steinberg Media Technologies GmbH.
"@ | Set-Content -Encoding utf8 "$pkg\LICENSES.txt"

# --- zip ----------------------------------------------------------------------------
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path "$pkg\*" -DestinationPath $zip
Write-Host "Created $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
