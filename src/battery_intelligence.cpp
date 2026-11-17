// Centralized Battery Intelligence Engine & Universal Charging State Normalization.
#include "battery_intelligence.h"
#include "devices.h"

#include <windows.h>
#include <setupapi.h>
#include <batclass.h>
#include <devguid.h>
#include <initguid.h>
#include <devpkey.h>

#include <ctime>
#include <cwctype>
#include <map>
#include <set>

namespace {

const wchar_t* kDevicesRegRoot = L"Software\\WirelessBattery\\Devices";
const wchar_t* kLegacyG435RegKey = L"Software\\WirelessBattery\\G435";

// {72631E54-78A4-11D0-BCF7-00AA00B7B32A} — GUID_DEVINTERFACE_BATTERY
const GUID kGuidDevInterfaceBattery = {
    0x72631E54, 0x78A4, 0x11D0, { 0xBC, 0xF7, 0x00, 0xAA, 0x00, 0xB7, 0xB3, 0x2A }
};

// Produces a deterministic, collision-free Registry subkey name from a stable device key.
// Each distinct `key` string maps to its own isolated subkey.
std::wstring SanitizeRegistrySubkey(const std::wstring& key) {
    if (key == L"g435") return L"g435";

    std::uint32_t fnv = 2166136261u;
    std::wstring clean;
    clean.reserve(key.size() + 10);
    bool modified = false;

    for (wchar_t ch : key) {
        fnv ^= static_cast<std::uint32_t>(ch);
        fnv *= 16777619u;
        if (iswalnum(ch) || ch == L'-' || ch == L'_' || ch == L'.') {
            clean.push_back(static_cast<wchar_t>(towlower(ch)));
        } else {
            clean.push_back(L'_');
            modified = true;
        }
    }
    if (clean.empty()) clean = L"dev";
    if (clean.size() > 96) {
        clean.resize(96);
        modified = true;
    }
    if (modified) {
        wchar_t hex[12] = {};
        swprintf(hex, 12, L"_%08x", fnv);
        clean += hex;
    }
    return clean;
}

DWORD ReadRegDword(HKEY key, const wchar_t* name, DWORD defVal) {
    DWORD v = defVal, sz = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v;
    return defVal;
}

std::uint64_t ReadRegQword(HKEY key, const wchar_t* name, std::uint64_t defVal) {
    std::uint64_t v = defVal;
    DWORD sz = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_QWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v;
    return defVal;
}

bool LoadHistoryFromOpenKey(HKEY k, BatteryIntelligence::DeviceHistory& st) {
    st.calibrated                 = ReadRegDword(k, L"Calibrated", 0) != 0;
    st.promptShown                = ReadRegDword(k, L"PromptShown", 0) != 0;
    DWORD pct                     = ReadRegDword(k, L"LastRealPercent", 0xFFFFFFFFu);
    st.lastRealPercent            = (pct <= 100) ? static_cast<int>(pct) : -1;
    st.lastRealTimestampSec       = ReadRegQword(k, L"LastRealTimestampSec", 0);
    st.activeUsageSec             = ReadRegQword(k, L"ActiveUsageSec", 0);
    st.totalLearnedUsageSec       = ReadRegQword(k, L"TotalLearnedUsageSec", st.activeUsageSec);
    st.learnedRateMilliPctPerHr   = ReadRegDword(k, L"LearnedRateMilliPctPerHr", 0);
    st.rateDeviationMilliPctPerHr = ReadRegDword(k, L"RateDeviationMilliPctPerHr", 0);
    st.sampleCount                = ReadRegDword(k, L"SampleCount", 0);
    st.lastPollTimestampSec       = ReadRegQword(k, L"LastPollTimestampSec", 0);
    st.unmonitoredGap             = ReadRegDword(k, L"UnmonitoredGap", 0) != 0;

    if (!st.calibrated || st.lastRealPercent < 0) {
        st.calibrated = false;
        st.lastRealPercent = -1;
    }
    if (st.sampleCount > 0 && st.totalLearnedUsageSec == 0) {
        st.totalLearnedUsageSec = static_cast<std::uint64_t>(st.sampleCount) * 3600ULL;
    }
    return st.calibrated || st.promptShown;
}

void SaveHistoryToOpenKey(HKEY k, const std::wstring& rawKey, const BatteryIntelligence::DeviceHistory& st) {
    auto setDw = [&](const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    };
    auto setQw = [&](const wchar_t* name, std::uint64_t v) {
        RegSetValueExW(k, name, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    };

    if (!rawKey.empty()) {
        RegSetValueExW(k, L"DeviceKey", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(rawKey.c_str()),
                       static_cast<DWORD>((rawKey.size() + 1) * sizeof(wchar_t)));
    }
    setDw(L"Calibrated", st.calibrated ? 1u : 0u);
    setDw(L"PromptShown", st.promptShown ? 1u : 0u);
    setDw(L"LastRealPercent", st.lastRealPercent >= 0 ? static_cast<DWORD>(st.lastRealPercent) : 0xFFFFFFFFu);
    setQw(L"LastRealTimestampSec", st.lastRealTimestampSec);
    setQw(L"ActiveUsageSec", st.activeUsageSec);
    setQw(L"TotalLearnedUsageSec", st.totalLearnedUsageSec);
    setDw(L"LearnedRateMilliPctPerHr", st.learnedRateMilliPctPerHr);
    setDw(L"RateDeviationMilliPctPerHr", st.rateDeviationMilliPctPerHr);
    setDw(L"SampleCount", st.sampleCount);
    setQw(L"LastPollTimestampSec", st.lastPollTimestampSec);
    setDw(L"UnmonitoredGap", st.unmonitoredGap ? 1u : 0u);
}

// Queries Windows Battery class interfaces (GUID_DEVINTERFACE_BATTERY, the kernel interface
// underlying Windows.Devices.Power.Battery / IBatteryReport) for peripherals exposing a
// battery node in a non-system PnP container.
std::map<std::wstring, ChargingState> QueryWindowsBatteryClassStates() {
    std::map<std::wstring, ChargingState> result;
    HDEVINFO di = SetupDiGetClassDevsW(&kGuidDevInterfaceBattery, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &kGuidDevInterfaceBattery, i, &ifd); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &reqSize, nullptr);
        if (reqSize == 0 || reqSize > 4096) continue;

        std::vector<BYTE> detailBuf(reqSize, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA dd{ sizeof(dd) };
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, detail, reqSize, nullptr, &dd)) continue;

