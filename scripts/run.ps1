# Build (if needed) and launch the C++ desktop app.
#   .\scripts\run.ps1            -> Release build + launch
#   .\scripts\run.ps1 -Console   -> console build (shows stderr/logs)
param([switch]$Console)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$args = @()
if ($Console) { $args += '-Console' }
& (Join-Path $PSScriptRoot 'build.ps1') @args
$exe = Join-Path $root 'build\bilimusic.exe'
if (-not (Test-Path $exe)) { throw ('not found: ' + $exe) }
Start-Process -FilePath $exe
Write-Host ('launched: ' + $exe)
