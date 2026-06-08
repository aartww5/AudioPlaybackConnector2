param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$ChangelogPath = "CHANGELOG.md"
)

$ErrorActionPreference = "Stop"

$searchVersion = $Version.TrimStart('v')
$content = [System.IO.File]::ReadAllText($ChangelogPath)

# Match from the version header to the next version header or end of file
$pattern = "(?ms)^## \[$([regex]::Escape($searchVersion))\][^\r\n]*(?:\r?\n)(.*?)(?=^## \[|\z)"
$match = [regex]::Match($content, $pattern)

if (-not $match.Success) {
    throw "Version [$searchVersion] not found in $ChangelogPath."
}

$body = $match.Groups[1].Value.Trim()

if ($env:GITHUB_OUTPUT) {
    $delimiter = "CHANGELOG_EOF_" + [Guid]::NewGuid().ToString("N")
    "BODY<<$delimiter" >> $env:GITHUB_OUTPUT
    $body >> $env:GITHUB_OUTPUT
    "$delimiter" >> $env:GITHUB_OUTPUT
}

$body
