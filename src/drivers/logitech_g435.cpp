// Logitech G435 Wireless Gaming Headset — unified LIGHTSPEED & Bluetooth (HFP) driver
// with adaptive Bluetooth-calibrated battery estimation and connection visibility.
//
// Unifies the Logitech G435 under a single logical identity (key "g435", name "Logitech G435")
// across both connection modes so the headset is never duplicated when switching modes:
//
//  1) Bluetooth mode (Hands-Free Profile / HFP, GearHub approach):
//     When the user connects the G435 via Bluetooth, Windows receives the real battery percentage
//     (0..100%) over the Hands-Free Audio Gateway service ({0000111E-0000-1000-8000-00805F9B34FB})
//     and stores it in DEVPKEY_Device_BatteryLevel ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid 2)
//     as a DEVPROP_TYPE_BYTE. Connection state is verified on the base BTHENUM\DEV_<MAC> (or
//     BTHLE\DEV_<MAC>) node via DEVPKEY_Device_IsConnected ({83DA6326-97A6-4088-9453-A1923F573B29},
//     pid 15). Each valid Bluetooth reading calibrates/recalibrates the persistent estimator in
//     HKCU\Software\WirelessBattery\G435 and updates the learned discharge rate from real usage.
//
//  2) LIGHTSPEED mode via Jieli receiver (VID 0x4C4A, PID 0x4155):
//     Direct battery polling over this LIGHTSPEED receiver is currently on hold (not permanently
//     abandoned; all descriptor, endpoint, and SDK findings are preserved in docs/logitech_g435.md).
//     Because the USB receiver keeps its USB interfaces statically enumerated while plugged in,
//     mere USB receiver presence is NOT treated as headset connection. Instead, LIGHTSPEED
//     connection and active usage are detected via WASAPI audio session/meter activity on the G435
//     USB audio endpoints, passive Consumer Control input reports on EP 0x82 (000C:0001), and
//     recent mode transitions. While connected via LIGHTSPEED:
//       - Uncalibrated (no prior Bluetooth reading): displays Unknown ("?") and prompts the user
//         to connect via Bluetooth once to record the initial reference reading.
//       - Calibrated (1 reading): displays the last real Bluetooth percentage as a reference ("~XX%").
//       - Calibrated (2+ readings with learned discharge rate): displays an adaptive heuristic
//         estimate ("~XX%") based strictly on accumulated active usage time.
//
//  3) LIGHTSPEED mode via Holtek receiver (VID 0x046D, PID 0x0ADF):
//     Queries battery millivolts over the vendor collection (FF00:0001, 64-byte CRC-16/XMODEM
//     frames carrying Airoha "PA" GET_BATTERY commands).
#include "drivers.h"
#include "logitech_g435_0adf.h"
#include "logitech_g435_estimator.h"
#include "../i18n.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <dbt.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <cwctype>
#include <map>
#include <mutex>

