<#
.SYNOPSIS
  Combines dist\*_x64.msix and dist\*_arm64.msix into one .msixbundle for the Microsoft Store.
#>
param(
    [string] $Version = '1.0.0.0',
    # Folder with KeyboardSwitcher_<Version>_<arch>.msix files
    [string] $DistDir,
    [string] $CertPfx,
    [string] $CertPassword
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Dist = if ($DistDir) { $DistDir } else { Join-Path $Root 'dist' }
$BundleIn = Join-Path $Dist 'bundle-input'

$kitBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*" -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$makeappx = Join-Path $kitBin.FullName 'x64\makeappx.exe'
$signtool = Join-Path $kitBin.FullName 'x64\signtool.exe'

if (Test-Path $BundleIn) { Remove-Item -Recurse -Force $BundleIn }
New-Item -ItemType Directory $BundleIn | Out-Null
$parts = Get-ChildItem $Dist -Filter "KeyboardSwitcher_${Version}_*.msix"
if (-not $parts) { throw "No KeyboardSwitcher_${Version}_*.msix in $Dist" }
$parts | Copy-Item -Destination $BundleIn

$bundle = Join-Path $Dist "KeyboardSwitcher_$Version.msixbundle"
& $makeappx bundle /d $BundleIn /p $bundle /bv $Version /o
if ($LASTEXITCODE) { throw 'makeappx bundle failed' }

if ($CertPfx) {
    & $signtool sign /fd SHA256 /a /f $CertPfx /p $CertPassword $bundle
    if ($LASTEXITCODE) { throw 'signtool failed' }
}
Remove-Item -Recurse -Force $BundleIn
Write-Host "Bundle: $bundle"
