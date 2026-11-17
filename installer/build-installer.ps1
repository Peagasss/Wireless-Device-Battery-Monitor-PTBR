# Builds dist\WirelessBattery-Setup-<version>.exe, dist\WirelessBattery-<version>-portable.zip,
# and dist\SHA256SUMS.txt, with optional Authenticode signing via scripts\sign-binaries.ps1.
# Needs: WirelessBattery.exe already built (make / build.bat / build-msvc.bat) and Inno Setup 6 or 7 installed.
param(
    [string]$Version = "1.0.0",
    [switch]$RequireSignature
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if ($env:SIGN_REQUIRED -eq "true" -or $env:SIGN_REQUIRED -eq "1") {
    $RequireSignature = [switch]::Present
}

# Bundled HeadsetControl: pinned release, verified against the SHA-256 GitHub publishes for it.
$hscVersion = "4.3.0"
$hscSha256  = "e371da043710ddb8fc70af77f2ccad1cd03f8c6f49ba423e08764ad7a94a3c40"
$hscDir = Join-Path $root "third_party\headsetcontrol"
$hscExe = Join-Path $hscDir "headsetcontrol.exe"
New-Item -ItemType Directory -Force $hscDir | Out-Null
if (-not (Test-Path $hscExe) -or (Get-FileHash $hscExe -Algorithm SHA256).Hash -ne $hscSha256) {
    Write-Host "Downloading HeadsetControl $hscVersion..."
    Invoke-WebRequest "https://github.com/Sapd/HeadsetControl/releases/download/$hscVersion/headsetcontrol-windows-x86_64.exe" -OutFile $hscExe
}
if ((Get-FileHash $hscExe -Algorithm SHA256).Hash -ne $hscSha256) {
    Remove-Item $hscExe
    throw "HeadsetControl checksum mismatch - refusing to bundle it."
}

$mainExe = Join-Path $root "WirelessBattery.exe"
if (-not (Test-Path $mainExe)) { throw "Build WirelessBattery.exe first (make, build.bat, or build-msvc.bat)." }

# Sign application binaries prior to packaging when credentials are configured (or fail if -RequireSignature).
$signScript = Join-Path $root "scripts\sign-binaries.ps1"
$hasSigningCreds = (-not [string]::IsNullOrWhiteSpace($env:AZURE_TRUSTED_SIGNING_DLIB)) -or
                   (-not [string]::IsNullOrWhiteSpace($env:SIGN_PFX_BASE64)) -or
                   (-not [string]::IsNullOrWhiteSpace($env:SIGN_PFX_PATH)) -or
                   (-not [string]::IsNullOrWhiteSpace($env:SIGN_CERT_THUMBPRINT))

if ($hasSigningCreds -or $RequireSignature) {
    & $signScript -RequireSignature:$RequireSignature $mainExe $hscExe
    if ($LASTEXITCODE -ne 0) { throw "Binary signing failed." }
} else {
    Write-Host "[INFO] Building unsigned distribution (no signing credentials configured)." -ForegroundColor Yellow
}

$iscc = foreach ($v in 7, 6) {
    foreach ($base in ${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs") { "$base\Inno Setup $v\ISCC.exe" }
}
$iscc = $iscc | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
if (-not $iscc) { throw "Inno Setup not found (https://jrsoftware.org/isdl.php)." }

$isccArgs = @("/DAppVersion=$Version")
if ($hasSigningCreds) {
    $pwshCmd = (Get-Command pwsh.exe -ErrorAction SilentlyContinue).Source
    if (-not $pwshCmd) { $pwshCmd = "powershell.exe" }
    $isccArgs += "/DEnableCodeSigning=1"
    $isccArgs += "/Sbyscript=$pwshCmd -NoProfile -ExecutionPolicy Bypass -File `"$signScript`" -RequireSignature `$f"
}
$isccArgs += (Join-Path $PSScriptRoot "WirelessBattery.iss")

& $iscc @isccArgs
if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }

# Portable zip: same files, no installer.
$stage = Join-Path $root "build\portable"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $mainExe, $hscExe, installer\THIRD_PARTY_NOTICES.txt $stage
Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
$portableZip = Join-Path $root "dist\WirelessBattery-$Version-portable.zip"
Compress-Archive -Path "$stage\*" -DestinationPath $portableZip -Force

# Verify signatures and generate SHA256SUMS.txt
$setupExe = Join-Path $root "dist\WirelessBattery-Setup-$Version.exe"
$verifyScript = Join-Path $root "scripts\verify-signatures.ps1"
& $verifyScript -RequireValid:$RequireSignature $mainExe $hscExe $setupExe

$sumsPath = Join-Path $root "dist\SHA256SUMS.txt"
$sumsLines = @()
foreach ($artifact in @($setupExe, $portableZip, $mainExe)) {
    if (Test-Path $artifact) {
        $h = (Get-FileHash -Path $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        $leaf = Split-Path $artifact -Leaf
        $sumsLines += "$h  $leaf"
    }
}
Set-Content -Path $sumsPath -Value ($sumsLines -join "`n") -Encoding UTF8
Write-Host "Done: dist\WirelessBattery-Setup-$Version.exe, dist\WirelessBattery-$Version-portable.zip, and dist\SHA256SUMS.txt"
