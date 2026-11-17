param(
    [string]$QtRoot = 'D:\qt_install\6.11.1\mingw_64',
    [string]$MingwRoot = 'D:\qt_install\Tools\mingw1310_64',
    [string]$CMake = 'cmake',
    [string]$NinjaDir = '',
    [string]$BuildDir = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build-windows' }
foreach ($file in @((Join-Path $QtRoot 'bin\qmake.exe'), (Join-Path $MingwRoot 'bin\g++.exe'))) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing tool: $file" }
}
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + (Join-Path $MingwRoot 'bin') + ';' + $NinjaDir + ';' + $env:PATH
    & $CMake -S $repoRoot -B $BuildDir -G Ninja "-DCMAKE_CXX_COMPILER=$MingwRoot/bin/g++.exe" "-DCMAKE_PREFIX_PATH=$QtRoot"
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    & $CMake --build $BuildDir -j2
    if ($LASTEXITCODE) { throw 'Build failed' }
    $ctest = Join-Path (Split-Path (Get-Command $CMake).Source) 'ctest.exe'
    & $ctest --test-dir $BuildDir -V
    if ($LASTEXITCODE) { throw 'Tests failed' }
} finally { $env:PATH = $previousPath }