namespace {

constexpr BYTE kCatOta = 0x06;
constexpr BYTE kCatWireless = 0x0E;
constexpr BYTE kGetMode = 0xE0;
constexpr BYTE kSetMode = 0xE1;

const wchar_t* kG435RegKey = L"Software\\WirelessBattery\\G435";

// DEVPKEY_Device_BatteryLevel: {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid 2 (DEVPROP_TYPE_BYTE, 0..100)
const DEVPROPKEY kPropBatteryLevel = {
    { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2
};

// DEVPKEY_Device_IsConnected: {83DA6326-97A6-4088-9453-A1923F573B29}, pid 15 (DEVPROP_TYPE_BOOLEAN)
const DEVPROPKEY kPropIsConnected = {
    { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15
};

// Bluetooth Hands-Free Audio Gateway service UUID marker in BTHENUM instance IDs (GearHub approach).
constexpr const wchar_t* kHfpServiceUuid = L"{0000111e-0000-1000-8000-00805f9b34fb}";

// Session init from the 0ADF capture (OTA_CMD_IOCTL_READ_MAX_LEN, sequence 1 embedded).
const std::vector<BYTE> kSessionInit = { 0x50, 0x41, 0x06, 0x01, 0x0D, 0x00, 0x25, 0x34, 0x12, 0x5A,
                                         0x5A, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00 };

bool Transact(HidDev& d, BYTE cmd, const std::vector<BYTE>& body, int waitMs, int* mv = nullptr) {
    auto f = G435Adf::Frame(cmd, body);
    std::vector<BYTE> out;
    out.push_back(0x00);
    out.insert(out.end(), f.begin(), f.end());
    d.Flush();
    if (!d.Write(out)) return false;
    ULONGLONG end = GetTickCount64() + waitMs;
    std::vector<BYTE> r;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return true;
        if (d.Read(r, left) < 0) continue;
        if (mv && (*mv = G435Adf::ParseMillivolts(r.data(), r.size())) >= 0) return true;
    }
}

struct SessionGuard {
    HidDev& d;
    ~SessionGuard() { Transact(d, 0x85, { 0x02 }, 50); }
};

int ReadMillivoltsAdf(HidDev& d) {
    if (!Transact(d, 0x84, { 0x01 }, 50)) return -1;
    SessionGuard guard{ d };
    Transact(d, 0x85, { 0x01 }, 50);
    Transact(d, 0x80, kSessionInit, 450);
    BYTE seq = 2;
    auto pa = [&](BYTE cat, BYTE len, std::initializer_list<BYTE> payload) {
        auto cmd = G435Adf::Pa(cat, seq, len, payload);
        seq = (BYTE)(seq % 127 + 1);
        return cmd;
    };
    Transact(d, 0x80, pa(kCatWireless, 1, { kGetMode }), 120);
    Transact(d, 0x80, pa(kCatWireless, 2, { kSetMode, 0x01 }), 120);
    Transact(d, 0x80, pa(kCatWireless, 1, { kGetMode }), 120);
    int mv = -1;
    Transact(d, 0x80, pa(kCatOta, 1, { 0x00, 0x31 }), 250, &mv);
    return mv;
}

std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
    return s;
}

// Strict token match for "g435" so no unrelated Bluetooth or USB device is ever associated with the G435.
bool IsG435BluetoothName(const std::wstring& name) {
    std::wstring low = ToLower(name);
    size_t pos = 0;
    while ((pos = low.find(L"g435", pos)) != std::wstring::npos) {
        bool leftOk = (pos == 0) || !iswalnum(low[pos - 1]);
        size_t end = pos + 4;
        bool rightOk = (end >= low.size()) || !iswalnum(low[end]);
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

// ---- Registry Persistence for G435Estimator::State --------------------------------------------

G435Estimator::State LoadEstimatorState() {
    return LoadDeviceBatteryHistory(L"g435");
}

void SaveEstimatorState(const G435Estimator::State& st) {
    SaveDeviceBatteryHistory(L"g435", st);
}

// ---- Passive HID Interrupt IN Monitor & WASAPI Audio Activity for Jieli 4C4A:4155 -------------

std::atomic<ULONGLONG> g_lastHidEventTick{ 0 };
std::mutex g_hidMonMutex;
std::wstring g_hidMonPath;
HANDLE g_hidMonThread = nullptr;

DWORD WINAPI JieliHidMonitorThread(LPVOID) {
    for (;;) {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(g_hidMonMutex);
            path = g_hidMonPath;
        }
        if (path.empty()) {
            Sleep(1000);
            continue;
        }

        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            Sleep(2000);
            continue;
        }

        HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BYTE buf[16] = {};
        while (evt != nullptr) {
            OVERLAPPED ov{};
            ov.hEvent = evt;
            ResetEvent(evt);
            DWORD got = 0;
            BOOL ok = ReadFile(h, buf, 3, &got, &ov);
            if (!ok) {
                if (GetLastError() != ERROR_IO_PENDING) break;
                DWORD w = WaitForSingleObject(evt, INFINITE);
                if (w != WAIT_OBJECT_0 || !GetOverlappedResult(h, &ov, &got, FALSE)) break;
            }
            if (got > 0) {
                g_lastHidEventTick.store(GetTickCount64());
                if (HWND wnd = FindWindowW(L"WirelessBatteryWnd", nullptr)) {
                    PostMessageW(wnd, WM_DEVICECHANGE, DBT_DEVICEARRIVAL, 0);
                }
            }
        }
        if (evt) CloseHandle(evt);
        CloseHandle(h);
        Sleep(1000);
    }
}

void EnsureJieliHidMonitor(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_hidMonMutex);
    g_hidMonPath = path;
    if (!g_hidMonThread && !path.empty()) {
        g_hidMonThread = CreateThread(nullptr, 0, JieliHidMonitorThread, nullptr, 0, nullptr);
    }
}

