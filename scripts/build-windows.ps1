param(
    [ValidateRange(1, 1024)]
    [int]$MaxCpuCount = [Environment]::ProcessorCount,
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$msbuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$msbuildPath = if ($msbuildCommand) { $msbuildCommand.Source } else { $null }

if (-not $msbuildPath) {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswherePath = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (Test-Path $vswherePath) {
        $msbuildPath = & $vswherePath -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}

if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    throw 'MSBuild was not found. Install Visual Studio 2022 with the C++ desktop workload or add MSBuild to PATH.'
}

$solutions = @(
    'Sources\Kernel\BusmasterKernel.sln',
    'Sources\BUSMASTER\BUSMASTER.sln'
)

$versionHeader = Join-Path $repositoryRoot 'Sources\BUSMASTER\Application\BuildVersion.h'
$originalVersionHeader = $null

try {
    if ($Version) {
        $versionParts = $Version.Split('.') | ForEach-Object { [int]$_ }
        if ($versionParts | Where-Object { $_ -gt 255 }) {
            throw 'Each version component must be between 0 and 255 for the application version constants.'
        }

        $originalVersionHeader = [IO.File]::ReadAllText($versionHeader)
        $generatedVersionHeader = @"
#pragma once
#define BUSMASTER_VERSION_MAJOR $($versionParts[0])
#define BUSMASTER_VERSION_MINOR $($versionParts[1])
#define BUSMASTER_VERSION_BUILD $($versionParts[2])
#define BUSMASTER_VERSION_DISPLAY "Ver $Version"
"@
    $generatedVersionHeader += "`r`n"
        [IO.File]::WriteAllText($versionHeader, $generatedVersionHeader, [Text.UTF8Encoding]::new($false))
    }

    foreach ($solution in $solutions) {
        $solutionPath = Join-Path $repositoryRoot $solution
        if (-not (Test-Path $solutionPath)) {
            throw "Build solution not found: $solutionPath"
        }

        Write-Host "Building $solution (Release|Win32; MSBuild nodes: $MaxCpuCount)"
        & $msbuildPath $solutionPath "/m:$MaxCpuCount" /nologo /verbosity:minimal `
            '/p:Configuration=Release' `
            '/p:Platform=Win32' `
            '/p:PlatformToolset=v143'

        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed for $solution with exit code $LASTEXITCODE."
        }
    }

    Write-Host 'Deploying runtime bundle to Sources\BUSMASTER\BIN\Release'
    & (Join-Path $PSScriptRoot 'deploy-runtime.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "deploy-runtime.ps1 failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ($null -ne $originalVersionHeader) {
        [IO.File]::WriteAllText($versionHeader, $originalVersionHeader, [Text.UTF8Encoding]::new($false))
    }
}
