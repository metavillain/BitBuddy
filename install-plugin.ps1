# Installs BitBuddy so every DAW can find it.
#
#   Right-click this file -> "Run with PowerShell"
#   (Windows will ask for admin permission - click Yes.)
#
# It copies the built plugin into the standard system folder:
#   C:\Program Files\Common Files\VST3\BitBuddy.vst3
# and also into your personal plugin folder (no admin needed):
#   %LOCALAPPDATA%\Programs\Common\VST3\BitBuddy.vst3

$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'build\BitBuddy_artefacts\Release\VST3\BitBuddy.vst3'
if (-not (Test-Path $src)) { throw "Build the plugin first: $src not found" }

# Per-user folder (no admin)
$userDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
New-Item -ItemType Directory -Force $userDir | Out-Null
if (Test-Path "$userDir\BitBuddy.vst3") { Remove-Item -Recurse -Force "$userDir\BitBuddy.vst3" }
Copy-Item -Recurse -Force $src "$userDir\"
Write-Host "Installed for you:      $userDir\BitBuddy.vst3"

# System folder (admin)
$sysDir = 'C:\Program Files\Common Files\VST3'
$cmd = "New-Item -ItemType Directory -Force '$sysDir' | Out-Null; " +
       "if (Test-Path '$sysDir\BitBuddy.vst3') { Remove-Item -Recurse -Force '$sysDir\BitBuddy.vst3' }; " +
       "Copy-Item -Recurse -Force '$src' '$sysDir\'"
try {
    Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile', '-Command', $cmd -Wait
    Write-Host "Installed system-wide: $sysDir\BitBuddy.vst3"
} catch {
    Write-Host "System-wide install skipped (admin permission was not given)."
}

Write-Host "`nNow open your DAW and re-scan plugins. Look for 'BitBuddy' under instruments."