struct G435UsbAudioState {
    bool endpointPresent = false;
    bool sessionActive   = false;
    bool peakActive      = false;
};

// Inspects active Windows audio render/capture endpoints belonging to the G435 USB receiver
// (excluding Bluetooth BTHENUM/BTHHFENUM endpoints) to detect active audio sessions or signal peaks.
G435UsbAudioState QueryG435UsbAudioActivity() {
    G435UsbAudioState out;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hrInit);

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum));
    if (SUCCEEDED(hr) && pEnum) {
        IMMDeviceCollection* pColl = nullptr;
        if (SUCCEEDED(pEnum->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &pColl)) && pColl) {
            UINT count = 0;
            pColl->GetCount(&count);
            for (UINT i = 0; i < count; i++) {
                IMMDevice* pDev = nullptr;
                if (FAILED(pColl->Item(i, &pDev)) || !pDev) continue;

                IPropertyStore* pProps = nullptr;
                std::wstring friendlyName;
                if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                        friendlyName = pv.pwszVal;
                    }
                    PropVariantClear(&pv);
                    pProps->Release();
                }

                // Exclude Bluetooth audio endpoints (which contain "Hands-Free" or Bluetooth adapter names)
                std::wstring lowName = ToLower(friendlyName);
                bool isBtAudio = (lowName.find(L"hands-free") != std::wstring::npos) ||
                                 (lowName.find(L"bth") != std::wstring::npos);

                if (IsG435BluetoothName(friendlyName) && !isBtAudio) {
                    out.endpointPresent = true;

                    IAudioMeterInformation* pMeter = nullptr;
                    if (SUCCEEDED(pDev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_INPROC_SERVER,
                                                 nullptr, reinterpret_cast<void**>(&pMeter))) && pMeter) {
                        float peak = 0.0f;
                        if (SUCCEEDED(pMeter->GetPeakValue(&peak)) && peak > 0.0001f) {
                            out.peakActive = true;
                        }
                        pMeter->Release();
                    }

                    IAudioSessionManager2* pSessMgr = nullptr;
                    if (SUCCEEDED(pDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                                                 nullptr, reinterpret_cast<void**>(&pSessMgr))) && pSessMgr) {
                        IAudioSessionEnumerator* pSessEnum = nullptr;
                        if (SUCCEEDED(pSessMgr->GetSessionEnumerator(&pSessEnum)) && pSessEnum) {
                            int sessCount = 0;
                            pSessEnum->GetCount(&sessCount);
                            for (int s = 0; s < sessCount; s++) {
                                IAudioSessionControl* pCtrl = nullptr;
                                if (SUCCEEDED(pSessEnum->GetSession(s, &pCtrl)) && pCtrl) {
                                    AudioSessionState st = AudioSessionStateInactive;
                                    if (SUCCEEDED(pCtrl->GetState(&st)) && st == AudioSessionStateActive) {
                                        out.sessionActive = true;
                                    }
                                    pCtrl->Release();
                                }
                            }
                            pSessEnum->Release();
                        }
                        pSessMgr->Release();
                    }
                }
                pDev->Release();
            }
            pColl->Release();
        }
        pEnum->Release();
    }

    if (needUninit) CoUninitialize();
    return out;
}

// ---- Bluetooth (HFP / DEVPKEY_Device_BatteryLevel) Query --------------------------------------

std::wstring ReadDeviceStringProp(HDEVINFO di, SP_DEVINFO_DATA* dd, const DEVPROPKEY& key) {
    DEVPROPTYPE t = 0;
    wchar_t buf[256] = {};
    if (SetupDiGetDevicePropertyW(di, dd, &key, &t, (PBYTE)buf, sizeof(buf) - sizeof(wchar_t), nullptr, 0) &&
        t == DEVPROP_TYPE_STRING) {
        return buf;
    }
    return L"";
}

