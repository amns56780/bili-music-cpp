# Build the portable zip + self-extracting installer for the C++ desktop build.
#   .\scripts\package.ps1              -> build + package + self-check + runtime smoke test
#   .\scripts\package.ps1 -SkipBuild   -> reuse the existing build
#   .\scripts\package.ps1 -SkipRuntime -> skip launching the packaged exe
#
# The installer EXE is the official 7-Zip SFX module + a .7z payload
# (created with py7zr, because 7z.exe itself is blocked by the installed AV).
# 7-Zip is NOT required on the user's machine.
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
$minizDir = Join-Path $root 'third_party\miniz'

# portable MinGW-w64 (same defaults as scripts\build.ps1)
$kit = $env:W64DEVKIT
if (-not $kit) { $kit = 'C:\Users\amns2333\BiliMusic\tools\w64devkit' }
$gxx = Join-Path $kit 'bin\g++.exe'
$windres = Join-Path $kit 'bin\windres.exe'
if (-not (Test-Path $gxx)) { throw ('MinGW-w64 not found: ' + $kit) }

# close any running instance first (build output / payload files may be locked)
Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

function Read-BytesWithRetry([string]$path) {
    for ($i = 0; $i -lt 40; $i++) {
        try { return [System.IO.File]::ReadAllBytes($path) } catch { Start-Sleep -Milliseconds 250 }
    }
    throw ('cannot read file after retries: ' + $path)
}

if (-not $SkipBuild) {
    Write-Host '[1/8] Building Release'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
} else {
    Write-Host '[1/8] Reusing existing build'
}

$exe = Join-Path $buildDir 'bilimusic.exe'
if (-not (Test-Path $exe)) { throw ('missing ' + $exe) }

Write-Host '[2/8] Staging payload'
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

# installer scripts: sources are UTF-8 for editing, package them as GBK for cmd.exe
$gbk = [System.Text.Encoding]::GetEncoding(936)
$crlf = [string][char]13 + [string][char]10
foreach ($pair in @(
        @{ Src = 'install.bat.utf8'; Dst = 'install.bat' },
        @{ Src = 'uninstall.bat.utf8'; Dst = 'uninstall.bat' })) {
    $text = [System.IO.File]::ReadAllText((Join-Path $pkgSrc $pair.Src))
    $text = [regex]::Replace($text, "\r?\n", $crlf)
    [System.IO.File]::WriteAllText((Join-Path $stage $pair.Dst), $text, $gbk)
}

Write-Host '[3/8] Copying bundled VLC core'
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

foreach ($need in @('bilimusic.exe', 'bilimusic.ico', 'bilimusic.bat', 'readme.txt', 'install.bat',
        'uninstall.bat', 'assets\MaterialIcons-Regular.ttf', 'vlc\libvlc.dll', 'vlc\plugins')) {
    if (-not (Test-Path (Join-Path $stage $need))) { throw ('staged package is missing ' + $need) }
}

Write-Host '[4/8] Creating portable zip'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zipPath = Join-Path $dist 'bilimusic-cpp-portable-win64.zip'
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
if (-not (Test-Path $zipPath)) { throw 'portable zip creation failed' }

Write-Host '[5/8] Building self-extracting installer (7zSD.sfx + py7zr LZMA1 payload)'
$sfxModule = Join-Path $root 'third_party\7zsd\7zSD.sfx'
if (-not (Test-Path $sfxModule)) { throw ('7z.sfx not found: ' + $sfxModule) }
$archive = Join-Path $stageRoot 'bilimusic-payload.7z'
Remove-Item $archive -Force -ErrorAction SilentlyContinue
& python (Join-Path $pkgSrc 'make_7z.py') write $stage $archive
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $archive)) { throw 'py7zr payload creation failed' }

