// Logitech G435 LIGHTSPEED wireless gaming headset driver (USB LIGHTSPEED receivers only;
// does not use HID++ and does not fall back to Bluetooth).
//
// Known hardware revisions of the G435 LIGHTSPEED USB receiver (model A00150):
//
//  1) 046D:0ACB (Realtek ALC4021 + PixArt PAU1823):
//     Oldest revision. Uses a different transport that interrupts audio on query; not polled here.
//
//  2) 046D:0ADF (Holtek USB bridge + PixArt RF SoC):
//     Exposes a vendor HID collection (FF00:0001, unnumbered 64-byte IN/OUT reports) carrying
//     CRC-16/XMODEM framed messages. "PA" commands inside them reach the headset's radio SoC and
//     return the battery voltage in millivolts via the OTA GET_BATTERY command (00 31).
//     Session sequence (captured by portyporty): open (84 01), 85 01, session init
//     (OTA_CMD_IOCTL_READ_MAX_LEN), REMOTE_GET_MODE / REMOTE_SET_MODE 01 / REMOTE_GET_MODE so the
//     query is routed to the headset rather than answered by the dongle, GET_BATTERY, close (85 02).
//     Protocol reference: github.com/portyporty/g435-battery-percentage (MIT), ported in
//     github.com/deddroid/logitech-g435-battery.
//
//  3) 4C4A:4155 (Jieli Technology SoC, dongle A00150 / P/N 881-000528, 2024 revision):
//     Reports USB VID 0x4C4A ("Jieli Technology"), PID 0x4155 ("G435 Wireless Gaming Headset").
//     Full USB/HID reverse-engineering findings on hardware:
//      * Configuration Descriptor (wTotalLength = 240 bytes, 4 interfaces, 1 configuration):
//        - Interface 0: USB Audio Control 1.0 (85 bytes: 2 Input Terminals, 2 Output Terminals,
//          2 Selector Units, 2 Feature Units; 0 Extension Units).
//        - Interface 1: USB Audio Streaming OUT (48 kHz 16-bit 2ch PCM, EP 0x03 Isochronous OUT).
//        - Interface 2: USB Audio Streaming IN (48 kHz 16-bit 1ch PCM, EP 0x83 Isochronous IN).
//        - Interface 3: USB HID 1.00 (EP 0x82 Interrupt IN, wMaxPacketSize = 16, no OUT endpoint).
//      * HID Report Descriptor on Interface 3 (51 bytes, UsagePage 0x000C, Usage 0x0001):
//        Declares 16 1-bit Input buttons (0xE9 Volume Inc, 0xEA Volume Dec, 0xE2 Mute, 0xCD
//        Play/Pause, 0xB5 Next, 0xB6 Prev, 0xB3 Fast Forward, 0xB7 Rewind, 0xCA Tracking Inc,
//        0xCB Tracking Dec, 0xCC Stop/Eject, 0xE0 Volume, 0x0150 Balance Right, 0x0151 Balance
//        Left, 0xB0 Play, 0xB1 Pause). InputReportByteLength = 3, OutputReportByteLength = 0,
//        FeatureReportByteLength = 0.
//      * Empirical tests on hardware:
//        - HidD_GetFeature (IDs 0..255): fails with Win32 error 1 (ERROR_INVALID_FUNCTION) because
//          FeatureReportByteLength is 0.
//        - HidD_GetInputReport (IDs 0..255): fails with Win32 error 31 (ERROR_GEN_FAILURE / USB
//          Endpoint 0 STALL from the Jieli firmware on GET_REPORT).
//        - EP 0x82 Interrupt IN: only emits 3-byte Consumer Control packets ([00 01 00] Volume+,
//          [00 02 00] Volume-, [00 00 00] release). Does not emit packets on headset Mute,
//          headset power off/on, USB-C charging state changes, or periodic intervals.
//        - Windows Audio Topology (IKsJackDescription::IsConnected) and PnP Container properties
//          remain static as long as the USB dongle is plugged in, with no battery property exposed.
//     Therefore, 4C4A:4155 is recognized as an active Logitech G435 LIGHTSPEED device with
//     percent = -1 (unknown) and an explicit diagnostic detail string, without fake estimates.
#include "drivers.h"
#include "logitech_g435_0adf.h"
#include "../i18n.h"

