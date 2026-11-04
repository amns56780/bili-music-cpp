# Build the portable zip + NSIS installer for the C++ desktop build.
#   .\scripts\package.ps1              -> build + package + smoke test
#   .\scripts\package.ps1 -SkipBuild   -> reuse the existing build
#   .\scripts\package.ps1 -SkipRuntime -> skip launching the packaged exe
#
# The installer is a modern NSIS (Modern UI 2) wizard, the same family the
# Electron app uses: per-user install (no UAC), Chinese wizard, directory /
# component pages, Start Menu + optional desktop shortcut, uninstall entry.
# NSIS 3.x portable is expected at ..\tools\nsis (or set NSIS_DIR).
# NOTE: keep this file ASCII-only: Windows PowerShell 5.1 parses BOM-less .ps1 as ANSI.
param(
    [switch]$SkipBuild,
    [switch]$SkipRuntime
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$repoParent = Split-Path $root -Parent
$buildDir = Join-Path $root 'build'
$stageRoot = Join-Path $buildDir 'package'
$stage = Join-Path $stageRoot 'bilimusic'
$dist = Join-Path $buildDir 'dist'
$pkgSrc = Join-Path $root 'scripts\packaging'

# app version from CMakeLists (single source of truth)
$cmakeText = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
$match = [regex]::Match($cmakeText, 'project\(bilimusic_cpp VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $match.Success) { throw 'cannot read project version from CMakeLists.txt' }
$appVersion = $match.Groups[1].Value

# NSIS toolchain (portable, not installed system-wide)
$nsisDir = $env:NSIS_DIR
if (-not $nsisDir) { $nsisDir = Join-Path $repoParent 'tools\nsis' }
$makensis = Join-Path $nsisDir 'makensis.exe'
if (-not (Test-Path $makensis)) {
    throw ('NSIS not found: ' + $makensis + '  (download NSIS 3.x portable into tools\nsis or set NSIS_DIR)')
}

# close any running instance first (build output / payload files may be locked)
Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

if (-not $SkipBuild) {
    Write-Host '[1/7] Building Release'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
} else {
    Write-Host '[1/7] Reusing existing build'
}

$exe = Join-Path $buildDir 'bilimusic.exe'
if (-not (Test-Path $exe)) { throw ('missing ' + $exe) }

Write-Host '[2/7] Staging payload'
if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe (Join-Path $stage 'bilimusic.exe') -Force
Copy-Item (Join-Path $root 'third_party\icons\bilimusic.ico') (Join-Path $stage 'bilimusic.ico') -Force
Copy-Item (Join-Path $pkgSrc 'bilimusic.bat') (Join-Path $stage 'bilimusic.bat') -Force
Copy-Item (Join-Path $pkgSrc 'readme.txt') (Join-Path $stage 'readme.txt') -Force

$assetsStage = Join-Path $stage 'assets'
New-Item -ItemType Directory -Force -Path $assetsStage | Out-Null
$iconFont = Join-Path $buildDir 'assets\MaterialIcons-Regular.ttf'
if (-not (Test-Path $iconFont)) { $iconFont = Join-Path $root 'third_party\icons\MaterialIcons-Regular.ttf' }
Copy-Item $iconFont (Join-Path $assetsStage 'MaterialIcons-Regular.ttf') -Force

Write-Host '[3/7] Copying bundled VLC core'
$vlcCandidates = @(
    (Join-Path $repoParent 'source\desktop\build\portable\bilimusic\vlc'),
    (Join-Path $repoParent 'tools\portable-test\vlc'),
    'C:\Program Files\VideoLAN\VLC',
    'C:\Program Files (x86)\VideoLAN\VLC'
)
$vlcSrc = $null
foreach ($candidate in $vlcCandidates) {
    if (Test-Path (Join-Path $candidate 'libvlc.dll')) { $vlcSrc = $candidate; break }
}
if (-not $vlcSrc) { throw 'no VLC installation found to bundle (checked source/desktop/build/portable and Program Files)' }
Write-Host ('  VLC source: ' + $vlcSrc)

$vlcStage = Join-Path $stage 'vlc'
New-Item -ItemType Directory -Force -Path $vlcStage | Out-Null
Copy-Item (Join-Path $vlcSrc 'libvlc.dll') (Join-Path $vlcStage 'libvlc.dll') -Force
Copy-Item (Join-Path $vlcSrc 'libvlccore.dll') (Join-Path $vlcStage 'libvlccore.dll') -Force
if (Test-Path (Join-Path $vlcSrc 'COPYING.txt')) {
    Copy-Item (Join-Path $vlcSrc 'COPYING.txt') (Join-Path $vlcStage 'COPYING.txt') -Force
}
$pluginsSrc = Join-Path $vlcSrc 'plugins'
$pluginsDst = Join-Path $vlcStage 'plugins'
if (Test-Path $pluginsSrc) {
    & robocopy $pluginsSrc $pluginsDst /E /NFL /NDL /NJH /NJS /NP /XF plugins.dat | Out-Null
    if ($LASTEXITCODE -ge 8) { throw ('robocopy plugins failed: ' + $LASTEXITCODE) }
}
$vlcFiles = @(Get-ChildItem $vlcStage -Recurse -File)
$vlcMb = [math]::Round((($vlcFiles | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host ('  VLC: {0} files, {1} MB' -f $vlcFiles.Count, $vlcMb)

foreach ($need in @('bilimusic.exe', 'bilimusic.ico', 'bilimusic.bat', 'readme.txt',
        'assets\MaterialIcons-Regular.ttf', 'vlc\libvlc.dll', 'vlc\plugins')) {
    if (-not (Test-Path (Join-Path $stage $need))) { throw ('staged package is missing ' + $need) }
}

Write-Host '[4/7] Creating portable zip'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zipPath = Join-Path $dist 'bilimusic-cpp-portable-win64.zip'
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
if (-not (Test-Path $zipPath)) { throw 'portable zip creation failed' }

Write-Host ('[5/7] Building NSIS installer (v' + $appVersion + ')')
$setupPath = Join-Path $dist 'bilimusic-cpp-setup.exe'
Remove-Item $setupPath -Force -ErrorAction SilentlyContinue
$iconPath = Join-Path $root 'third_party\icons\bilimusic.ico'
$nsiPath = Join-Path $pkgSrc 'bilimusic.nsi'
& $makensis '-V2' ('/DPAYLOAD=' + $stage) ('/DOUTFILE=' + $setupPath) ('/DICON=' + $iconPath) ('/DBITMAPDIR=' + $pkgSrc) ('/DAPPVERSION=' + $appVersion) $nsiPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $setupPath)) { throw 'NSIS build failed' }

Write-Host '[6/7] Self-check'
$setupInfo = (Get-Item $setupPath).VersionInfo
if ($setupInfo.FileVersion -notlike ($appVersion + '*')) {
    throw ('installer version mismatch: ' + $setupInfo.FileVersion + ' (expected ' + $appVersion + ')')
}
$exeInfo = (Get-Item (Join-Path $stage 'bilimusic.exe')).VersionInfo
if ($exeInfo.FileVersion -notlike ($appVersion + '*')) {
    throw ('exe version mismatch: ' + $exeInfo.FileVersion)
}
$payloadCount = @(Get-ChildItem $stage -Recurse -File).Count
Write-Host ('  [ok] exe/installer version ' + $appVersion + ', payload ' + $payloadCount + ' files')
Write-Host ('  [ok] setup.exe: ' + $setupInfo.FileDescription)

Write-Host '[7/7] Sizes and runtime smoke test'
Write-Host ('  exe        {0} MB' -f [math]::Round((Get-Item (Join-Path $stage 'bilimusic.exe')).Length / 1MB, 2))
Write-Host ('  vlc        {0} MB ({1} files)' -f $vlcMb, $vlcFiles.Count)
Write-Host ('  portable   {0} MB' -f [math]::Round((Get-Item $zipPath).Length / 1MB, 1))
Write-Host ('  setup.exe  {0} MB' -f [math]::Round((Get-Item $setupPath).Length / 1MB, 1))

if (-not $SkipRuntime) {
    Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath (Join-Path $stage 'bilimusic.exe') -WorkingDirectory $stage -PassThru
    $windowMs = -1
    while ($watch.ElapsedMilliseconds -lt 15000) {
        Start-Sleep -Milliseconds 100
        if ($proc.HasExited) { break }
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne 0) { $windowMs = $watch.ElapsedMilliseconds; break }
    }
    Start-Sleep -Milliseconds 1200
    $proc.Refresh()
    if ($proc.HasExited) { throw 'packaged exe exited during smoke test' }
    Write-Host ('  startup    {0} ms to window' -f $windowMs)
    Write-Host ('  working set {0} MB' -f [math]::Round($proc.WorkingSet64 / 1MB, 1))
    Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Host 'Done.'
Write-Host ('  ' + $zipPath)
Write-Host ('  ' + $setupPath)
