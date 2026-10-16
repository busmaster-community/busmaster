param(
    [switch]$SkipIxxatDriver
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

    $buildSolutionPath = $solutionPath
    $temporarySolutionPath = $null
    if ($SkipIxxatDriver -and $solution -eq 'Sources\BUSMASTER\BUSMASTER.sln') {
        $solutionContent = [System.IO.File]::ReadAllText($solutionPath)
        $ixxatReleaseBuildMapping = [regex]::new(
            '(?m)^\s*\{F920F538-A4B0-4EB6-81FC-6011E9C1806F\}\.Release\|Win32\.Build\.0 = Release\|Win32\r?\n'
        )

        if (-not $ixxatReleaseBuildMapping.IsMatch($solutionContent)) {
            throw "Could not find the IXXAT Release|Win32 build mapping in $solutionPath"
        }

        $solutionContent = $ixxatReleaseBuildMapping.Replace($solutionContent, '')
        $temporarySolutionPath = Join-Path (Split-Path -Parent $solutionPath) "BUSMASTER.CI.$([guid]::NewGuid().ToString('N')).sln"
        [System.IO.File]::WriteAllText(
            $temporarySolutionPath,
            $solutionContent,
            [System.Text.UTF8Encoding]::new($true)
        )
        $buildSolutionPath = $temporarySolutionPath
        Write-Host 'Skipping CAN_IXXAT_VCI: the IXXAT VCI SDK is not provisioned on CI runners.'
    }

    Write-Host "Building $solution (Release|Win32)"
    try {
        & $msbuildPath $buildSolutionPath /m /nologo /verbosity:minimal `
            '/p:Configuration=Release' `
            '/p:Platform=Win32' `
            '/p:PlatformToolset=v143'
        $buildExitCode = $LASTEXITCODE
    }
    finally {
        if ($temporarySolutionPath) {
            Remove-Item -LiteralPath $temporarySolutionPath
        }
    }

    if ($buildExitCode -ne 0) {
        exit $buildExitCode
    }
}
