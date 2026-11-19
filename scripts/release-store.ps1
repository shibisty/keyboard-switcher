<#
.SYNOPSIS
  Production build for the Microsoft Store.
  x64 + ARM64 Release -> .msix -> .msixbundle -> .msixupload (with PDB symbols).

.DESCRIPTION
  1. Checks scripts\store.config.json (identity from Partner Center, Qt paths) and the version.
  2. Clean Release builds for every architecture (each in its own process, own MSVC env).
  3. Verifies each package: manifest identity/version/arch, exe version, no debug Qt DLLs.
  4. Packs the .msixbundle and the .appxsym symbol files into a .msixupload.
  Packages are NOT signed: the Store signs them itself.

.EXAMPLE
  .\scripts\release-store.ps1 -Version 1.2.0.0

.EXAMPLE
  .\scripts\release-store.ps1 -Version 1.2.0.0 -Arch x64     # only AMD64
#>
param(
    [Parameter(Mandatory)] [string] $Version,
    [string] $Config,
    [ValidateSet('x64', 'arm64')] [string[]] $Arch = @('x64', 'arm64'),
    # Rebuild a version that already exists in dist\store
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Config) { $Config = Join-Path $PSScriptRoot 'store.config.json' }

function Fail([string] $msg) { Write-Host "`nERROR: $msg" -ForegroundColor Red; exit 1 }
function Step([string] $msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }

# --- 1. Checks -----------------------------------------------------------------
Step 'Checking configuration'
if (-not (Test-Path $Config)) {
    Fail ("Config not found: $Config`n" +
          "Copy scripts\store.config.example.json to scripts\store.config.json and fill it in " +
          "from Partner Center -> your app -> Product management -> Product identity.")
}
$cfg = Get-Content -Raw -Encoding UTF8 $Config | ConvertFrom-Json

foreach ($key in 'identityName', 'publisher', 'publisherDisplayName', 'qtX64') {
    $value = [string]$cfg.$key
    if (-not $value -or $value.Contains('<')) { Fail "store.config.json: fill in '$key'" }
}
if ('arm64' -in $Arch -and (-not $cfg.qtArm64 -or ([string]$cfg.qtArm64).Contains('<'))) {
    Fail "store.config.json: fill in 'qtArm64' (or build only x64: -Arch x64)"
}
if ($cfg.publisher -notmatch '^CN=') { Fail "publisher must start with 'CN=' (Package/Identity/Publisher in Partner Center)" }
if ($cfg.identityName -eq 'KeyboardSwitcher.Dev') { Fail 'identityName still has the development value' }

if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)\.(\d+)$') { Fail "Version must have 4 numbers, e.g. 1.2.0.0 (got '$Version')" }
if ([int]$Matches[4] -ne 0) { Fail 'The Store reserves the 4th version number: it must be 0 (e.g. 1.2.3.0)' }
if ([int]$Matches[1] -gt 65535 -or [int]$Matches[2] -gt 65535 -or [int]$Matches[3] -gt 65535) { Fail 'Each version part must be <= 65535' }

$qtFor = @{ x64 = [string]$cfg.qtX64; arm64 = [string]$cfg.qtArm64 }
foreach ($a in $Arch) {
    $toolchain = Join-Path $qtFor[$a] 'lib\cmake\Qt6\qt.toolchain.cmake'
    if (-not (Test-Path $toolchain)) { Fail "Qt for $a not found: $($qtFor[$a]) (need an MSVC 2022 Qt kit)" }
}
if (-not (Test-Path (Join-Path $cfg.qtX64 'bin\windeployqt.exe'))) { Fail "windeployqt.exe not found in $($cfg.qtX64)\bin" }

$outDir = Join-Path $Root "dist\store\$Version"
if (Test-Path $outDir) {
    if (-not $Force) { Fail "$outDir already exists. Every Store submission needs a higher version; use -Force to rebuild this one." }
    Remove-Item -Recurse -Force $outDir
}
$msixDir = Join-Path $outDir 'msix'
$symDir = Join-Path $outDir 'symbols'
New-Item -ItemType Directory -Force $msixDir, $symDir | Out-Null

Write-Host "Identity : $($cfg.identityName)"
Write-Host "Publisher: $($cfg.publisher)"
Write-Host "Version  : $Version"
Write-Host "Arch     : $($Arch -join ', ')"

