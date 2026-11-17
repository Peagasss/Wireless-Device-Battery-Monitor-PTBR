<#
.SYNOPSIS
    Signs Windows executables for Wireless Device Battery Monitor using Authenticode (SHA-256 + RFC 3161 timestamp).

.DESCRIPTION
    Supports production code signing via environment variables / CI secrets:
      1. Microsoft Trusted Signing (Azure Artifact Signing):
         - AZURE_TRUSTED_SIGNING_DLIB     : Path to Azure.CodeSigning.Dlib.dll
         - AZURE_TRUSTED_SIGNING_METADATA : Path to metadata.json
      2. PKCS#12 / PFX Certificate (File or Base64 Secret):
         - SIGN_PFX_BASE64   : Base64-encoded .pfx certificate (decoded to a temporary file and wiped after use)
         - SIGN_PFX_PATH     : Path to a .pfx certificate file
         - SIGN_PFX_PASSWORD : Password for the .pfx certificate
      3. Windows Certificate Store (Hardware Token / Cloud HSM / Installed OV/EV Cert):
         - SIGN_CERT_THUMBPRINT : SHA-1 thumbprint of certificate in CurrentUser\My or LocalMachine\My

    Safety rules:
      - Never uses or accepts self-signed / untrusted-root certificates as production signatures
        (signing with an untrusted root triggers CERT_E_UNTRUSTEDROOT and causes Smart App Control to block the binary).
      - Fails explicitly with exit code 1 when -RequireSignature (or SIGN_REQUIRED=true) is set and
        credentials are missing or signature verification fails.
#>
param(
    [Parameter(Mandatory = $false, ValueFromRemainingArguments = $true)]
    [string[]]$Files,
    [switch]$RequireSignature,
    [string]$TimestampUrl = $env:SIGN_TIMESTAMP_URL
)

$ErrorActionPreference = "Stop"

if ($env:SIGN_REQUIRED -eq "true" -or $env:SIGN_REQUIRED -eq "1") {
    $RequireSignature = [switch]::Present
}

if (-not $Files -or $Files.Count -eq 0) {
    $root = Split-Path $PSScriptRoot -Parent
    $defaultExe = Join-Path $root "WirelessBattery.exe"
    if (Test-Path $defaultExe) {
        $Files = @($defaultExe)
    } else {
        throw "No files specified to sign and WirelessBattery.exe was not found."
    }
}

$timestampServers = @()
if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
    $timestampServers += $TimestampUrl
}
$timestampServers += @(
    "http://timestamp.digicert.com",
    "http://timestamp.sectigo.com",
    "http://timestamp.globalsign.com/tsa/r6advanced1"
)

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path $kitsRoot) {
        $found = Get-ChildItem -Path $kitsRoot -Filter "signtool.exe" -Recurse -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
                 Sort-Object FullName -Descending |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

function Test-CertificateChainTrusted {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2]$Cert)
    if (-not $Cert) { return $false }
    $chain = New-Object System.Security.Cryptography.X509Certificates.X509Chain
    $chain.ChainPolicy.RevocationMode = [System.Security.Cryptography.X509Certificates.X509RevocationMode]::Online
    $chain.ChainPolicy.VerificationFlags = [System.Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
    $isValid = $chain.Build($Cert)
    return $isValid
}

# Determine signing mode from environment variables.
$hasAzureDlib = (-not [string]::IsNullOrWhiteSpace($env:AZURE_TRUSTED_SIGNING_DLIB)) -and
                (-not [string]::IsNullOrWhiteSpace($env:AZURE_TRUSTED_SIGNING_METADATA))
$hasPfxBase64 = -not [string]::IsNullOrWhiteSpace($env:SIGN_PFX_BASE64)
$hasPfxPath   = -not [string]::IsNullOrWhiteSpace($env:SIGN_PFX_PATH)
$hasThumbprint = -not [string]::IsNullOrWhiteSpace($env:SIGN_CERT_THUMBPRINT)

if (-not ($hasAzureDlib -or $hasPfxBase64 -or $hasPfxPath -or $hasThumbprint)) {
    if ($RequireSignature) {
        Write-Error "ERROR: Code signing is required (-RequireSignature / SIGN_REQUIRED=true), but no signing credentials were found in environment variables (AZURE_TRUSTED_SIGNING_*, SIGN_PFX_BASE64, SIGN_PFX_PATH, or SIGN_CERT_THUMBPRINT)."
        exit 1
    }
    Write-Host "[SIGNING SKIPPED] No production code-signing credentials configured. Binaries remain UNSIGNED." -ForegroundColor Yellow
    exit 0
}

$signtool = Find-SignTool
if (-not $signtool) {
    throw "signtool.exe not found. Install the Windows 10/11 SDK Signing Tools."
}

