// Logitech G435 Wireless Gaming Headset — unified LIGHTSPEED & Bluetooth (HFP) driver.
//
// Unifies the Logitech G435 under a single logical identity (key "g435", name "Logitech G435")
// across both connection modes so the headset is never duplicated when switching modes:
//
//  1) Bluetooth mode (Hands-Free Profile / HFP, GearHub approach):
//     When the user switches the G435 to Bluetooth, Windows receives the real battery percentage
//     (0..100%) over the Hands-Free Audio Gateway service ({0000111E-0000-1000-8000-00805F9B34FB})
//     and stores it in DEVPKEY_Device_BatteryLevel ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid 2)
//     as a DEVPROP_TYPE_BYTE. Connection state is verified on the base BTHENUM\DEV_<MAC> (or
//     BTHLE\DEV_<MAC>) node via DEVPKEY_Device_IsConnected ({83DA6326-97A6-4088-9453-A1923F573B29},
//     pid 15). Only devices whose Bluetooth name strictly matches the G435 token are associated.
//
//  2) LIGHTSPEED mode via Jieli receiver (VID 0x4C4A, PID 0x4155):
//     Recognizes the Jieli USB receiver on its Consumer Control collection (000C:0001: InLen 3 B,
//     OutLen 0 B, FeatLen 0 B). Direct battery polling over this LIGHTSPEED receiver is currently
//     on hold (not permanently abandoned; all descriptor, endpoint, and SDK findings are preserved
//     in docs/logitech_g435.md for future investigation). While in LIGHTSPEED mode on 4C4A:4155,
//     percent is set to -1 (unknown) and detail reports "Bateria indisponível via LIGHTSPEED"
//     ("Battery unavailable via LIGHTSPEED"), while sharing the unified "g435" key preserves the
//     last valid battery percentage read over Bluetooth without inventing or estimating values.
//
//  3) LIGHTSPEED mode via Holtek receiver (VID 0x046D, PID 0x0ADF):
//     Queries battery millivolts over the vendor collection (FF00:0001, 64-byte CRC-16/XMODEM
//     frames carrying Airoha "PA" GET_BATTERY commands).
#include "drivers.h"
#include "logitech_g435_0adf.h"
#include "../i18n.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace {

constexpr BYTE kCatOta = 0x06;
constexpr BYTE kCatWireless = 0x0E;
constexpr BYTE kGetMode = 0xE0;
constexpr BYTE kSetMode = 0xE1;

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

// Strict token match for "g435" so no unrelated Bluetooth device is ever associated with the G435.
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

}  // namespace

void PollLogitechG435(PollContext& ctx) {
    // 1. Check Bluetooth (HFP / DEVPKEY_Device_BatteryLevel) for the G435 first.
    G435BtState bt = QueryG435Bluetooth();
    if (!bt.containerId.empty()) {
        // Reserve the G435 Bluetooth container so PollBluetooth in generic.cpp never duplicates it.
        ctx.btContainers.insert(bt.containerId);
    }

    // If the G435 is actively connected over Bluetooth right now, report the unified device in
    // Bluetooth mode with its real HFP battery percentage, and claim the USB dongle IDs so a
    // plugged-in receiver does not create a second entry.
    if (bt.paired && bt.connected) {
        ctx.claimed.insert({ 0x046D, 0x0ADF });
        ctx.claimed.insert({ 0x4C4A, 0x4155 });

        DeviceStatus s;
        s.key = L"g435";
        s.name = L"Logitech G435";
        s.kind = DeviceKind::Headset;
        s.via = L"Bluetooth";
        s.connected = true;
        s.percent = bt.percent;  // real 0..100% from DEVPKEY_Device_BatteryLevel, or -1 if not provided
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
            DeviceStatus s;
            s.key = L"g435";
            s.name = L"Logitech G435";
            s.kind = DeviceKind::Headset;
            s.via = L"LIGHTSPEED";

            if (mv < 0) {
                s.connected = false;
                ctx.Add(s, 0x046D, 0x0ADF);
                return;
            }
            s.millivolts = mv;
            s.percent = G435Adf::Percent(mv);
            s.connected = true;
            ctx.Add(s, 0x046D, 0x0ADF);
            return;
        }
    }

    // 3. Jieli 4C4A:4155 LIGHTSPEED receiver (unified identity "g435", no invented/estimated percent).
    if (!ctx.IsClaimed(0x4C4A, 0x4155)) {
        auto colls = ctx.Find(0x4C4A, 0x4155, [](const HidColl& c) {
            return c.page() == 0x000C && c.usage() == 0x0001;
        });
        if (!colls.empty()) {
            DeviceStatus s;
            s.key = L"g435";
            s.name = L"Logitech G435";
            s.kind = DeviceKind::Headset;
            s.via = L"LIGHTSPEED";
            s.connected = true;
            s.percent = -1;
            s.detail = Tr(StrId::G435JieliNoTelemetry);
            ctx.Add(s, 0x4C4A, 0x4155);
        }
    }
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

    if (banner) out += L"------------------------------------\n";
    return out;
}