        GUID containerGuid{};
        DEVPROPTYPE propType = 0;
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &propType,
                                       reinterpret_cast<PBYTE>(&containerGuid), sizeof(containerGuid), nullptr, 0) ||
            propType != DEVPROP_TYPE_GUID) {
            continue;
        }
        wchar_t gbuf[64] = {};
        StringFromGUID2(containerGuid, gbuf, 64);
        std::wstring cid = gbuf;
        for (auto& ch : cid) ch = static_cast<wchar_t>(towlower(ch));

        HANDLE hBat = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hBat == INVALID_HANDLE_VALUE) continue;

        ULONG tag = 0;
        ULONG wait = 0;
        DWORD bytesRet = 0;
        if (DeviceIoControl(hBat, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &bytesRet, nullptr) && tag != 0) {
            BATTERY_QUERY_INFORMATION bqi{};
            bqi.BatteryTag = tag;
            bqi.InformationLevel = BatteryInformation;
            BATTERY_INFORMATION bi{};
            bool hasInfo = DeviceIoControl(hBat, IOCTL_BATTERY_QUERY_INFORMATION, &bqi, sizeof(bqi),
                                           &bi, sizeof(bi), &bytesRet, nullptr) != FALSE;
            // Skip system UPS / main laptop ACPI batteries (SYSTEM_BATTERY flag without peripheral container)
            BATTERY_WAIT_STATUS bws{};
            bws.BatteryTag = tag;
            BATTERY_STATUS bs{};
            if (DeviceIoControl(hBat, IOCTL_BATTERY_QUERY_STATUS, &bws, sizeof(bws), &bs, sizeof(bs), &bytesRet, nullptr)) {
                ChargingState cs = ChargingState::Unknown;
                if (bs.PowerState & BATTERY_CHARGING) {
                    cs = ChargingState::Charging;
                } else if (bs.PowerState & BATTERY_DISCHARGING) {
                    cs = ChargingState::Discharging;
                } else if (bs.PowerState & BATTERY_POWER_ON_LINE) {
                    if (hasInfo && bi.FullChargedCapacity > 0 && bs.Capacity != BATTERY_UNKNOWN_CAPACITY &&
                        bs.Capacity >= bi.FullChargedCapacity) {
                        cs = ChargingState::Full;
                    } else {
                        cs = ChargingState::Idle;
                    }
                }
                if (cs != ChargingState::Unknown) {
                    result[cid] = cs;
                }
            }
        }
        CloseHandle(hBat);
    }

    SetupDiDestroyDeviceInfoList(di);
    return result;
}

// Returns true if the user has interacted with the PC (mouse/keyboard/input) within the last 5 minutes.
bool IsUserSessionActive() {
    LASTINPUTINFO lii{ sizeof(lii) };
    if (!GetLastInputInfo(&lii)) return true;
    DWORD now = GetTickCount();
    DWORD idleMs = now - lii.dwTime;
    return idleMs <= 5 * 60 * 1000u;
}

std::set<std::wstring> g_activeSessionKeys;

}  // namespace

BatteryIntelligence::DeviceHistory LoadDeviceBatteryHistory(const std::wstring& key) {
    BatteryIntelligence::DeviceHistory st;
    if (key.empty()) return st;

    std::wstring subPath = std::wstring(kDevicesRegRoot) + L"\\" + SanitizeRegistrySubkey(key);
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subPath.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        bool loaded = LoadHistoryFromOpenKey(k, st);
        RegCloseKey(k);
        if (loaded) return st;
    }

    // Automatic non-destructive migration for the Logitech G435 from HKCU\Software\WirelessBattery\G435.
    if (key == L"g435") {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyG435RegKey, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            LoadHistoryFromOpenKey(k, st);
            RegCloseKey(k);
            if (st.calibrated || st.promptShown) {
                SaveDeviceBatteryHistory(key, st);
            }
        }
    }
    return st;
}

