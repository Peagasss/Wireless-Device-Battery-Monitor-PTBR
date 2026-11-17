# Changelog

All notable changes in this fork (`Peagasss/Wireless-Device-Battery-Monitor-PTBR`) of [Wireless Device Battery Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), organized by git commits and functional milestones.

---

## [Unreleased] - Fork Milestones

### Added

#### Battery Intelligence Engine & Universal Charging Detection (`3804c95`)
- **Centralized Battery Intelligence Engine (`src/battery_intelligence.h`, `src/battery_intelligence.cpp`):**
  - Independent engine architecture processing battery telemetry after all drivers finish querying, without modifying individual driver query paths.
  - Per-device isolated history persisted under `HKCU\Software\WirelessBattery\Devices\<SanitizedKey>`, eliminating cross-device state pollution.
  - Adaptive discharge rate learning (%/hour) using an exponential moving average (EMA) with historical weighting (70% prior, 30% observed) and sanity bounds (0.2%/h to 50.0%/h).
  - Strict tracking of confirmed active usage time: discharge time accumulates only while a device is connected, awake, in use, and running on battery power.
  - Unmonitored gap detection (intervals > 15 minutes) ensuring off-periods, sleep cycles, and offline battery drops recalibrate reference levels without distorting learned active discharge rates.
  - Remaining battery runtime estimation (`runtimeMinutes`), formatted as `~Xh remaining` or `~Xh Ymin remaining` in taskbar tooltips, tray icons, and menu entries.
  - Multi-tier statistical confidence calculation (`Insufficient`, `Low`, `Medium`, `High`) based on sample count, cumulative active usage duration (>= 45 min for Medium, >= 2h for High), and observed rate deviation.
  - Automatic, non-destructive migration and bi-directional synchronization with the legacy `HKCU\Software\WirelessBattery\G435` registry key.
- **Universal Charging State Normalization:**
  - Standardized five-state enum (`Charging`, `Discharging`, `Full`, `Idle`, `Unknown`).
  - Strict visual indicator rule: the charging bolt (`⚡`) appears exclusively when active charging is confirmed by device firmware or OS power reporting.
  - Integration with generic HID Battery System usages (`UsagePage 0x85`: `0x44` Charging, `0x45` Discharging, `0x46` Fully Charged, `0xD0` AC Present).
  - Integration with Windows kernel battery class devices (`GUID_DEVINTERFACE_BATTERY` / `IOCTL_BATTERY_QUERY_STATUS`) to detect external power and charging states on peripheral containers.
  - Full mapping of `Windows.Gaming.Input` `IBatteryReport` statuses (`Charging`, `Discharging`, `Idle`) in the Xbox/XInput driver.
  - Dedicated charging state resolution in vendor drivers (Logitech HID++ 1.0/2.0, Corsair Void & Bragi, Sprime, etc.).
- **Authenticode Code Signing Infrastructure & CI/CD (`scripts/sign-binaries.ps1`, `scripts/verify-signatures.ps1`, `docs/code_signing.md`):**
  - `scripts/sign-binaries.ps1`: Automated Authenticode signing with SHA-256 digest and RFC 3161 timestamps (DigiCert, Sectigo, GlobalSign).
  - Chain-of-trust pre-validation (`X509Chain.Build`) that explicitly rejects self-signed certificates or untrusted roots to prevent Windows Smart App Control blocks (`CERT_E_UNTRUSTEDROOT`).
  - Support for PKCS#12 (`.pfx`) files, base64 environment variables (`SIGN_PFX_BASE64`), certificate store thumbprints (`SIGN_CERT_THUMBPRINT`), and Microsoft Trusted Signing (`Azure.CodeSigning.Dlib.dll`).
  - `scripts/verify-signatures.ps1`: Automated audit tool reporting binary size, SHA-256 hash, Authenticode status, signer subject, issuer, and timestamp details.
  - Packaging integration in `installer/build-installer.ps1` and `installer/WirelessBattery.iss` to sign `WirelessBattery.exe`, bundled `headsetcontrol.exe`, the uninstaller `unins000.exe`, and the setup installer.
  - GitHub Actions workflow update (`.github/workflows/build.yml`) to inject signing secrets and generate `dist/SHA256SUMS.txt`.
  - *Note on prerequisites:* Production Authenticode signing requires an external Code Signing certificate issued by a CA in the Microsoft Trusted Root Program. When credentials are not configured, builds complete unsigned and scripts report unsigned status clearly without spoofing.
