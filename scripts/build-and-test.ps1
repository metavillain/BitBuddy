# One command to build BitBuddy and run every check.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-and-test.ps1 [-SkipPluginval]
#
# 1. configure + build (Release)       logs        -> build\logs\
# 2. audio tests (real VST3 in a host)  report      -> docs\reports\test-results.txt
# 3. keyboard-focus UI test             report      -> docs\reports\focus-test.txt
# 4. CPU benchmark                      report      -> docs\reports\benchmark.txt
# 5. pluginval, strictness 10           report      -> docs\reports\pluginval.txt
#    (downloaded once into build\tools\pluginval from Tracktion's GitHub releases)
# 6. UI snapshots -> build\snapshots\, README image -> docs\screenshot.png
#
# Exit code = number of failed steps.

param([switch]$SkipPluginval)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')

$logs     = Join-Path $root 'build\logs'
$reports  = Join-Path $root 'docs\reports'
$vst3     = Join-Path $root 'build\BitBuddy_artefacts\Release\VST3\BitBuddy.vst3'
$testHost = Join-Path $root 'build\BitBuddyTestHost_artefacts\Release\BitBuddyTestHost.exe'
New-Item -ItemType Directory -Force $logs, $reports | Out-Null

$summary = New-Object System.Collections.Generic.List[string]
$failed = 0

# --- 1. build -------------------------------------------------------------------
Write-Host '== Building (Release)...'
$t = Get-Date
cmake -B build -G 'Visual Studio 17 2022' -A x64 *> (Join-Path $logs 'configure.log')
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed - see build\logs\configure.log" }
cmake --build build --config Release --parallel -- /m /v:minimal *> (Join-Path $logs 'build.log')
if ($LASTEXITCODE -ne 0) {
    Select-String -Path (Join-Path $logs 'build.log') -Pattern ': error' | Select-Object -First 20 | ForEach-Object { $_.Line }
    throw 'Build failed - see build\logs\build.log'
}
$warnings = @(Select-String -Path (Join-Path $logs 'build.log') -Pattern 'warning C').Count
$summary.Add(('Build          OK in {0}s, {1} warning(s)' -f [int]((Get-Date) - $t).TotalSeconds, $warnings))

# --- 2-4. test host ------------------------------------------------------------------
function Invoke-TestHost ([string]$label, [string]$reportName, [string[]]$hostArgs) {
    Write-Host "== $label..."
    $report = Join-Path $reports $reportName
    & $testHost @hostArgs | Out-File -Encoding utf8 $report
    $code = $LASTEXITCODE
    $line = Get-Content $report | Where-Object { $_ -match 'passed,|SKIP' } | Select-Object -Last 1
    if (-not $line) { $line = 'done' }
    $script:summary.Add(('{0,-14} {1}  {2}' -f $label, $(if ($code -eq 0) { 'OK  ' } else { 'FAIL' }), $line.Trim()))
    if ($code -ne 0) { $script:failed++ }
}

Invoke-TestHost 'Audio tests' 'test-results.txt' @('--test', $vst3)
Invoke-TestHost 'Focus test'  'focus-test.txt'   @('--focus-test')
Invoke-TestHost 'Benchmark'   'benchmark.txt'    @('--bench', $vst3)

# --- 5. pluginval -------------------------------------------------------------------
if (-not $SkipPluginval) {
    Write-Host '== pluginval (strictness 10)...'
    $pvDir = Join-Path $root 'build\tools\pluginval'
    $pv = Join-Path $pvDir 'pluginval.exe'
    if (-not (Test-Path $pv)) {
        New-Item -ItemType Directory -Force $pvDir | Out-Null
        $release = Invoke-RestMethod 'https://api.github.com/repos/Tracktion/pluginval/releases/latest' -Headers @{ 'User-Agent' = 'bitbuddy-build' }
        $asset = $release.assets | Where-Object { $_.name -like '*Windows*' } | Select-Object -First 1
        $zip = Join-Path $pvDir $asset.name
        Invoke-WebRequest $asset.browser_download_url -OutFile $zip
        Expand-Archive $zip -DestinationPath $pvDir -Force
        Remove-Item -LiteralPath $zip
    }
    $pvOut = Join-Path $logs 'pluginval'
    New-Item -ItemType Directory -Force $pvOut | Out-Null
    $p = Start-Process -FilePath $pv -NoNewWindow -Wait -PassThru `
            -ArgumentList '--strictness-level', '10', '--timeout-ms', '600000', '--output-dir', "`"$pvOut`"", '--validate', "`"$vst3`"" `
            -RedirectStandardOutput (Join-Path $logs 'pluginval-stdout.txt')
    $latest = Get-ChildItem $pvOut -Filter '*.txt' | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($latest) { Copy-Item $latest.FullName (Join-Path $reports 'pluginval.txt') -Force }
    $summary.Add(('{0,-14} {1}  strictness 10' -f 'pluginval', $(if ($p.ExitCode -eq 0) { 'OK  ' } else { 'FAIL' })))
    if ($p.ExitCode -ne 0) { $failed++ }
}

# --- 6. snapshots ------------------------------------------------------------------
Write-Host '== UI snapshots...'
$snaps = Join-Path $root 'build\snapshots'
& $testHost --snapshot $snaps | Out-Null
Copy-Item (Join-Path $snaps 'ui-hero-lead.png') (Join-Path $root 'docs\screenshot.png') -Force
$summary.Add(('{0,-14} OK    build\snapshots\ ({1} images), docs\screenshot.png' -f 'Snapshots', @(Get-ChildItem $snaps -Filter '*.png').Count))

# --- summary -----------------------------------------------------------------------
$size = (Get-Item (Join-Path $vst3 'Contents\x86_64-win\BitBuddy.vst3')).Length
$summary.Add(('{0,-14} {1:N2} MB' -f 'Plugin size', ($size / 1MB)))
Write-Host ''
$summary | ForEach-Object { Write-Host "  $_" }
exit $failed