Write-Host '[6/8] Packing setup.exe (7zSD.sfx + config + payload)'
$configPath = Join-Path $stageRoot 'sfx-config.txt'
$sfxConfig = ';!@Install@!UTF-8!' + $crlf + 'Title="bilimusic"' + $crlf + 'RunProgram="install.bat"' + $crlf + 'GUIMode="2"' + $crlf + ';!@InstallEnd@!' + $crlf
[System.IO.File]::WriteAllText($configPath, $sfxConfig, (New-Object System.Text.UTF8Encoding($false)))
$setupPath = Join-Path $dist 'bilimusic-cpp-setup.exe'
$out = [System.IO.File]::Create($setupPath)
try {
    foreach ($part in @($sfxModule, $configPath, $archive)) {
        $bytes = [System.IO.File]::ReadAllBytes($part)
        $out.Write($bytes, 0, $bytes.Length)
    }
} finally {
    $out.Close()
}
if (-not (Test-Path $setupPath)) { throw 'setup.exe creation failed' }
$sfxLength = (Get-Item $sfxModule).Length
$configLength = (Get-Item $configPath).Length

Write-Host '[7/8] Self-check (extract payload back out and validate)'
$verify = Join-Path $stageRoot 'verify'
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Force -Path $verify | Out-Null

# 1) portable zip
$verifyZip = Join-Path $stageRoot 'verify-portable'
if (Test-Path $verifyZip) { Remove-Item $verifyZip -Recurse -Force }
Expand-Archive -Path $zipPath -DestinationPath $verifyZip -Force
foreach ($need in @('bilimusic.exe', 'vlc\libvlc.dll', 'install.bat', 'assets\MaterialIcons-Regular.ttf')) {
    if (-not (Test-Path (Join-Path $verifyZip $need))) { throw ('portable zip is missing ' + $need) }
}
Write-Host ('  [ok] portable zip: {0} files' -f @(Get-ChildItem $verifyZip -Recurse -File).Count)

# 2) setup.exe payload: cut the 7z part out (after sfx module + config) and extract it
$setupBytes = [System.IO.File]::ReadAllBytes($setupPath)
$payloadOffset = [int]($sfxLength + $configLength)
$payloadLen = $setupBytes.Length - $payloadOffset
if ($payloadLen -le 0) { throw 'setup payload is empty' }
$payloadArchive = Join-Path $stageRoot 'verify-payload.7z'
$fs = [System.IO.File]::Create($payloadArchive)
try { $fs.Write($setupBytes, $payloadOffset, $payloadLen) } finally { $fs.Close() }
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Force -Path $verify | Out-Null
& python (Join-Path $pkgSrc 'make_7z.py') extract $payloadArchive $verify
if ($LASTEXITCODE -ne 0) { throw 'payload extraction failed' }
foreach ($need in @('bilimusic.exe', 'vlc\libvlc.dll', 'vlc\plugins', 'install.bat', 'uninstall.bat', 'assets\MaterialIcons-Regular.ttf')) {
    if (-not (Test-Path (Join-Path $verify $need))) { throw ('setup payload is missing ' + $need) }
}
$inst = [System.IO.File]::ReadAllText((Join-Path $verify 'install.bat'), $gbk)
if ($inst -notmatch 'Uninstall\\bilimusic') { throw 'install.bat does not register the uninstall key' }
$uni = [System.IO.File]::ReadAllText((Join-Path $verify 'uninstall.bat'), $gbk)
if ($uni -notmatch 'reg delete') { throw 'uninstall.bat does not remove the registry key' }
Write-Host ('  [ok] setup payload: {0} files, installers valid' -f @(Get-ChildItem $verify -Recurse -File).Count)

Write-Host '[8/8] Sizes and runtime smoke test'
Write-Host ('  exe        {0} MB' -f [math]::Round((Get-Item (Join-Path $stage 'bilimusic.exe')).Length / 1MB, 2))
Write-Host ('  sfx module {0} KB' -f [math]::Round($sfxLength / 1KB, 0))
Write-Host ('  vlc        {0} MB ({1} files)' -f $vlcMb, $vlcFiles.Count)
Write-Host ('  portable   {0} MB' -f [math]::Round((Get-Item $zipPath).Length / 1MB, 1))
Write-Host ('  setup.exe  {0} MB' -f [math]::Round((Get-Item $setupPath).Length / 1MB, 1))

if (-not $SkipRuntime) {
    Get-Process bilimusic -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    $logPath = Join-Path $env:USERPROFILE '.bilimusic\cpp-app.log'
    $before = 0
    if (Test-Path $logPath) { $before = (Get-Item $logPath).Length }
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