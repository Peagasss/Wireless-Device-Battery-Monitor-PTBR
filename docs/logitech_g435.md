# Logitech G435 Wireless Gaming Headset (LIGHTSPEED & Bluetooth Unified Support)

## 1. Overview and Current Strategy

The Logitech G435 supports two wireless connection modes: **LIGHTSPEED** (via its USB receiver, model `A00150`) and **Bluetooth**.

In this project, the G435 is represented under a **single unified logical identity** (`key = "g435"`, `name = "Logitech G435"`):
- **Connection Visibility:** The G435 icon is hidden whenever the headset is disconnected, and immediately removed when turned off or disconnected. On the Jieli `4C4A:4155` receiver, mere USB receiver presence is **not** treated as headset connection; instead, active connection and usage are verified via WASAPI audio session/meter activity on the G435 USB audio endpoints, passive Consumer Control input reports on `EP 0x82` (`000C:0001`), or a recent mode transition.
- **Bluetooth mode (Primary real battery telemetry & calibration path):** Reads the real battery percentage (`0–100%`) reported by the headset over the Bluetooth Hands-Free Profile (HFP) and cached by Windows in `DEVPKEY_Device_BatteryLevel` (`{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid 2`), following the approach demonstrated in [jenesei-software/gearhub](https://github.com/jenesei-software/gearhub). Each Bluetooth reading calibrates or recalibrates the persistent estimator in `HKCU\Software\WirelessBattery\G435`.
- **LIGHTSPEED mode via Jieli receiver (`4C4A:4155`) with Adaptive Heuristic Estimation:** Direct battery polling over the Jieli USB receiver is **on hold** (not permanently abandoned; see Section 3 for preserved hardware findings). While connected via LIGHTSPEED on `4C4A:4155`:
  - **Uncalibrated (first connection without any prior Bluetooth reading):** Displays `?` (unknown, `percent = -1`, never a fake percentage) and prompts the user to connect via Bluetooth once to record the initial reference reading.
  - **Calibrated with 1 reading (`ReferenceOnly`):** Displays the last real Bluetooth percentage prefixed with `~` (e.g. `~80%`, dimmed) as a reference without inventing a fixed discharge rate.
  - **Calibrated with 2+ readings (`Estimated`):** Learns the headset's discharge rate from observed battery drop across accumulated active usage time (`dropPct / activeHours`), and estimates the remaining percentage (`~XX%`, dimmed) strictly during confirmed active usage. Battery is never discounted while the headset is off, disconnected, or idle.
- **LIGHTSPEED mode via Holtek receiver (`046D:0ADF`):** Queries battery voltage in millivolts over the `FF00:0001` vendor collection using CRC-16/XMODEM framed Airoha `"PA"` commands.

---

## 2. Unified Identity, Calibration, and Adaptive Estimation (`src/drivers/logitech_g435_estimator.h`)

### 2.1. Bluetooth HFP Detection (`QueryG435Bluetooth`)
1. Enumerates present `BTHENUM\`, `BTHLE\`, and `BTHLEDEVICE\` device nodes grouped by `DEVPKEY_Device_ContainerId`.
2. Matches the device strictly by checking for the standalone alphanumeric token `"g435"` in `DEVPKEY_Device_FriendlyName` / `DEVPKEY_Device_DeviceDesc` (`IsG435BluetoothName`), preventing false associations with other Bluetooth headsets or speakers.
3. Reads the active connection state (`DEVPKEY_Device_IsConnected`, `{83DA6326-97A6-4088-9453-A1923F573B29}, pid 15`) on the base `BTHENUM\DEV_<MAC>` (or `BTHLE\DEV_<MAC>`) node.
4. Reads `DEVPKEY_Device_BatteryLevel` (`{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid 2`, `DEVPROP_TYPE_BYTE`) from the Hands-Free Audio Gateway service node (`{0000111E-0000-1000-8000-00805F9B34FB}`), validating `pct <= 100`.
5. Registers the G435 Bluetooth `ContainerId` in `ctx.btContainers` so `PollBluetooth` in `src/drivers/generic.cpp` never creates a duplicate entry.

### 2.2. Visual Distinction of Reading Types & Registry Persistence (`HKCU\Software\WirelessBattery\G435`)
- **Real Battery (`RealBluetooth`):** Full brightness percentage (`80%`) with detail `"bateria real"` (`"real battery"`).
- **Estimated / Reference Battery (`ReferenceOnly` / `Estimated`):** Dimmed percentage prefixed with `~` (`~75%`) and detail `"referência Bluetooth — aguardando mais leituras para taxa de descarga"` or `"bateria estimada, ref. XX% BT"`.
- **Unknown Battery (`Unknown`):** Dimmed `?` (`percent = -1`) with calibration instruction `"Conecte seu Logitech G435 via Bluetooth para realizar a primeira leitura da bateria. Depois disso, poderemos estimar o nível de carga durante o uso via LIGHTSPEED."`
- **Persistence:** `Calibrated`, `PromptShown`, `LastRealPercent`, `LastRealTimestampSec`, `ActiveUsageSec`, `LearnedRateMilliPctPerHr`, `SampleCount`, and `LastPollTimestampSec` are persisted in `HKCU\Software\WirelessBattery\G435` across application restarts and disconnections.

---

## 3. Preserved Technical Findings — Jieli Receiver (`4C4A:4155`) [On Hold]

These findings are preserved for future reference if a viable USB/LIGHTSPEED battery query method is discovered for the Jieli revision:

### 3.1. Hardware Identification
- **Headset Model:** `A00149`
- **USB Receiver Model:** `A00150` (`P/N 881-000528`)
- **USB Identifiers:** `VID = 0x4C4A` (*Jieli Technology*), `PID = 0x4155` (*G435 Wireless Gaming Headset*), `bcdDevice = 0x0100`, `bcdUSB = 0x0110` (USB 1.1 Full-Speed).

### 3.2. USB Configuration and Interface Layout (`wTotalLength = 240 bytes`)
- **Interface 0:** USB Audio Control 1.0 (85 bytes of class descriptors: 2 Input Terminals, 2 Output Terminals, 2 Selector Units, 2 Feature Units; 0 Extension Units).
- **Interface 1:** USB Audio Streaming OUT (`EP 0x03` Isochronous OUT, 48 kHz 16-bit stereo PCM).
- **Interface 2:** USB Audio Streaming IN (`EP 0x83` Isochronous IN, 48 kHz 16-bit mono PCM).
- **Interface 3:** USB HID 1.00 (`EP 0x82` Interrupt IN, `wMaxPacketSize = 16`, `bInterval = 1 ms`; no Interrupt OUT endpoint).

### 3.3. HID Descriptor and Report Observations
- **Measurement sources:**
  - `UsbTreeView` reports `wDescriptorLength = 51 bytes` (`0x0033`) in the 9-byte `0x21` HID descriptor, while direct retrieval of the `0x22` Report Descriptor via the hub IOCTL returned `ERROR_INVALID_PARAMETER`.
  - A previously captured raw HID descriptor dump measured `90 bytes`.
  - Windows `HidP_GetCaps` on the parsed collection reports `UsagePage = 0x000C` (*Consumer*), `Usage = 0x0001` (*Consumer Control*), `InputReportByteLength = 3`, `OutputReportByteLength = 0`, `FeatureReportByteLength = 0`, with 16 1-bit input buttons (`DataIdx 0..15`).
- **Observed traffic on `EP 0x82`:**
  - Pressing Volume + emits `[00 01 00]` followed by `[00 00 00]`.
  - Pressing Volume - emits `[00 02 00]` followed by `[00 00 00]`.
  - Pressing Mute, toggling headset power, or connecting a USB-C charging cable produced no HID input reports during passive capture.

### 3.4. Comparison with Other G435 Revisions (`046D:0ACB` and `046D:0ADF`)
- **`046D:0ACB` ([deddroid/logitech-g435-battery](https://github.com/deddroid/logitech-g435-battery)):** Realtek `ALC4021` USB bridge + Airoha `PAU1823` RF SoC. Uses vendor collection `0xFFC0` (Report ID `6`, 515-byte output reports + 64-byte feature reports) and enters a bridge bootloader mode (`06 01 03`) that interrupts audio for ~0.6 s per read.
- **`046D:0ADF` ([portyporty/g435-battery-percentage](https://github.com/portyporty/g435-battery-percentage)):** Holtek USB bridge + Airoha RF SoC. Uses vendor collection `0xFF00:0001` (64-byte IN/OUT reports with CRC-16/XMODEM framing) to tunnel Airoha `"PA"` (`50 41`) commands (`GET_BATTERY = 00 31`) and receive `"PI"` (`50 49`) replies in millivolts.
- **`4C4A:4155`:** Uses a Zhuhai Jieli Technology SoC without the `0xFFC0` or `0xFF00` vendor HID collections and without the Airoha `"PA"`/`"PI"` stack. In Jieli's official SDKs (`AW30N` / `fw-AC63_BT_SDK`), PC-to-dongle RCSP telemetry (`0x4A4C ... 0xED`) uses a separate `custom_hid` interface (`UsagePage 0xFF02`, 2 endpoints), which is not enabled in the `4C4A:4155` descriptor set.

### 3.5. Future Investigation Paths (On Hold)
- Determining whether the `4C4A:4155` firmware implements any custom handler on Endpoint 0 (`user_setup_filter` / `hid_recv_output_report`) would require inspecting a firmware binary dump from the receiver or capturing traffic from a compatible factory/updater tool before issuing any custom control transfers.