# --- 2. Builds ------------------------------------------------------------------
$shell = (Get-Process -Id $PID).Path   # same PowerShell (5.1 or 7) in a fresh process per arch
Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($a in $Arch) {
    Step "Building $a"
    $buildArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build.ps1'),
        '-Arch', $a, '-QtDir', $qtFor[$a], '-Version', $Version,
        '-IdentityName', $cfg.identityName, '-Publisher', $cfg.publisher,
        '-PublisherDisplayName', $cfg.publisherDisplayName,
        '-OutDir', $msixDir, '-Clean'
    )
    if ($a -eq 'arm64') { $buildArgs += @('-QtHostDir', $cfg.qtX64) }
    & $shell @buildArgs
    if ($LASTEXITCODE) { Fail "Build for $a failed" }

    # --- 3. Verify ---------------------------------------------------------------
    Step "Verifying $a"
    $buildDir = Join-Path $Root "build\release-$a"
    $msix = Join-Path $msixDir "KeyboardSwitcher_${Version}_$a.msix"
    if (-not (Test-Path $msix)) { Fail "Package not produced: $msix" }

    $zip = [IO.Compression.ZipFile]::OpenRead($msix)
    try {
        $reader = New-Object IO.StreamReader($zip.GetEntry('AppxManifest.xml').Open())
        [xml]$manifest = $reader.ReadToEnd()
        $reader.Close()
        $debugDlls = @($zip.Entries | Where-Object { $_.Name -match '^Qt6\w+d\.dll$' })
    } finally { $zip.Dispose() }

    $id = $manifest.Package.Identity
    if ($id.Name -ne $cfg.identityName) { Fail "Manifest Name '$($id.Name)' != config" }
    if ($id.Publisher -ne $cfg.publisher) { Fail "Manifest Publisher '$($id.Publisher)' != config" }
    if ($id.Version -ne $Version) { Fail "Manifest Version '$($id.Version)' != $Version" }
    if ($id.ProcessorArchitecture -ne $a) { Fail "Manifest architecture '$($id.ProcessorArchitecture)' != $a" }
    if ($debugDlls.Count) { Fail "Debug Qt DLLs inside the package: $($debugDlls.Name -join ', ')" }

    $exe = Join-Path $buildDir 'package\KeyboardSwitcher.exe'
    $exeVersion = (Get-Item $exe).VersionInfo.FileVersion
    if ($exeVersion -ne $Version) { Fail "KeyboardSwitcher.exe version '$exeVersion' != $Version" }
    Write-Host "OK: manifest, exe version $exeVersion, release DLLs"

    # Symbols for Store crash analytics (.appxsym = zip with the PDB)
    $pdb = Join-Path $buildDir 'KeyboardSwitcher.pdb'
    if (Test-Path $pdb) {
        $symZip = Join-Path $symDir "KeyboardSwitcher_${Version}_$a.zip"
        Compress-Archive -Path $pdb -DestinationPath $symZip -Force
        Move-Item $symZip ([IO.Path]::ChangeExtension($symZip, '.appxsym')) -Force
    } else {
        Write-Host "WARNING: $pdb not found, crash reports for $a will have no symbols" -ForegroundColor Yellow
    }
}

# --- 4. Bundle + upload file ----------------------------------------------------
Step 'Creating .msixbundle'
& $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'bundle.ps1') -Version $Version -DistDir $msixDir
if ($LASTEXITCODE) { Fail 'Bundling failed' }
$bundle = Join-Path $outDir "KeyboardSwitcher_$Version.msixbundle"
Move-Item (Join-Path $msixDir "KeyboardSwitcher_$Version.msixbundle") $bundle -Force

Step 'Creating .msixupload'
$archTag = ($Arch | Sort-Object) -join '_'
$uploadZip = Join-Path $outDir "KeyboardSwitcher_${Version}_$archTag.zip"
$items = @($bundle) + @(Get-ChildItem $symDir -Filter '*.appxsym' | ForEach-Object FullName)
Compress-Archive -Path $items -DestinationPath $uploadZip -Force
$upload = [IO.Path]::ChangeExtension($uploadZip, '.msixupload')
Move-Item $uploadZip $upload -Force

$sizeMb = '{0:N1}' -f ((Get-Item $upload).Length / 1MB)
Write-Host "`nDone." -ForegroundColor Green
Write-Host "Upload this file ($sizeMb MB):"
Write-Host "  $upload" -ForegroundColor Green
Write-Host @"

Next steps in Partner Center (partner.microsoft.com/dashboard):
  1. Apps and games -> Keyboard Switcher -> Start update (or Start submission).
  2. Packages -> drag the .msixupload file in, wait for validation.
  3. Device families: Windows 10/11 Desktop.
  4. Restricted capability 'runFullTrust': explain it, e.g.
     "Desktop tray utility: watches the foreground window and switches
      the keyboard layout per application."
  5. Submit to the Store.
"@
