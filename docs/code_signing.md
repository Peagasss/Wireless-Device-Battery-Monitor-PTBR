# Code Signing & Windows Smart App Control Guide

## 1. Why Windows Smart App Control Blocks Unknown or Self-Signed Builds

**Smart App Control (SAC)** in Windows 11 evaluates every executable (`WirelessBattery.exe`, `headsetcontrol.exe`, `WirelessBattery-Setup-<version>.exe`, and the Inno Setup uninstaller `unins000.exe`) before allowing process creation:

1. **Cloud Reputation (Intelligent Security Graph / ISG):** Windows queries Microsoft's cloud reputation service to determine whether the binary hash or signer has established positive reputation.
2. **Authenticode Digital Signature Verification (`WinVerifyTrust`):** If the binary is newly compiled or lacks sufficient cloud reputation, SAC checks whether the file carries a valid **Authenticode signature** that chains to a Root Certificate Authority in the **Microsoft Trusted Root Program**, countersigned by a trusted **RFC 3161 Timestamp Authority (TSA)**.

### Why Self-Signed Certificates Must Never Be Used for Distribution
- A self-signed certificate (`New-SelfSignedCertificate`) is not rooted in the Microsoft Trusted Root Program.
- Signing an executable with an untrusted root causes `WinVerifyTrust` (`Get-AuthenticodeSignature` / `signtool verify /pa`) to return `CERT_E_UNTRUSTEDROOT` (`UnknownError`).
- On systems with Smart App Control in **Enforced** mode, an invalid or untrusted signature immediately blocks execution and caches a block attribute in NTFS Extended Attributes (`$KERNEL.PURGE.ESBCACHE`).
- For this reason, [`scripts/sign-binaries.ps1`](../scripts/sign-binaries.ps1) validates the full X.509 certificate chain (`X509Chain.Build`) before signing and refuses any self-signed or untrusted certificate.

