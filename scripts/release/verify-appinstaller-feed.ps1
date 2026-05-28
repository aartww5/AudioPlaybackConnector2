param(
    [Parameter(Mandatory = $true)]
    [string]$AppInstallerUrl,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedPackageVersion,

    [int]$Attempts = 6,
    [int]$DelaySeconds = 10,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
$verified = $false

for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    try {
        $response = Invoke-WebRequest -Uri $AppInstallerUrl -UseBasicParsing -TimeoutSec $TimeoutSeconds -ErrorAction Stop
        $content = if ($response.Content -is [byte[]]) {
            [System.Text.Encoding]::UTF8.GetString($response.Content)
        } else {
            [string]$response.Content
        }

        [xml]$feed = $content
        $appInstallerVersion = $feed.DocumentElement.GetAttribute("Version")
        $packageNode = $feed.DocumentElement.GetElementsByTagName("MainPackage", $feed.DocumentElement.NamespaceURI)[0]
        if (-not $packageNode) {
            throw "MainPackage element not found."
        }

        $actualPackageVersion = $packageNode.GetAttribute("Version")
        if ($appInstallerVersion -eq $ExpectedPackageVersion -and $actualPackageVersion -eq $ExpectedPackageVersion) {
            $verified = $true
            break
        }

        Write-Warning "App Installer feed version is '$appInstallerVersion' and package version is '$actualPackageVersion', expected '$ExpectedPackageVersion'."
    } catch {
        Write-Warning "App Installer feed check failed on attempt ${attempt}: $($_.Exception.Message)"
    }

    if ($attempt -lt $Attempts) {
        Start-Sleep -Seconds $DelaySeconds
    }
}

if (-not $verified) {
    throw "Published App Installer feed did not match package version $ExpectedPackageVersion."
}

Write-Host "App Installer feed verified: $AppInstallerUrl -> $ExpectedPackageVersion"
