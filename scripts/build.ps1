# One-click build using the portable MinGW-w64 + CMake under ../tools (no VS / Qt needed).
#   .\scripts\build.ps1            -> Release build
#   .\scripts\build.ps1 -Console   -> console window build (shows stderr)
#   .\scripts\build.ps1 -Clean     -> remove build dir first
# NOTE: keep this file ASCII-only: Windows PowerShell 5.1 parses BOM-less .ps1 as ANSI.
param(
    [switch]$Console,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$kit = $env:W64DEVKIT
if (-not $kit) { $kit = 'F:\BiliMusic\tools\w64devkit' }
$cmakeDir = $env:CMAKE_DIR
if (-not $cmakeDir) { $cmakeDir = 'F:\BiliMusic\tools\cmake' }
$cmake = Join-Path $cmakeDir 'bin\cmake.exe'

if (-not (Test-Path (Join-Path $kit 'bin\g++.exe'))) { throw ('MinGW-w64 not found: ' + $kit) }
if (-not (Test-Path $cmake)) { throw ('CMake not found: ' + $cmake) }

$env:PATH = (Join-Path $kit 'bin') + ';' + (Join-Path $cmakeDir 'bin') + ';' + $env:PATH
$buildDir = Join-Path $root 'build'
if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }

$configureArgs = @('-S', $root, '-B', $buildDir, '-G', 'MinGW Makefiles', '-DCMAKE_BUILD_TYPE=Release')
$configureArgs += ('-DCMAKE_MAKE_PROGRAM=' + (Join-Path $kit 'bin\make.exe'))
if ($Console) { $configureArgs += '-DBILIMUSIC_CONSOLE=ON' } else { $configureArgs += '-DBILIMUSIC_CONSOLE=OFF' }

Write-Host '[1/2] configure'
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host '[2/2] build'
& $cmake --build $buildDir -j 8
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

$exe = Join-Path $buildDir 'bilimusic.exe'
if (Test-Path $exe) { Write-Host ('OK: ' + $exe) } else { throw 'bilimusic.exe not found' }
