param(
    [ValidateRange(1, 1024)]
    [int]$MaxCpuCount = [Environment]::ProcessorCount
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
        exit $LASTEXITCODE
    }
}
