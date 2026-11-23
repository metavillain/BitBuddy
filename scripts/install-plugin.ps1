# Installs BitBuddy (the app + the plugin) from your build.
#
#   Right-click this file (scripts\install-plugin.ps1) -> "Run with PowerShell"
#   (Windows will ask for admin permission once - click Yes.)
#
# - the app:    %LOCALAPPDATA%\Programs\BitBuddy\BitBuddy.exe + a Start-menu shortcut (no admin)
# - the plugin: your personal plugin folder %LOCALAPPDATA%\Programs\Common\VST3 (no admin)
#               and the standard system folder C:\Program Files\Common Files\VST3 (admin)

$ErrorActionPreference = 'Stop'
$release = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\BitBuddy_artefacts\Release'
$src = Join-Path $release 'VST3\BitBuddy.vst3'
if (-not (Test-Path $src)) { throw "Build the plugin first: $src not found" }

# The app (no admin)
$appSrc = Join-Path $release 'Standalone\BitBuddy.exe'
if (Test-Path $appSrc) {
    $appDir = Join-Path $env:LOCALAPPDATA 'Programs\BitBuddy'
    New-Item -ItemType Directory -Force $appDir | Out-Null
    Copy-Item -Force $appSrc $appDir
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'BitBuddy.lnk'
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
    $link.TargetPath = Join-Path $appDir 'BitBuddy.exe'
    $link.WorkingDirectory = $appDir
    $link.Description = 'BitBuddy - easy chiptune synth'
    $link.Save()
    Write-Host "App installed:          $appDir\BitBuddy.exe (Start menu: BitBuddy)"
}

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
$binary = 'Contents\x86_64-win\BitBuddy.vst3'
try {
    Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile', '-Command', $cmd -Wait
    $installed = Join-Path "$sysDir\BitBuddy.vst3" $binary
    if ((Test-Path $installed) -and
        (Get-FileHash $installed).Hash -eq (Get-FileHash (Join-Path $src $binary)).Hash) {
        Write-Host "Installed system-wide: $sysDir\BitBuddy.vst3"
    } else {
        Write-Host "System-wide copy did not complete - try running this script again."
    }
} catch {
    Write-Host "System-wide install skipped (admin permission was not given)."
}

Write-Host "`nNow open your DAW and re-scan plugins. Look for 'BitBuddy' under instruments."
