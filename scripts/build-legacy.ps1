<#
.SYNOPSIS
  Legacy build for Windows 7 / 8 / 8.1 / 10 / 11, 32-bit (x86) or 64-bit, with Qt 5.15 + MinGW.
  Qt 6 runs only on Windows 10+ x64/ARM64; this build covers everything older.
  Output: portable zip (no installer, no admin rights, the MinGW runtime is included).

.EXAMPLE
  .\scripts\build-legacy.ps1 -Arch x86
  .\scripts\build-legacy.ps1 -Arch x64 -Version 1.1.0.0

  Requires Qt 5.15.2 "MinGW 8.1.0 32-bit / 64-bit" and the matching "MinGW 8.1.0" tool
  (Qt Maintenance Tool -> Archive -> Qt 5.15.2; Developer and Designer Tools -> MinGW 8.1.0).
#>
param(
    [ValidateSet('x86', 'x64')] [string] $Arch = 'x86',
    [string] $QtDir,
    [string] $MinGWDir,
    [string] $Version = '1.1.0.0'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$bits = if ($Arch -eq 'x86') { '32' } else { '64' }
if (-not $QtDir) { $QtDir = "C:\Qt\5.15.2\mingw81_$bits" }
if (-not $MinGWDir) { $MinGWDir = "C:\Qt\Tools\mingw810_$bits" }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw "Version must have 4 parts, e.g. 1.1.0.0" }
foreach ($p in "$QtDir\bin\windeployqt.exe", "$MinGWDir\bin\g++.exe", "$MinGWDir\bin\mingw32-make.exe") {
    if (-not (Test-Path $p)) { throw "Not found: $p (install Qt 5.15.2 MinGW $bits-bit, or pass -QtDir / -MinGWDir)" }
}

$BuildDir = Join-Path $Root "build\legacy-$Arch"
$Name = "KeyboardChanger-$Version-win7-$Arch"
$Stage = Join-Path $Root "dist\legacy\$Name"
$Zip = Join-Path $Root "dist\legacy\$Name.zip"

$env:PATH = "$MinGWDir\bin;$QtDir\bin;$env:PATH"

Write-Host '==> configure'
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
cmake -S $Root -B $BuildDir -G 'MinGW Makefiles' `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$QtDir" `
    -DKC_FORCE_QT5=ON `
    "-DKC_BUILD_VERSION=$Version"
if ($LASTEXITCODE) { throw 'cmake configure failed' }

Write-Host '==> build'
cmake --build $BuildDir --parallel
if ($LASTEXITCODE) { throw 'build failed' }

Write-Host '==> deploy'
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item (Join-Path $BuildDir 'KeyboardChanger.exe') $Stage
& "$QtDir\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --no-angle --compiler-runtime (Join-Path $Stage 'KeyboardChanger.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed' }

Write-Host '==> zip'
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip
Write-Host "Done: $Zip"