struct G435BtState {
    bool paired = false;
    bool connected = false;
    int percent = -1;            // 0..100 when validated from DEVPKEY_Device_BatteryLevel, -1 otherwise
    bool fromHfpService = false; // true when read from Hands-Free AG {0000111E-...} node
    std::wstring friendlyName;
    std::wstring containerId;
    std::wstring hfpInstanceId;
};

// Queries Windows PnP / BTHENUM for a paired Logitech G435 Bluetooth headset and reads its
// real battery percentage from the Hands-Free (HFP) service node property DEVPKEY_Device_BatteryLevel.
G435BtState QueryG435Bluetooth() {
    G435BtState out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;

    struct ContainerInfo {
        std::wstring name;
        bool isG435 = false;
        bool hasMainConn = false;
        bool mainConnected = false;
        int hfpPercent = -1;
        int fallbackPercent = -1;
        std::wstring hfpInst;
    };

    std::map<std::wstring, ContainerInfo> containers;
    SP_DEVINFO_DATA dd{ sizeof(dd) };

    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        if (wcsncmp(inst, L"BTHENUM\\", 8) != 0 &&
            wcsncmp(inst, L"BTHLE\\", 6) != 0 &&
            wcsncmp(inst, L"BTHLEDEVICE\\", 12) != 0) {
            continue;
        }

        GUID guid{};
        DEVPROPTYPE t = 0;
        std::wstring cid;
        if (SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &t, (PBYTE)&guid, sizeof(guid), nullptr, 0) &&
            t == DEVPROP_TYPE_GUID) {
            wchar_t gbuf[64] = {};
            StringFromGUID2(guid, gbuf, 64);
            cid = gbuf;
        }
        if (cid.empty()) continue;

        auto& ci = containers[cid];
        std::wstring name = ReadDeviceStringProp(di, &dd, DEVPKEY_Device_FriendlyName);
        if (name.empty()) name = ReadDeviceStringProp(di, &dd, DEVPKEY_Device_DeviceDesc);
        if (IsG435BluetoothName(name)) {
            ci.isG435 = true;
            if (ci.name.empty()) ci.name = name;
        }

        // Base device node (BTHENUM\DEV_<MAC> or BTHLE\DEV_<MAC>): authoritative connection state and name.
        if (wcsncmp(inst, L"BTHENUM\\DEV_", 12) == 0 || wcsncmp(inst, L"BTHLE\\DEV_", 10) == 0) {
            if (!name.empty()) ci.name = name;
            if (IsG435BluetoothName(name)) ci.isG435 = true;
            DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
            if (SetupDiGetDevicePropertyW(di, &dd, &kPropIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) &&
                t == DEVPROP_TYPE_BOOLEAN) {
                ci.hasMainConn = true;
                ci.mainConnected = (conn != DEVPROP_FALSE);
            }
        }

        // Read DEVPKEY_Device_BatteryLevel if present on this node.
        ULONG st = 0, prob = 0;
        if (CM_Get_DevNode_Status(&st, &prob, dd.DevInst, 0) == CR_SUCCESS && (st & DN_HAS_PROBLEM)) continue;

        BYTE pct = 0xFF;
        if (SetupDiGetDevicePropertyW(di, &dd, &kPropBatteryLevel, &t, &pct, sizeof(pct), nullptr, 0) &&
            t == DEVPROP_TYPE_BYTE && pct <= 100) {
            std::wstring lowInst = ToLower(inst);
            if (lowInst.find(kHfpServiceUuid) != std::wstring::npos) {
                ci.hfpPercent = (int)pct;
                ci.hfpInst = inst;
            } else if (ci.fallbackPercent < 0) {
                ci.fallbackPercent = (int)pct;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(di);

    for (const auto& [cid, ci] : containers) {
        if (!ci.isG435) continue;
        out.paired = true;
        out.containerId = cid;
        out.friendlyName = ci.name;
        out.connected = ci.hasMainConn ? ci.mainConnected : false;
        if (ci.hfpPercent >= 0) {
            out.percent = ci.hfpPercent;
            out.fromHfpService = true;
            out.hfpInstanceId = ci.hfpInst;
        } else if (ci.fallbackPercent >= 0) {
            out.percent = ci.fallbackPercent;
        }
        if (out.connected) break;  // prefer an actively connected G435 container
    }

    return out;
}

// Runtime transition tracking across polls (worker thread).
bool g_firstPollDone   = false;
bool g_wasBtConnected  = false;
bool g_wasJieliPresent = false;
ULONGLONG g_lastTransitionTick = 0;

}  // namespace

