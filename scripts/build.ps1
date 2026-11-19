<#
.SYNOPSIS
  Release build + MSIX package of Keyboard Changer for x64 (AMD64) or ARM64.

.EXAMPLE
  # AMD64
  .\scripts\build.ps1 -Arch x64 -QtDir C:\Qt\6.11.1\msvc2022_64

  # ARM64 (cross-compiled on an x64 PC; needs the x64 Qt as host tools)
  .\scripts\build.ps1 -Arch arm64 -QtDir C:\Qt\6.11.1\msvc2022_arm64 -QtHostDir C:\Qt\6.11.1\msvc2022_64

  # Both, then a bundle for the Store
  .\scripts\build.ps1 -Arch x64   -QtDir ... ; .\scripts\build.ps1 -Arch arm64 -QtDir ... -QtHostDir ...
  .\scripts\bundle.ps1

  Requires: Visual Studio 2022 (Desktop C++ workload, ARM64 build tools for arm64),
  Windows 10/11 SDK (makeappx.exe, signtool.exe), Qt for MSVC.
#>
param(
    [ValidateSet('x64', 'arm64')] [string] $Arch = 'x64',
    [Parameter(Mandatory)] [string] $QtDir,
    [string] $QtHostDir,
    [string] $Version = '1.0.0.0',
    # Values from Partner Center (Product identity) for the Store build:
    [string] $IdentityName = 'KeyboardChanger.Dev',
    [string] $Publisher = 'CN=KeyboardChangerDev',
    [string] $PublisherDisplayName = 'Keyboard Changer',
    # Optional: sign for sideloading (the Store re-signs packages itself)
    [string] $CertPfx,
    [string] $CertPassword,
    # Where .msix files go (release-store.ps1 passes its own folder)
    [string] $OutDir,
    # Delete the build folder first (always used for Store builds)
    [switch] $Clean,
    [switch] $NoPackage
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build\release-$Arch"
$StageDir = Join-Path $BuildDir 'package'
if (-not $OutDir) { $OutDir = Join-Path $Root 'dist' }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw "Version must have 4 parts, e.g. 1.2.0.0 (got '$Version')" }

if ($Arch -eq 'arm64' -and -not $QtHostDir) { throw '-QtHostDir (x64 Qt) is required for ARM64 cross builds' }

# --- 1. MSVC environment ------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with C++ tools not found' }
$hostArch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$vcArch = if ($hostArch -eq $Arch) { $Arch } else { "${hostArch}_$Arch" }
Write-Host "==> vcvarsall $vcArch"
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvarsall.bat`" $vcArch >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

# --- 2. Configure & build -----------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) { Write-Host '==> clean'; Remove-Item -Recurse -Force $BuildDir }
$cmakeArgs = @(
    '-S', $Root, '-B', $BuildDir, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DKC_BUILD_VERSION=$Version",
    "-DCMAKE_TOOLCHAIN_FILE=$QtDir\lib\cmake\Qt6\qt.toolchain.cmake"
)
if ($QtHostDir) { $cmakeArgs += "-DQT_HOST_PATH=$QtHostDir" }
Write-Host '==> cmake configure'
cmake @cmakeArgs
if ($LASTEXITCODE) { throw 'cmake configure failed' }
Write-Host '==> cmake build'
cmake --build $BuildDir --config Release
if ($LASTEXITCODE) { throw 'build failed' }

# --- 3. Deploy Qt runtime -----------------------------------------------------
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force $StageDir | Out-Null
Copy-Item (Join-Path $BuildDir 'KeyboardChanger.exe') $StageDir

$deployTool = if ($QtHostDir) { "$QtHostDir\bin\windeployqt.exe" } else { "$QtDir\bin\windeployqt.exe" }
$deployArgs = @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw',
                '--no-compiler-runtime', (Join-Path $StageDir 'KeyboardChanger.exe'))
if ($QtHostDir) {
    $qtpaths = if (Test-Path "$QtDir\bin\qtpaths.bat") { "$QtDir\bin\qtpaths.bat" } else { "$QtDir\bin\qtpaths.exe" }
    $deployArgs = @('--qtpaths', $qtpaths) + $deployArgs
}
Write-Host '==> windeployqt'
& $deployTool @deployArgs
if ($LASTEXITCODE) { throw 'windeployqt failed' }

if ($NoPackage) { Write-Host "Done (no package): $StageDir"; exit 0 }

# --- 4. MSIX ------------------------------------------------------------------
Copy-Item -Recurse (Join-Path $Root 'packaging\Assets') (Join-Path $StageDir 'Assets')
(Get-Content -Raw -Encoding UTF8 (Join-Path $Root 'packaging\AppxManifest.xml.in')).
    Replace('@IDENTITY_NAME@', $IdentityName).
    Replace('@PUBLISHER@', $Publisher).
    Replace('@PUBLISHER_DISPLAY_NAME@', $PublisherDisplayName).
    Replace('@VERSION@', $Version).
    Replace('@ARCH@', $Arch) |
    Set-Content -Encoding UTF8 (Join-Path $StageDir 'AppxManifest.xml')

$kitBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$makeappx = Join-Path $kitBin.FullName 'x64\makeappx.exe'
$signtool = Join-Path $kitBin.FullName 'x64\signtool.exe'

New-Item -ItemType Directory -Force $OutDir | Out-Null
$msix = Join-Path $OutDir "KeyboardChanger_${Version}_$Arch.msix"
Write-Host '==> makeappx pack'
& $makeappx pack /d $StageDir /p $msix /o
if ($LASTEXITCODE) { throw 'makeappx failed' }

if ($CertPfx) {
    Write-Host '==> signtool'
    & $signtool sign /fd SHA256 /a /f $CertPfx /p $CertPassword $msix
    if ($LASTEXITCODE) { throw 'signtool failed' }
}
Write-Host "Package: $msix"
