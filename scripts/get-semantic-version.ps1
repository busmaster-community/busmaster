$ErrorActionPreference = 'Stop'

$isTagBuild = $env:GITHUB_REF_TYPE -eq 'tag'
if ($isTagBuild) {
    $currentTag = $env:GITHUB_REF_NAME
    if ($currentTag -notmatch '^v(?<major>0|[1-9]\d*)\.(?<minor>0|[1-9]\d*)\.(?<patch>0|[1-9]\d*)$') {
        throw "Release tag must use vMAJOR.MINOR.PATCH, found '$currentTag'."
    }

    $major = [int]$Matches.major
    $minor = [int]$Matches.minor
    $patch = [int]$Matches.patch
    $version = "$major.$minor.$patch"
    $previousTagOutput = & git describe --tags --abbrev=0 --match 'v[0-9]*' "$currentTag^" 2>$null
    $previousTag = if ($LASTEXITCODE -eq 0) { $previousTagOutput.Trim() } else { '' }
    $bump = 'tag'
    $hasRelease = $true
} else {
    $tagOutput = & git describe --tags --abbrev=0 --match 'v[0-9]*' 2>$null
    $hasTag = $LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($tagOutput)
    if ($hasTag) {
        $previousTag = $tagOutput.Trim()
        if ($previousTag -notmatch '^v(?<major>0|[1-9]\d*)\.(?<minor>0|[1-9]\d*)\.(?<patch>0|[1-9]\d*)$') {
            throw "Latest version tag must use vMAJOR.MINOR.PATCH, found '$previousTag'."
        }

        $major = [int]$Matches.major
        $minor = [int]$Matches.minor
        $patch = [int]$Matches.patch
        $range = "$previousTag..HEAD"
    } else {
        $previousTag = ''
        $major = 0
        $minor = 0
        $patch = 0
        $range = 'HEAD'
    }

    $logRecords = (& git log $range '--format=%s%x1f%b%x1e') -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to read commit history for semantic version calculation.'
    }

    $bump = 'none'
    foreach ($record in ($logRecords -split [char]0x1e)) {
        if ([string]::IsNullOrWhiteSpace($record)) {
            continue
        }

        $parts = $record -split [char]0x1f, 2
        $subject = $parts[0].Trim()
        $body = if ($parts.Count -gt 1) { $parts[1] } else { '' }

        if ($subject -match '^[a-z][a-z0-9-]*(?:\([^)]+\))?!:\s+\S' -or
            $body -match '(?im)^BREAKING[ -]CHANGE:\s*\S') {
            $bump = 'major'
            break
        }

        if ($subject -match '^feat(?:\([^)]+\))?:\s+\S') {
            if ($bump -ne 'minor') {
                $bump = 'minor'
            }
            continue
        }

        if ($subject -match '^(?:fix|perf|revert)(?:\([^)]+\))?:\s+\S' -and $bump -eq 'none') {
            $bump = 'patch'
        }
    }

    $hasRelease = $bump -ne 'none'
    switch ($bump) {
        'major' { $major++; $minor = 0; $patch = 0 }
        'minor' { $minor++; $patch = 0 }
        'patch' { $patch++ }
    }

    $version = "$major.$minor.$patch"
}

Write-Host "Previous tag: $(if ($previousTag) { $previousTag } else { '(none)' })"
Write-Host "Detected bump: $bump"
Write-Host "Build version: $version"

if ($env:GITHUB_OUTPUT) {
    Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "version=$version"
    Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "release=$($hasRelease.ToString().ToLowerInvariant())"
    Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "previous_tag=$previousTag"
}