void PollLogitechG435(PollContext& ctx) {
    const std::uint64_t nowSec = static_cast<std::uint64_t>(std::time(nullptr));
    const ULONGLONG nowTick = GetTickCount64();
    G435Estimator::State estState = LoadEstimatorState();

    // 1. Check Bluetooth (HFP / DEVPKEY_Device_BatteryLevel) for the G435 first.
    G435BtState bt = QueryG435Bluetooth();
    if (!bt.containerId.empty()) {
        // Reserve the G435 Bluetooth container so PollBluetooth in generic.cpp never duplicates it.
        ctx.btContainers.insert(bt.containerId);
    }

    auto jieliColls = ctx.Find(0x4C4A, 0x4155, [](const HidColl& c) {
        return c.page() == 0x000C && c.usage() == 0x0001;
    });
    const bool jieliPresent = !jieliColls.empty();
    EnsureJieliHidMonitor(jieliPresent ? jieliColls[0]->path : L"");

    if (g_firstPollDone) {
        // Detect user switching headset from Bluetooth back to LIGHTSPEED, or hotplugging the USB receiver.
        if ((g_wasBtConnected && !bt.connected && jieliPresent) || (!g_wasJieliPresent && jieliPresent)) {
            g_lastTransitionTick = nowTick;
        }
    }
    g_firstPollDone = true;
    g_wasBtConnected = (bt.paired && bt.connected);
    g_wasJieliPresent = jieliPresent;

    // If the G435 is actively connected over Bluetooth right now, report the unified device in
    // Bluetooth mode with its real HFP battery percentage, update calibration/learning in the
    // Registry, and claim the USB dongle IDs so a plugged-in receiver never creates a second entry.
    if (bt.paired && bt.connected) {
        g_lastTransitionTick = 0;
        ctx.claimed.insert({ 0x046D, 0x0ADF });
        ctx.claimed.insert({ 0x4C4A, 0x4155 });

        bool dirty = false;
        if (bt.percent >= 0) {
            dirty |= G435Estimator::RecordBluetoothReading(estState, bt.percent, nowSec);
        } else {
            dirty |= G435Estimator::AccumulateUsage(estState, true, nowSec);
        }
        if (dirty) SaveEstimatorState(estState);

        DeviceStatus s;
        s.key = L"g435";
        s.name = L"Logitech G435";
        s.kind = DeviceKind::Headset;
        s.via = L"Bluetooth";
        s.connected = true;
        s.estimated = false;
        s.chargeState = ChargingState::Unknown;
        s.activityState = ActivityState::Active;
        s.percent = bt.percent;  // real 0..100% from DEVPKEY_Device_BatteryLevel
        if (bt.percent >= 0) {
            s.detail = Tr(StrId::G435BatteryRealBt);
        }
        ctx.out.push_back(s);
        return;
    }

    // 2. Holtek 046D:0ADF LIGHTSPEED receiver (vendor collection FF00:0001, 64-byte reports).
    if (!ctx.IsClaimed(0x046D, 0x0ADF)) {
        auto colls = ctx.Find(0x046D, 0x0ADF, [](const HidColl& c) {
            return c.page() == 0xFF00 && c.usage() == 0x0001 && c.inLen() == 65 && c.outLen() == 65;
        });
        for (const auto* coll : colls) {
            HidDev d;
            if (!d.Open(*coll)) continue;

            int mv = ReadMillivoltsAdf(d);
            ctx.claimed.insert({ 0x046D, 0x0ADF });
            if (mv < 0) {
                // Headset is turned off / disconnected from the 0ADF receiver: hide the icon.
                if (G435Estimator::AccumulateUsage(estState, false, nowSec)) SaveEstimatorState(estState);
                return;
            }

            int realPct = G435Adf::Percent(mv);
            if (G435Estimator::RecordBluetoothReading(estState, realPct, nowSec)) SaveEstimatorState(estState);

            DeviceStatus s;
            s.key = L"g435";
            s.name = L"Logitech G435";
            s.kind = DeviceKind::Headset;
            s.via = L"LIGHTSPEED";
            s.millivolts = mv;
            s.percent = realPct;
            s.connected = true;
            s.estimated = false;
            ctx.out.push_back(s);
            return;
        }
    }

    // 3. Jieli 4C4A:4155 LIGHTSPEED receiver.
    // Do NOT treat the mere presence of the USB receiver as proof that the headset is connected.
    // Verify active connection/usage via WASAPI USB audio session/meter activity, passive EP 0x82
    // HID button events, or a recent mode switch / hotplug transition.
    if (!ctx.IsClaimed(0x4C4A, 0x4155) && jieliPresent) {
        ctx.claimed.insert({ 0x4C4A, 0x4155 });

        G435UsbAudioState audio = QueryG435UsbAudioActivity();
        ULONGLONG lastHid = g_lastHidEventTick.load();
        bool recentHidButton  = (lastHid > 0) && (nowTick >= lastHid) && (nowTick - lastHid <= 90 * 1000ULL);
        bool recentTransition = (g_lastTransitionTick > 0) && (nowTick >= g_lastTransitionTick) &&
                                (nowTick - g_lastTransitionTick <= 90 * 1000ULL);

        bool activelyInUse = audio.peakActive || audio.sessionActive || recentHidButton;
        bool lightspeedConnected = activelyInUse || recentTransition;

        if (!lightspeedConnected) {
            // Headset is disconnected or turned off: do not discount battery and do not show the icon.
            if (G435Estimator::AccumulateUsage(estState, false, nowSec)) SaveEstimatorState(estState);
            return;
        }

        // Accumulate usage strictly when there is evidence of active use (audio stream/peak or button events).
        bool dirty = G435Estimator::AccumulateUsage(estState, activelyInUse, nowSec);
        G435Estimator::Evaluation ev = G435Estimator::EvaluateLightspeed(estState);
        if (ev.showCalibrationPrompt) dirty = true;
        if (dirty) SaveEstimatorState(estState);

        DeviceStatus s;
        s.key = L"g435";
        s.name = L"Logitech G435";
        s.kind = DeviceKind::Headset;
        s.via = L"LIGHTSPEED";
        s.connected = true;
        s.percent = ev.percent;
        s.estimated = ev.estimated;
        s.chargeState = ChargingState::Unknown;
        s.activityState = activelyInUse ? ActivityState::Active : ActivityState::Idle;
        s.confidence = ev.confidence;
        s.runtimeMinutes = ev.runtimeMinutes;
        if (ev.showCalibrationPrompt) {
            s.calibrateMsg = Tr(StrId::G435CalibratePrompt);
        }

        switch (ev.kind) {
            case G435Estimator::ReadingKind::Unknown:
                s.detail = Tr(StrId::G435CalibratePrompt);
                break;
            case G435Estimator::ReadingKind::ReferenceOnly:
                s.levelText = L"~" + std::to_wstring(ev.percent) + L"%";
                s.detail = Tr(StrId::G435BatteryReferenceBt);
                break;
            case G435Estimator::ReadingKind::Estimated:
                s.levelText = L"~" + std::to_wstring(ev.percent) + L"%";
                s.detail = std::wstring(Tr(StrId::G435BatteryEstimated)) + L", ref. " +
                           std::to_wstring(ev.referencePercent) + L"% BT";
                break;
            case G435Estimator::ReadingKind::RealBluetooth:
                s.detail = Tr(StrId::G435BatteryRealBt);
                break;
        }
        ctx.out.push_back(s);
        return;
    }

    // Disconnected on all transports: stop usage accumulation while preserving calibration history.
    if (G435Estimator::AccumulateUsage(estState, false, nowSec)) SaveEstimatorState(estState);
}

