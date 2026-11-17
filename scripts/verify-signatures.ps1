<#
.SYNOPSIS
    Verifies file integrity (SHA-256) and Authenticode signature status of Wireless Battery executables.

.DESCRIPTION
    Inspects WirelessBattery.exe, bundled third-party binaries, and installer artifacts in dist\.
    Never reports an unsigned or self-signed binary as trusted.
    Pass -RequireValid to fail with exit code 1 if any checked executable lacks a valid trusted Authenticode signature.
#>
param(
    [Parameter(Mandatory = $false, ValueFromRemainingArguments = $true)]
    [string[]]$Files,
    [switch]$RequireValid
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if ($env:SIGN_REQUIRED -eq "true" -or $env:SIGN_REQUIRED -eq "1") {
    $RequireValid = [switch]::Present
}

if (-not $Files -or $Files.Count -eq 0) {
    $candidates = @(
        (Join-Path $root "WirelessBattery.exe"),
        (Join-Path $root "third_party\headsetcontrol\headsetcontrol.exe")
    )
    $distDir = Join-Path $root "dist"
    if (Test-Path $distDir) {
        $candidates += Get-ChildItem -Path $distDir -Filter "*.exe" | Select-Object -ExpandProperty FullName
    }
    $Files = $candidates | Where-Object { Test-Path $_ }
}

if (-not $Files -or $Files.Count -eq 0) {
    throw "No executable files found to verify."
}

$anyInvalid = $false
Write-Host "=== Binary Integrity & Authenticode Verification ===" -ForegroundColor Cyan

foreach ($file in $Files) {
    if (-not (Test-Path $file)) {
        Write-Error "File not found: $file"
        $anyInvalid = $true
        continue
    }
    $resolved = (Resolve-Path $file).Path
    $item = Get-Item $resolved
    $hash = (Get-FileHash -Path $resolved -Algorithm SHA256).Hash
    $sig = Get-AuthenticodeSignature -FilePath $resolved

    $signerSubj = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { "None" }
    $signerIssuer = if ($sig.SignerCertificate) { $sig.SignerCertificate.Issuer } else { "None" }
    $timestamper = if ($sig.TimeStamperCertificate) { $sig.TimeStamperCertificate.Subject } else { "None" }

    Write-Host ("File      : {0} ({1} bytes)" -f $item.Name, $item.Length)
    Write-Host ("SHA-256   : {0}" -f $hash)
    Write-Host ("Status    : {0} ({1})" -f $sig.Status, $sig.StatusMessage)
    Write-Host ("Signer    : {0}" -f $signerSubj)
    Write-Host ("Issuer    : {0}" -f $signerIssuer)
    Write-Host ("Timestamp : {0}" -f $timestamper)
    Write-Host "----------------------------------------------------"

    if ($sig.Status -ne "Valid") {
        $anyInvalid = $true
    }
}

if ($RequireValid -and $anyInvalid) {
    Write-Error "Verification failed: one or more executables do not carry a Valid trusted Authenticode signature."
    exit 1
}