void SaveDeviceBatteryHistory(const std::wstring& key, const BatteryIntelligence::DeviceHistory& st) {
    if (key.empty()) return;

    std::wstring subPath = std::wstring(kDevicesRegRoot) + L"\\" + SanitizeRegistrySubkey(key);
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        SaveHistoryToOpenKey(k, key, st);
        RegCloseKey(k);
    }

    // Keep the legacy G435 Registry key synchronized so nothing in the existing G435 setup is ever lost.
    if (key == L"g435") {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kLegacyG435RegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            SaveHistoryToOpenKey(k, key, st);
            RegCloseKey(k);
        }
    }
}

void ApplyBatteryIntelligence(std::vector<DeviceStatus>& devices) {
    const std::uint64_t nowSec = static_cast<std::uint64_t>(std::time(nullptr));
    const bool userActive = IsUserSessionActive();
    const auto winBatteryStates = QueryWindowsBatteryClassStates();

    std::set<std::wstring> currentKeys;

    for (auto& s : devices) {
        if (s.key.empty()) continue;
        currentKeys.insert(s.key);

        // 1. Enrich ChargingState from Windows Battery class if the device is a Bluetooth/HID container
        //    whose driver did not already report an explicit ChargingState.
        if (s.chargeState == ChargingState::Unknown && !s.charging && s.key.rfind(L"bt:", 0) == 0) {
            std::wstring cid = s.key.substr(3);
            for (auto& ch : cid) ch = static_cast<wchar_t>(towlower(ch));
            auto it = winBatteryStates.find(cid);
            if (it != winBatteryStates.end()) {
                s.chargeState = it->second;
            }
        }

        // 2. Normalize ChargingState and enforce that `s.charging` (⚡ icon) is true ONLY when
        //    `s.chargeState == ChargingState::Charging` is confirmed.
        s.chargeState = BatteryIntelligence::ResolveChargingState(s.chargeState, s.charging, s.percent);
        s.charging = (s.chargeState == ChargingState::Charging);

        // 3. Load isolated per-device history.
        BatteryIntelligence::DeviceHistory h = LoadDeviceBatteryHistory(s.key);

        // The Logitech G435 driver (`PollLogitechG435`) already updates its `g435` history directly
        // during its poll to support unified Bluetooth-to-LIGHTSPEED percentage estimation.
        if (s.key == L"g435") {
            s.confidence = BatteryIntelligence::ComputeConfidence(h);
            int effectivePct = s.percent;
            s.runtimeMinutes = BatteryIntelligence::ComputeRuntimeMinutes(effectivePct, s.chargeState, h);
            continue;
        }

        // 4. Determine whether the device is connected and actively in use during this poll step.
        bool connectedAndActive = false;
        if (s.connected && !s.asleep) {
            if (s.activityState == ActivityState::Active) {
                connectedAndActive = true;
            } else if (s.activityState == ActivityState::Idle) {
                connectedAndActive = false;
            } else {
                // When the driver does not report per-device activity, do not assume the peripheral
                // remained in active use while the user was away from the PC.
                connectedAndActive = userActive;
            }
        }

        bool dirty = BatteryIntelligence::AccumulateUsage(h, connectedAndActive, s.chargeState, nowSec);
        if (s.connected && !s.asleep && !s.estimated && s.percent >= 0) {
            dirty |= BatteryIntelligence::UpdateWithRealReading(h, s.percent, s.chargeState, nowSec);
        }
        if (dirty) {
            SaveDeviceBatteryHistory(s.key, h);
        }

        // 5. Compute confidence and remaining runtime estimate.
        s.confidence = BatteryIntelligence::ComputeConfidence(h);
        int effectivePct = (s.percent >= 0) ? s.percent : h.lastRealPercent;
        if (s.connected) {
            s.runtimeMinutes = BatteryIntelligence::ComputeRuntimeMinutes(effectivePct, s.chargeState, h);
        } else {
            s.runtimeMinutes = -1;
        }
    }

    // 6. Any device that was present in the previous poll and is now disconnected must stop
    //    accumulating active usage time while preserving its calibration & learned history.
    for (const auto& prevKey : g_activeSessionKeys) {
        if (currentKeys.count(prevKey) == 0 && prevKey != L"g435") {
            BatteryIntelligence::DeviceHistory h = LoadDeviceBatteryHistory(prevKey);
            if (h.lastPollTimestampSec != 0) {
                BatteryIntelligence::AccumulateUsage(h, false, ChargingState::Unknown, nowSec);
                SaveDeviceBatteryHistory(prevKey, h);
            }
        }
    }
    g_activeSessionKeys = std::move(currentKeys);
}