> [!IMPORTANT]
> A valid Authenticode signature issued by a CA in the Microsoft Trusted Root Program is the primary technical path to establish trust with Windows Smart App Control and SmartScreen, **but it does not guarantee automatic approval in every scenario** (for example, brand-new OV certificates may still go through initial reputation building in Microsoft's Intelligent Security Graph).

---

## 2. Official Code Signing Options for Open-Source Distribution on GitHub

| Option | Trust Model | Storage / CI Integration | Suitability for GitHub OSS |
| :--- | :--- | :--- | :--- |
| **Microsoft Trusted Signing** *(Azure Artifact Signing)* | Short-lived certs chained to *Microsoft Identity Verification Root CA* | Cloud-based via `Azure.CodeSigning.Dlib.dll` (`signtool.exe /dlib`) | **Recommended:** Low cost, immediate Windows root trust, native GitHub Actions support, no physical USB token required. |
| **SignPath.io (Free OSS Program)** | Public CA Code Signing certificate managed in cloud HSM | GitHub Actions integration | **Recommended for community OSS:** Free tier available for qualifying open-source GitHub repositories. |
| **Commercial OV / EV Code Signing Certificate** *(DigiCert, Sectigo, GlobalSign, Certum, SSL.com)* | Public CA in Microsoft Trusted Root Program | Cloud HSM / Azure Key Vault or PKCS#12 (`.pfx`) | Supported directly via `SIGN_PFX_BASE64` / `SIGN_PFX_PASSWORD` or `SIGN_CERT_THUMBPRINT`. |

---

## 3. Executables Covered by the Signing Pipeline

The project's packaging pipeline ([`installer/build-installer.ps1`](../installer/build-installer.ps1) and [`installer/WirelessBattery.iss`](../installer/WirelessBattery.iss)) covers all executable components:

1. `WirelessBattery.exe` — Main application executable (statically linked; no custom `.dll` files are distributed).
2. `third_party\headsetcontrol\headsetcontrol.exe` — Bundled CLI helper (verified against its pinned SHA-256 hash before signing/packaging).
3. `unins000.exe` — Inno Setup uninstaller generated during compilation (`SignedUninstaller=yes` when signing is enabled).
4. `dist\WirelessBattery-Setup-<version>.exe` — Outer Inno Setup installer (`SignTool=byscript`).

---

## 4. Configuring Production Code Signing

### 4.1. Local Production Signing (Environment Variables)

#### Option A: PKCS#12 / PFX Certificate
```powershell
$env:SIGN_PFX_PATH = "C:\secure\path\production-codesign.pfx"
$env:SIGN_PFX_PASSWORD = "<certificate-password>"
$env:SIGN_TIMESTAMP_URL = "http://timestamp.digicert.com"

# Sign WirelessBattery.exe and fail explicitly if signing or verification fails:
powershell -ExecutionPolicy Bypass -File .\scripts\sign-binaries.ps1 -RequireSignature .\WirelessBattery.exe
```

#### Option B: Microsoft Trusted Signing (Dlib)
```powershell
$env:AZURE_TRUSTED_SIGNING_DLIB = "C:\Tools\Azure.CodeSigning.Dlib.dll"
$env:AZURE_TRUSTED_SIGNING_METADATA = "C:\secure\path\metadata.json"

powershell -ExecutionPolicy Bypass -File .\scripts\sign-binaries.ps1 -RequireSignature .\WirelessBattery.exe
```

#### Option C: Windows Certificate Store (Hardware Token / Installed OV/EV Cert)
```powershell
$env:SIGN_CERT_THUMBPRINT = "<40-HEX-DIGIT-SHA1-THUMBPRINT>"

powershell -ExecutionPolicy Bypass -File .\scripts\sign-binaries.ps1 -RequireSignature .\WirelessBattery.exe
```

### 4.2. Configuring GitHub Actions Secrets

In your GitHub repository (**Settings → Secrets and variables → Actions**), configure the following Repository Secrets:

- `SIGN_PFX_BASE64`: Base64-encoded contents of your production `.pfx` certificate (`[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))`).
- `SIGN_PFX_PASSWORD`: Password protecting the `.pfx` file.
- `SIGN_TIMESTAMP_URL` *(optional)*: Custom RFC 3161 timestamp server URL (defaults to `http://timestamp.digicert.com` with automatic fallback to Sectigo and GlobalSign).
- Or, for Microsoft Trusted Signing:
  - `AZURE_TRUSTED_SIGNING_DLIB`
  - `AZURE_TRUSTED_SIGNING_METADATA`

When these secrets are not yet configured, [`.github/workflows/build.yml`](../.github/workflows/build.yml) and [`installer/build-installer.ps1`](../installer/build-installer.ps1) complete the build, run all unit tests, generate `dist\SHA256SUMS.txt`, and explicitly log `[SIGNING SKIPPED] ... Binaries remain UNSIGNED` without pretending the binaries are signed. To enforce mandatory signing in a release environment, pass `-RequireSignature` (or set `SIGN_REQUIRED=true`).

---

## 5. Verifying Signatures and File Integrity

Run [`scripts/verify-signatures.ps1`](../scripts/verify-signatures.ps1) to inspect SHA-256 hashes, Authenticode status, signer certificate chain, and RFC 3161 timestamps:

```powershell
# Inspect current build status (reports Valid vs NotSigned):
powershell -ExecutionPolicy Bypass -File .\scripts\verify-signatures.ps1

# Enforce that all binaries carry a Valid trusted Authenticode signature (exits with code 1 otherwise):
powershell -ExecutionPolicy Bypass -File .\scripts\verify-signatures.ps1 -RequireValid
```

You can also verify manually with the Windows SDK:
```powershell
signtool.exe verify /pa /v .\WirelessBattery.exe
Get-AuthenticodeSignature .\WirelessBattery.exe | Format-List *
```

---

## 6. Limitations for Local and Unsigned Builds

1. **Unsigned GitHub Releases / Downloads:** Files downloaded from the internet via a browser receive the `Zone.Identifier` alternate data stream (*Mark of the Web*). If the binary is unsigned, Smart App Control in Enforced mode will block execution.
2. **Local Builds (`build-msvc.bat` / `build.bat`):** Locally compiled unsigned binaries do not carry a `Zone.Identifier` stream, but overwriting an existing blocked `.exe` in place can preserve cached NTFS Extended Attributes. [`build-msvc.bat`](../build-msvc.bat) deletes any prior `WirelessBattery.exe` before linking to ensure a clean file creation.
