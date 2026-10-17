<#
.SYNOPSIS
Populates the BUSMASTER distribution folder from tracked inputs.

.DESCRIPTION
The folder Sources\BUSMASTER\BIN\Release is a purely generated distribution
folder. Nothing inside it is tracked in git. This script assembles it from:

  - build outputs                 Sources\BUSMASTER\build\<Configuration>\*
                                  (BUSMASTER.exe, driver DLLs, .tlb, ...)
  - kernel build outputs          Sources\Kernel\build\<Configuration>\Busmaster*.dll, DIL_J1939.dll
  - vendor runtime DLLs           Sources\BUSMASTER\Externals\Runtime\*.dll
  - distribution assets           Sources\BUSMASTER\Externals\Assets\*
  - generated version file        parsed from Sources\BUSMASTER\Application\BuildVersion.h
  - splash bitmap                 Sources\BUSMASTER\Application\Res\Splsh16.bmp
  - COM interface files           BUSMASTER_Interface.h/.c and BUSMASTER.tlb
                                  from the build output

The NSIS installer and CI packaging consume everything from this folder.

The script is idempotent: files are only copied when size or timestamp differ.

.PARAMETER Destination
Folder to populate. Defaults to Sources\BUSMASTER\BIN\Release.

.PARAMETER Configuration
Build configuration to deploy. Defaults to Release.

.EXAMPLE
.\scripts\deploy-runtime.ps1
#>
[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\Sources\BUSMASTER\BIN\Release'),
    [ValidateSet('Release', 'Debug', 'busmaster_debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Destination = [IO.Path]::GetFullPath($Destination)
$busmasterRoot = Join-Path $repositoryRoot 'Sources\BUSMASTER'
$kernelRoot = Join-Path $repositoryRoot 'Sources\Kernel'

$buildOutputDirectory = Join-Path $busmasterRoot "build\$Configuration"
$kernelBuildDirectory = Join-Path $kernelRoot "build\$Configuration"
$runtimeDirectory = Join-Path $busmasterRoot 'Externals\Runtime'
$assetsDirectory = Join-Path $busmasterRoot 'Externals\Assets'
$applicationDirectory = Join-Path $busmasterRoot 'Application'
$versionHeader = Join-Path $applicationDirectory 'BuildVersion.h'
$splashBitmap = Join-Path $applicationDirectory 'Res\Splsh16.bmp'

foreach ($required in @($runtimeDirectory, $assetsDirectory, $versionHeader, $splashBitmap)) {
    if (-not (Test-Path $required)) {
        throw "Required input not found: $required"
    }
}

if (-not (Test-Path $buildOutputDirectory)) {
    throw "Build output directory not found: $buildOutputDirectory. Build the BUSMASTER solution first."
}

if (-not (Test-Path $Destination)) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
}

foreach ($processName in @('BUSMASTER', 'BusEmulation')) {
    $runningProcesses = Get-Process -Name $processName -ErrorAction SilentlyContinue
    foreach ($runningProcess in $runningProcesses) {
        Write-Host "Stopping stale $($runningProcess.ProcessName) process (PID $($runningProcess.Id)) before refreshing runtime files."
        Stop-Process -Id $runningProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

$copiedCount = 0

function Copy-IfChanged {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$TargetDirectory
    )

    $targetPath = Join-Path $TargetDirectory (Split-Path -Leaf $Source)
    $needsCopy = -not (Test-Path $targetPath)
    if (-not $needsCopy) {
        $sourceItem = Get-Item $Source
        $targetItem = Get-Item $targetPath
        if ($sourceItem.Length -ne $targetItem.Length -or
            $sourceItem.LastWriteTimeUtc -gt $targetItem.LastWriteTimeUtc) {
            $needsCopy = $true
        }
    }

    if ($needsCopy) {
        Copy-Item -LiteralPath $Source -Destination $targetPath -Force
        $script:copiedCount++
    }
}

# Vendor runtime DLLs.
Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File |
    ForEach-Object { Copy-IfChanged -Source $_.FullName -TargetDirectory $Destination }

# Distribution assets (help file, COM registration manifest, changelog, ...).
Get-ChildItem -LiteralPath $assetsDirectory -File |
    ForEach-Object { Copy-IfChanged -Source $_.FullName -TargetDirectory $Destination }

# Splash bitmap used by the NSIS installer splash screen.
Copy-IfChanged -Source $splashBitmap -TargetDirectory $Destination

# Build outputs: the shared out-of-tree output folder holds every produced
# binary (exe, driver DLLs) plus the MIDL-generated COM interface files.
# Intermediate junk (PDBs, .ilk, .lib import libraries) is filtered out so
# the distribution folder stays clean; static/import .lib files are not part
# of the runtime distribution.
Get-ChildItem -LiteralPath $buildOutputDirectory -File |
    Where-Object { $_.Extension -notin @('.lib', '.pdb', '.exp', '.ilk', '.iobj', '.ipdb') -and $_.Name -notlike '*.recipe' } |
    ForEach-Object { Copy-IfChanged -Source $_.FullName -TargetDirectory $Destination }

# Kernel build outputs (BusmasterKernel.dll, BusmasterDriverInterface.dll,
# DIL_J1939.dll) are produced by the kernel solution's build folder.
if (Test-Path $kernelBuildDirectory) {
    Get-ChildItem -LiteralPath $kernelBuildDirectory -File -Filter '*.dll' |
        ForEach-Object { Copy-IfChanged -Source $_.FullName -TargetDirectory $Destination }
}

# Generate the Application.version file from BuildVersion.h.
$major = $null
$minor = $null
$build = $null
foreach ($line in Get-Content -LiteralPath $versionHeader) {
    if ($line -match '^\s*#define\s+BUSMASTER_VERSION_MAJOR\s+(\d+)') {
        $major = $Matches[1]
    }
    elseif ($line -match '^\s*#define\s+BUSMASTER_VERSION_MINOR\s+(\d+)') {
        $minor = $Matches[1]
    }
    elseif ($line -match '^\s*#define\s+BUSMASTER_VERSION_BUILD\s+(\d+)') {
        $build = $Matches[1]
    }
}

if ($null -eq $major -or $null -eq $minor -or $null -eq $build) {
    throw "Could not parse version constants from $versionHeader"
}

$version = "$major.$minor.$build"
$versionFile = Join-Path $Destination 'Application.version'
if (-not (Test-Path $versionFile) -or (Get-Content -LiteralPath $versionFile -Raw).Trim() -ne $version) {
    [IO.File]::WriteAllText($versionFile, $version + "`r`n", [Text.UTF8Encoding]::new($false))
    $copiedCount++
}

Write-Host "Deployed $copiedCount file(s) to $Destination (BUSMASTER $version)."