namespace {

constexpr BYTE kCatOta = 0x06;
constexpr BYTE kCatWireless = 0x0E;
constexpr BYTE kGetMode = 0xE0;
constexpr BYTE kSetMode = 0xE1;

// Session init from the capture (OTA_CMD_IOCTL_READ_MAX_LEN, sequence 1 embedded).
const std::vector<BYTE> kSessionInit = { 0x50, 0x41, 0x06, 0x01, 0x0D, 0x00, 0x25, 0x34, 0x12, 0x5A,
                                         0x5A, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00 };

// Sends one frame (report ID 0 if numbered) and collects the input reports that arrive within `waitMs`.
// With `mv` set, returns as soon as a battery reply is seen.
bool Transact(HidDev& d, BYTE cmd, const std::vector<BYTE>& body, int waitMs, int* mv = nullptr) {
    auto f = G435Adf::Frame(cmd, body);
    std::vector<BYTE> out;
    if (d.coll().outLen() == 65) {
        out.push_back(0x00);
        out.insert(out.end(), f.begin(), f.end());
    } else {
        out.assign(f.begin(), f.end());
    }
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

// Always closes the session (85 02) once it was opened, whatever happens in between.
struct SessionGuard {
    HidDev& d;
    ~SessionGuard() { Transact(d, 0x85, { 0x02 }, 50); }
};

int ReadMillivolts(HidDev& d) {
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

struct G435Target {
    USHORT vid;
    USHORT pid;
    const wchar_t* via;
};

constexpr G435Target kTargets[] = {
    { 0x046D, 0x0ADF, L"2.4 GHz receiver" },
    { 0x4C4A, 0x4155, L"LIGHTSPEED" },
};

}  // namespace

void PollLogitechG435(PollContext& ctx) {
    for (const auto& t : kTargets) {
        if (ctx.IsClaimed(t.vid, t.pid)) continue;

        if (t.vid == 0x4C4A && t.pid == 0x4155) {
            auto colls = ctx.Find(t.vid, t.pid, [](const HidColl& c) {
                return c.page() == 0x000C && c.usage() == 0x0001;
            });
            if (!colls.empty()) {
                DeviceStatus s;
                s.key = L"g435:" + Hex4(t.pid);
                s.name = L"Logitech G435";
                s.kind = DeviceKind::Headset;
                s.via = t.via;
                s.connected = true;
                s.percent = -1;
                s.detail = Tr(StrId::G435JieliNoTelemetry);
                ctx.Add(s, t.vid, t.pid);
            }
            continue;
        }

        // 046D:0ADF Holtek revision with FF00:0001 vendor channel (64-byte IN/OUT reports)
        auto colls = ctx.Find(t.vid, t.pid, [](const HidColl& c) {
            return (c.page() == 0xFF00 && c.usage() == 0x0001) ||
                   (c.inLen() >= 32 && c.outLen() >= 32);
        });
        if (colls.empty()) continue;

        for (const auto* coll : colls) {
            HidDev d;
            if (!d.Open(*coll)) continue;

            int mv = ReadMillivolts(d);
            DeviceStatus s;
            s.key = L"g435:" + Hex4(t.pid);
            s.name = L"Logitech G435";
            s.kind = DeviceKind::Headset;
            s.via = t.via;

            if (mv < 0) {
                // Dongle present but no reply: headset off or out of range.
                s.connected = false;
                ctx.Add(s, t.vid, t.pid);
                break;
            }
            s.millivolts = mv;
            s.percent = G435Adf::Percent(mv);
            s.connected = true;
            ctx.Add(s, t.vid, t.pid);
            break;
        }
    }
}

std::wstring DumpG435Diagnostics() {
    std::wstring out;
    auto colls = EnumHid();
    bool banner = false;

    for (const auto& c : colls) {
        bool match = (c.vid == 0x046D && (c.pid == 0x0ADF || c.pid == 0x0ACB)) ||
                     (c.vid == 0x4C4A && c.pid == 0x4155);
        std::wstring prod = HidProduct(c);
        if (!match && prod.find(L"G435") != std::wstring::npos) match = true;
        if (!match) continue;

        if (!banner) {
            out += L"\n--- Logitech G435 Hardware Probe ---\n";
            banner = true;
        }

        wchar_t buf[256];
        swprintf(buf, 256, L"Collection: %04X:%04X (IF %d) UsagePg:%04X Usage:%04X InLen:%u OutLen:%u FeatLen:%u Product:\"%s\"\n",
                 c.vid, c.pid, c.iface, c.page(), c.usage(), c.inLen(), c.outLen(), c.featLen(), prod.c_str());
        out += buf;

        // Probe Feature Reports
        if (c.featLen() > 0) {
            HidDev d;
            if (d.OpenFeatureOnly(c)) {
                for (BYTE id : { (BYTE)0, (BYTE)1, (BYTE)2, (BYTE)3 }) {
                    std::vector<BYTE> feat;
                    if (d.GetFeature(id, feat)) {
                        out += L"  [Feature Report " + std::to_wstring(id) + L"]: ";
                        for (size_t i = 0; i < std::min<size_t>(feat.size(), 20); i++) {
                            wchar_t hex[8];
                            swprintf(hex, 8, L"%02X ", feat[i]);
                            out += hex;
                        }
                        out += L"\n";
                    }
                }
            }
        }

        // Probe Input Reports (passive read)
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

        // Probe G435 PA Session command
        if (c.outLen() >= 32 && c.inLen() >= 32) {
            HidDev d;
            if (d.Open(c)) {
                int mv = ReadMillivolts(d);
                if (mv >= 0) {
                    out += L"  [G435 PA Probe]: SUCCESS! Voltage = " + std::to_wstring(mv) + L" mV (" +
                           std::to_wstring(G435Adf::Percent(mv)) + L"%)\n";
                } else {
                    out += L"  [G435 PA Probe]: No PA response on this collection (timed out or off).\n";
                }
            }
        }
    }

    if (banner) out += L"------------------------------------\n";
    return out;
}
