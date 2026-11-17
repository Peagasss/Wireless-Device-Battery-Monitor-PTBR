#pragma once
#include <string>
#include <vector>

#include "battery_intelligence.h"

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad, Phone };

// One battery-powered device, as seen during a single poll.
struct DeviceStatus {
    std::wstring key;          // stable identity across polls (used for the tray icon id & persistence)
    std::wstring name;         // human readable name
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring detail;       // extra breakdown for the tooltip, e.g. "L 80%, R 75%, case 40%"
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    bool charging   = false;   // true only when Charging is confirmed (chargeState == ChargingState::Charging)
    ChargingState chargeState = ChargingState::Unknown;
    ActivityState activityState = ActivityState::Unknown;
    ConfidenceLevel confidence = ConfidenceLevel::Insufficient;
    int  runtimeMinutes = -1;  // > 0 when estimated remaining runtime is available, -1 when insufficient history
    bool asleep     = false;   // receiver answered but the device is sleeping (keep last value)
    bool connected  = true;    // false: receiver present, device switched off (hidden)
    bool estimated  = false;   // true: heuristic or reference battery estimate (not a live direct reading)
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
    int  graceSec   = 0;       // keep showing the last value this long if the device stops answering
    std::wstring calibrateMsg; // one-time calibration instruction when first connected without reference
};

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices();

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();

// Centralized Battery Intelligence Engine: normalizes charging state, updates per-device history,
// persists learning data, and computes remaining runtime and confidence for all polled devices.
void ApplyBatteryIntelligence(std::vector<DeviceStatus>& devices);

// Loads/saves per-device history in HKCU\Software\WirelessBattery\Devices\<SanitizedKey>
// (with automatic migration from HKCU\Software\WirelessBattery\G435 for key == "g435").
BatteryIntelligence::DeviceHistory LoadDeviceBatteryHistory(const std::wstring& key);
void SaveDeviceBatteryHistory(const std::wstring& key, const BatteryIntelligence::DeviceHistory& h);