std::wstring DumpG435Diagnostics() {
    std::wstring out;
    auto colls = EnumHid();
    bool banner = false;

    for (const auto& c : colls) {
        bool isAdf   = (c.vid == 0x046D && c.pid == 0x0ADF);
        bool isAcb   = (c.vid == 0x046D && c.pid == 0x0ACB);
        bool isJieli = (c.vid == 0x4C4A && c.pid == 0x4155);
        if (!isAdf && !isAcb && !isJieli) continue;

        if (!banner) {
            out += L"\n--- Logitech G435 Hardware Probe ---\n";
            banner = true;
        }

        std::wstring prod = HidProduct(c);
        wchar_t buf[256];
        swprintf(buf, 256, L"Collection: %04X:%04X (IF %d) UsagePg:%04X Usage:%04X InLen:%u OutLen:%u FeatLen:%u Product:\"%s\"\n",
                 c.vid, c.pid, c.iface, c.page(), c.usage(), c.inLen(), c.outLen(), c.featLen(), prod.c_str());
        out += buf;

        if (c.featLen() > 0) {
            HidDev d;
            if (d.OpenFeatureOnly(c)) {
                BYTE reportId = (isAcb && c.page() == 0xFFC0) ? 6 : 0;
                std::vector<BYTE> feat;
                if (d.GetFeature(reportId, feat)) {
                    out += L"  [Feature Report " + std::to_wstring(reportId) + L"]: ";
                    for (size_t i = 0; i < std::min<size_t>(feat.size(), 20); i++) {
                        wchar_t hex[8];
                        swprintf(hex, 8, L"%02X ", feat[i]);
                        out += hex;
                    }
                    out += L"\n";
                }
            }
        }

        if (c.inLen() > 0) {
            HidDev d;
            if (d.OpenReadOnly(c)) {
                std::vector<BYTE> in;
                int n = d.Read(in, 300);
                if (n > 0) {
                    out += L"  [Passive Input Report (" + std::to_wstring(n) + L" bytes)]: ";
                    for (size_t i = 0; i < std::min<size_t>(in.size(), 20); i++) {
                        wchar_t hex[8];
                        swprintf(hex, 8, L"%02X ", in[i]);
                        out += hex;
                    }
                    out += L"\n";
                }
            }
        }

        if (isAdf && c.page() == 0xFF00 && c.usage() == 0x0001 && c.outLen() == 65 && c.inLen() == 65) {
            HidDev d;
            if (d.Open(c)) {
                int mv = ReadMillivoltsAdf(d);
                if (mv >= 0) {
                    out += L"  [G435 0ADF PA Probe]: SUCCESS! Voltage = " + std::to_wstring(mv) + L" mV (" +
                           std::to_wstring(G435Adf::Percent(mv)) + L"%)\n";
                } else {
                    out += L"  [G435 0ADF PA Probe]: No PA response on this collection (timed out or off).\n";
                }
            }
        }
    }

    G435BtState bt = QueryG435Bluetooth();
    if (bt.paired) {
        if (!banner) {
            out += L"\n--- Logitech G435 Hardware Probe ---\n";
            banner = true;
        }
        out += L"Bluetooth G435 Node: \"" + bt.friendlyName + L"\" Container:" + bt.containerId +
               L" Connected:" + (bt.connected ? L"YES" : L"NO") +
               L" Battery:" + (bt.percent >= 0 ? std::to_wstring(bt.percent) + L"%" : L"N/A") +
               L" Source:" + (bt.fromHfpService ? L"HFP ({0000111E-...})" : L"Container") + L"\n";
        if (!bt.hfpInstanceId.empty()) {
            out += L"  HFP Instance: " + bt.hfpInstanceId + L"\n";
        }
    }

    G435UsbAudioState audio = QueryG435UsbAudioActivity();
    if (audio.endpointPresent) {
        if (!banner) {
            out += L"\n--- Logitech G435 Hardware Probe ---\n";
            banner = true;
        }
        out += std::wstring(L"USB Audio Endpoint: Present=YES SessionActive=") +
               (audio.sessionActive ? L"YES" : L"NO") + L" PeakActive=" +
               (audio.peakActive ? L"YES" : L"NO") + L"\n";
    }

    G435Estimator::State est = LoadEstimatorState();
    if (banner) {
        out += L"Calibration State: Calibrated=" + std::wstring(est.calibrated ? L"YES" : L"NO") +
               L" LastReal=" + (est.lastRealPercent >= 0 ? std::to_wstring(est.lastRealPercent) + L"%" : L"N/A") +
               L" ActiveUsageSec=" + std::to_wstring(est.activeUsageSec) +
               L" LearnedRateMilliPctPerHr=" + std::to_wstring(est.learnedRateMilliPctPerHr) +
               L" Samples=" + std::to_wstring(est.sampleCount) + L"\n";
        out += L"------------------------------------\n";
    }
    return out;
}