$tempPfx = $null
try {
    if ($hasPfxBase64) {
        $tempPfx = Join-Path ([System.IO.Path]::GetTempPath()) ("wb-sign-" + [Guid]::NewGuid().ToString("N") + ".pfx")
        $bytes = [Convert]::FromBase64String($env:SIGN_PFX_BASE64)
        [System.IO.File]::WriteAllBytes($tempPfx, $bytes)
        $pfxToUse = $tempPfx
    } elseif ($hasPfxPath) {
        if (-not (Test-Path $env:SIGN_PFX_PATH)) {
            throw "SIGN_PFX_PATH does not exist: $env:SIGN_PFX_PATH"
        }
        $pfxToUse = $env:SIGN_PFX_PATH
    }

    # Validate certificate trust before signing so we never apply a self-signed / untrusted root certificate
    # that would cause Smart App Control to block the executable.
    if ($pfxToUse) {
        $flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
        $certObj = if ($env:SIGN_PFX_PASSWORD) {
            New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($pfxToUse, $env:SIGN_PFX_PASSWORD, $flags)
        } else {
            New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($pfxToUse, "", $flags)
        }
        if (-not (Test-CertificateChainTrusted -Cert $certObj)) {
            throw "Refusing to sign: PFX certificate '$($certObj.Subject)' (Issuer: '$($certObj.Issuer)') does not chain to a trusted Root CA in the Windows certificate store. Self-signed certificates trigger Smart App Control blocks."
        }
    } elseif ($hasThumbprint) {
        $cleanThumb = ($env:SIGN_CERT_THUMBPRINT -replace "\s+", "").ToUpperInvariant()
        $certObj = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
                   Where-Object { $_.Thumbprint -eq $cleanThumb } |
                   Select-Object -First 1
        if (-not $certObj) {
            throw "Certificate with thumbprint $cleanThumb was not found in CurrentUser\My or LocalMachine\My."
        }
        if (-not (Test-CertificateChainTrusted -Cert $certObj)) {
            throw "Refusing to sign: Certificate '$($certObj.Subject)' ($cleanThumb) does not chain to a trusted Root CA. Self-signed development certificates must not be used for Windows Smart App Control distribution."
        }
    }

    foreach ($file in $Files) {
        if (-not (Test-Path $file)) {
            throw "Target file to sign does not exist: $file"
        }
        $resolved = (Resolve-Path $file).Path
        Write-Host "Signing $resolved ..." -ForegroundColor Cyan

        $signedOk = $false
        foreach ($tsUrl in $timestampServers) {
            $argsList = @("sign", "/v", "/fd", "SHA256", "/tr", $tsUrl, "/td", "SHA256")
            if ($hasAzureDlib) {
                $argsList += @("/dlib", $env:AZURE_TRUSTED_SIGNING_DLIB, "/dmdf", $env:AZURE_TRUSTED_SIGNING_METADATA)
            } elseif ($pfxToUse) {
                $argsList += @("/f", $pfxToUse)
                if (-not [string]::IsNullOrWhiteSpace($env:SIGN_PFX_PASSWORD)) {
                    $argsList += @("/p", $env:SIGN_PFX_PASSWORD)
                }
            } elseif ($hasThumbprint) {
                $argsList += @("/sha1", ($env:SIGN_CERT_THUMBPRINT -replace "\s+", ""))
            }
            $argsList += $resolved

            & $signtool @argsList
            if ($LASTEXITCODE -eq 0) {
                $signedOk = $true
                break
            }
            Write-Warning "signtool failed with timestamp server $tsUrl (exit code $LASTEXITCODE); trying next server..."
        }

        if (-not $signedOk) {
            throw "Failed to sign $resolved with all configured timestamp servers."
        }

        # Verify Authenticode signature against Windows Default Authentication Policy (/pa)
        & $signtool verify /pa /v $resolved
        if ($LASTEXITCODE -ne 0) {
            throw "Authenticode verification (signtool verify /pa) failed for $resolved."
        }

        $auth = Get-AuthenticodeSignature -FilePath $resolved
        if ($auth.Status -ne "Valid") {
            throw "Get-AuthenticodeSignature reported non-Valid status '$($auth.Status)' ($($auth.StatusMessage)) for $resolved."
        }
        $hash = (Get-FileHash -Path $resolved -Algorithm SHA256).Hash
        Write-Host "[SIGNED & VERIFIED] $resolved (SHA256: $hash, Signer: $($auth.SignerCertificate.Subject))" -ForegroundColor Green
    }
} finally {
    if ($tempPfx -and (Test-Path $tempPfx)) {
        Remove-Item $tempPfx -Force -ErrorAction SilentlyContinue
    }
}
