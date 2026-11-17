param(
    [string]$QtRoot = 'D:\qt_install\6.11.1\mingw_64',
    [string]$MingwRoot = 'D:\qt_install\Tools\mingw1310_64',
    [switch]$SmokeTest
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $repoRoot 'build-windows\smarthome.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Run tools/build-windows.ps1 first.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + (Join-Path $MingwRoot 'bin') + ';' + $env:PATH
    if ($SmokeTest) {
        $env:QT_QPA_PLATFORM = 'offscreen'
        & $binary --simulate --smoke-test
    } else {
        & $binary --simulate
    }
    if ($LASTEXITCODE) { throw "Application exited with $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM = $previousPlatform
}