- **Automated Unit Test Suites:**
  - `tests/battery_intelligence_test.cpp`: Comprehensive unit tests covering charging state normalization, discharge EMA calculation, remaining runtime math, inactive/disconnection periods, unmonitored gaps, confidence progression, per-device isolation, and registry persistence round-trips.

#### Logitech G435 Adaptive Estimation & Connection Visibility (`d2b731c`)
- **Strict Connection Visibility:**
  - Hides the G435 icon completely from taskbar and tray when disconnected or powered off, rather than assuming connection from USB receiver presence.
  - Active connection detection on the Jieli `4C4A:4155` receiver via WASAPI audio session state (`IAudioSessionControl`) and peak meters (`IAudioMeterInformation`) on G435 USB audio endpoints.
  - Passive background interrupt listener thread on `EP 0x82` (`000C:0001`) capturing volume button events.
- **First-Connection Calibration Prompt:**
  - Displays a one-time guidance notification when first connected via LIGHTSPEED without prior battery calibration: *"Connect your Logitech G435 via Bluetooth to take the first battery reading. After that, we can estimate the charge level during LIGHTSPEED use."*
  - Shows `?` (unknown battery) before calibration, avoiding fabricated percentages.
- **Adaptive LIGHTSPEED Estimation:**
  - Single-reading reference mode: displays the last real Bluetooth reading dimmed (`~XX%`) as a reference without temporal decay.
  - Multi-reading learned mode: calculates real discharge rate from active usage intervals and adaptively decrements the estimate during active use.
  - Recalibration upon reconnection to Bluetooth, resetting reference while refining the learned discharge rate.

#### Logitech G435 Bluetooth HFP Monitoring (`738722f`)
- **Unified Device Identity (`g435`):**
  - Unifies LIGHTSPEED and Bluetooth modes under a single logical key (`"g435"`), preventing duplicate icons when toggling connection modes.
  - Claims USB receiver IDs (`046D:0ADF` and `4C4A:4155`) while connected via Bluetooth so a plugged-in dongle does not spawn a secondary entry.
  - Reserves the Bluetooth container ID in `ctx.btContainers` so generic Bluetooth polling does not duplicate the headset.
- **Bluetooth Hands-Free Profile (HFP) Telemetry:**
  - Queries Windows PnP properties for paired G435 devices matching the `"g435"` name token.
  - Reads the real battery percentage (`0..100%`) from `DEVPKEY_Device_BatteryLevel` on the Hands-Free Audio Gateway service node (`{0000111E-0000-1000-8000-00805F9B34FB}`).
- **Hardware Investigation Documentation:**
  - Preserved USB descriptors, interface configurations, endpoint details, and SDK findings for the Jieli `4C4A:4155` receiver in `docs/logitech_g435.md`.

#### Build, Runtime Settings & Internationalization (`f0ba085`)
- **MSVC Build Support:**
  - Added `build-msvc.bat` with auto-detection of Microsoft Visual Studio 2019/2022/2026 x64 developer tools via `vswhere.exe` and `vcvarsall.bat`.
- **Runtime Settings & Menus:**
  - Added configurable polling intervals (30s, 60s, 2m, 5m).
  - Added configurable low-battery thresholds (10%, 15%, 20%, 25%).
  - Added language selection (Auto, English, Brazilian Portuguese).
  - Settings persisted in `HKCU\Software\WirelessBattery`.
- **Internationalization (`src/i18n.h`):**
  - Added bilingual localization for all menu items, tooltips, and notification messages.
