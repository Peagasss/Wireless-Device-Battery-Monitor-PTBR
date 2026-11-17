#include "../src/battery_intelligence.h"
#include "../src/devices.h"
#include "../src/drivers/logitech_g435_estimator.h"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

namespace {

// 1 & 2. Charging states & absence of charging info.
void TestChargingStateNormalization() {
    // Explicit states are preserved.
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Charging, false, 50) == ChargingState::Charging);
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Discharging, false, 50) == ChargingState::Discharging);
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Idle, false, 80) == ChargingState::Idle);
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Full, false, 100) == ChargingState::Full);

    // Legacy boolean flag maps to Charging when explicit state is Unknown.
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Unknown, true, 65) == ChargingState::Charging);

    // Absence of charging info must remain Unknown (never assumed).
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Unknown, false, 85) == ChargingState::Unknown);
    assert(BatteryIntelligence::ResolveChargingState(ChargingState::Unknown, false, 100) == ChargingState::Unknown);
}

// 3, 4, 5. Insufficient history, discharge rate calculation, and remaining runtime estimation.
void TestDischargeRateAndRuntimeEstimation() {
    BatteryIntelligence::DeviceHistory h;

    // Uncalibrated: Insufficient confidence, no runtime.
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Insufficient);
    assert(BatteryIntelligence::ComputeRuntimeMinutes(80, ChargingState::Discharging, h) == -1);

    // Single real reading (85% at t = 1000s): calibrated, but a single reading must NEVER define a rate.
    assert(BatteryIntelligence::UpdateWithRealReading(h, 85, ChargingState::Discharging, 1000));
    assert(h.calibrated);
    assert(h.lastRealPercent == 85);
    assert(h.learnedRateMilliPctPerHr == 0);
    assert(h.sampleCount == 0);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Insufficient);
    assert(BatteryIntelligence::ComputeRuntimeMinutes(85, ChargingState::Discharging, h) == -1);

    // Accumulate 1 hour (3600s) of confirmed active usage in 60s steps.
    std::uint64_t t = 1000;
    for (int i = 0; i < 60; i++) {
        t += 60;
        BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    }
    assert(h.activeUsageSec == 3600);

    // Second real reading (80% after 1 hour -> 5%/h = 5000 m%/h):
    //   - Available battery: 80%
    //   - Observed consumption: 5%/h
    //   - Remaining runtime: 80 / 5 = 16 hours = 960 minutes.
    assert(BatteryIntelligence::UpdateWithRealReading(h, 80, ChargingState::Discharging, t));
    assert(h.learnedRateMilliPctPerHr == 5000);
    assert(h.sampleCount == 1);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Low);
    assert(BatteryIntelligence::ComputeRuntimeMinutes(80, ChargingState::Discharging, h) == 960);

    // At 42.5% with 5%/h -> 8.5 hours = 510 minutes (8h 30min).
    assert(BatteryIntelligence::ComputeRuntimeMinutes(42, ChargingState::Discharging, h) == 504);

    // While charging or full, runtime estimate is suppressed (-1).
    assert(BatteryIntelligence::ComputeRuntimeMinutes(80, ChargingState::Charging, h) == -1);
    assert(BatteryIntelligence::ComputeRuntimeMinutes(100, ChargingState::Full, h) == -1);
}

// 6. Transition between charging and discharging (and rising % without charging flag).
void TestChargingAndDischargingTransitions() {
    BatteryIntelligence::DeviceHistory h;
    std::uint64_t t = 1000;

    BatteryIntelligence::UpdateWithRealReading(h, 60, ChargingState::Discharging, t);
    for (int i = 0; i < 60; i++) {
        t += 60;
        BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    }
    // 60% -> 55% in 1h (5%/h = 5000 m%/h).
    BatteryIntelligence::UpdateWithRealReading(h, 55, ChargingState::Discharging, t);
    assert(h.learnedRateMilliPctPerHr == 5000);

    // User plugs in charger: active usage accumulation stops during Charging, reference updates to 90%.
    for (int i = 0; i < 30; i++) {
        t += 60;
        assert(!BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Charging, t));
    }
    assert(h.activeUsageSec == 0);
    BatteryIntelligence::UpdateWithRealReading(h, 90, ChargingState::Charging, t);
    assert(h.lastRealPercent == 90);
    assert(h.learnedRateMilliPctPerHr == 5000);  // learned discharge rate is preserved across charging!

    // What if a device does NOT report a Charging flag (Unknown), and its battery jumps from 90% to 95%?
    // Engine recalibrates reference to 95% without corrupting the discharge rate and without assuming Charging.
    t += 600;
    BatteryIntelligence::UpdateWithRealReading(h, 95, ChargingState::Unknown, t);
    assert(h.lastRealPercent == 95);
    assert(h.learnedRateMilliPctPerHr == 5000);

    // Resuming discharge from 95% -> 90% over 1h of active use:
    for (int i = 0; i < 60; i++) {
        t += 60;
        BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    }
    BatteryIntelligence::UpdateWithRealReading(h, 90, ChargingState::Discharging, t);
    assert(h.sampleCount == 2);
    assert(h.learnedRateMilliPctPerHr == 5000);
}

// 7 & 8. Inactive device, disconnection, and unmonitored gap protection.
void TestInactiveDisconnectionAndUnmonitoredGap() {
    BatteryIntelligence::DeviceHistory h;
    std::uint64_t t = 1000;

    BatteryIntelligence::UpdateWithRealReading(h, 80, ChargingState::Discharging, t);
    for (int i = 0; i < 60; i++) {
        t += 60;
        BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    }
    BatteryIntelligence::UpdateWithRealReading(h, 75, ChargingState::Discharging, t);
    assert(h.learnedRateMilliPctPerHr == 5000);

    // Accumulate 20 minutes (1200s) of active usage, then disconnect for 4 hours (14400s).
    for (int i = 0; i < 20; i++) {
        t += 60;
        BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    }
    assert(h.activeUsageSec == 1200);

    // Disconnection zeroes lastPollTimestampSec after marking unmonitored gap when long time passes.
    BatteryIntelligence::AccumulateUsage(h, false, ChargingState::Discharging, t + 60);
    assert(h.lastPollTimestampSec == 0);

    // Suppose the app was closed for 3 hours (10800s) while the user used the mouse on another PC,
    // and upon reopening the battery dropped from 75% to 50%. Because of the unmonitored gap (dt > 900s),
    // the engine must recalibrate to 50% WITHOUT corrupting the 5.0%/h learned rate!
    t += 10800;
    BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t);
    // Simulate a direct jump across an unmonitored gap:
    h.unmonitoredGap = true;
    BatteryIntelligence::UpdateWithRealReading(h, 50, ChargingState::Discharging, t);
    assert(h.lastRealPercent == 50);
    assert(h.learnedRateMilliPctPerHr == 5000);
    assert(!h.unmonitoredGap);
}

// 10 & 11. Confidence progression (Insufficient -> Low -> Medium -> High) and recalibration on variance.
void TestConfidenceLevelsAndRecalibration() {
    BatteryIntelligence::DeviceHistory h;
    std::uint64_t t = 1000;

    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Insufficient);
    BatteryIntelligence::UpdateWithRealReading(h, 100, ChargingState::Discharging, t);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Insufficient);

    // Interval 1: 1h active use, 100% -> 95% (5%/h) -> Low confidence (only 1 sample).
    for (int i = 0; i < 60; i++) { t += 60; BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t); }
    BatteryIntelligence::UpdateWithRealReading(h, 95, ChargingState::Discharging, t);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Low);

    // Interval 2: 1h active use, 95% -> 90% (5%/h) -> Medium confidence (2 samples, 2h learned).
    for (int i = 0; i < 60; i++) { t += 60; BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t); }
    BatteryIntelligence::UpdateWithRealReading(h, 90, ChargingState::Discharging, t);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Medium);

    // Interval 3: 1h active use, 90% -> 85% (5%/h) -> Medium confidence (3 samples).
    for (int i = 0; i < 60; i++) { t += 60; BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t); }
    BatteryIntelligence::UpdateWithRealReading(h, 85, ChargingState::Discharging, t);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::Medium);

    // Interval 4: 1h active use, 85% -> 80% (5%/h) -> High confidence (4 consistent samples, >= 2h learned).
    for (int i = 0; i < 60; i++) { t += 60; BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t); }
    BatteryIntelligence::UpdateWithRealReading(h, 80, ChargingState::Discharging, t);
    assert(BatteryIntelligence::ComputeConfidence(h) == ConfidenceLevel::High);

    // Sudden behavioral change (e.g. RGB turned to max: 20%/h drain -> 80% to 60% in 1h)
    // adapts the rate and lowers confidence from High due to increased deviation.
    for (int i = 0; i < 60; i++) { t += 60; BatteryIntelligence::AccumulateUsage(h, true, ChargingState::Discharging, t); }
    BatteryIntelligence::UpdateWithRealReading(h, 60, ChargingState::Discharging, t);
    assert(h.learnedRateMilliPctPerHr > 5000);
    assert(BatteryIntelligence::ComputeConfidence(h) != ConfidenceLevel::High);
}

// 9 & 12. Isolation between devices and G435 LIGHTSPEED/Bluetooth compatibility.
void TestDeviceIsolationAndG435Compatibility() {
    std::map<std::wstring, BatteryIntelligence::DeviceHistory> store;

    auto& g435 = store[L"g435"];
    auto& mouse = store[L"logi:unit:deadbeef"];

    std::uint64_t t = 5000;
    // Calibrate G435 at 80% (5%/h after 1h), and Mouse at 90% (2%/h after 1h).
    G435Estimator::RecordBluetoothReading(g435, 80, t);
    BatteryIntelligence::UpdateWithRealReading(mouse, 90, ChargingState::Discharging, t);

    for (int i = 0; i < 60; i++) {
        t += 60;
        G435Estimator::AccumulateUsage(g435, true, t);
        BatteryIntelligence::AccumulateUsage(mouse, true, ChargingState::Discharging, t);
    }

    G435Estimator::RecordBluetoothReading(g435, 75, t);
    BatteryIntelligence::UpdateWithRealReading(mouse, 88, ChargingState::Discharging, t);

    // Histories and learned rates are strictly isolated.
    assert(g435.learnedRateMilliPctPerHr == 5000);
    assert(mouse.learnedRateMilliPctPerHr == 2000);

    // Simulate 1 more hour of active LIGHTSPEED use on G435:
    for (int i = 0; i < 60; i++) {
        t += 60;
        G435Estimator::AccumulateUsage(g435, true, t);
    }
    auto ev = G435Estimator::EvaluateLightspeed(g435);
    assert(ev.kind == G435Estimator::ReadingKind::Estimated);
    assert(ev.percent == 70);  // 75% - 5% = 70%
    assert(ev.runtimeMinutes == 840);  // 70% / (5%/h) = 14h = 840 min
    assert(BatteryIntelligence::ComputeRuntimeMinutes(88, ChargingState::Discharging, mouse) == 2640);  // 88% / (2%/h) = 44h = 2640 min
}

// Registry persistence round-trip & isolation test.
void TestRegistryPersistenceAndIsolation() {
    const std::wstring keyA = L"unit_test:dev_a";
    const std::wstring keyB = L"unit_test:dev_b";

    BatteryIntelligence::DeviceHistory ha;
    ha.calibrated = true;
    ha.promptShown = true;
    ha.lastRealPercent = 77;
    ha.lastRealTimestampSec = 1700000000ULL;
    ha.activeUsageSec = 1800ULL;
    ha.totalLearnedUsageSec = 10800ULL;
    ha.learnedRateMilliPctPerHr = 4500;
    ha.rateDeviationMilliPctPerHr = 250;
    ha.sampleCount = 4;
    ha.lastPollTimestampSec = 1700001800ULL;

    BatteryIntelligence::DeviceHistory hb;
    hb.calibrated = true;
    hb.lastRealPercent = 42;
    hb.lastRealTimestampSec = 1700005000ULL;
    hb.learnedRateMilliPctPerHr = 8200;
    hb.sampleCount = 2;
    hb.totalLearnedUsageSec = 3600ULL;

    SaveDeviceBatteryHistory(keyA, ha);
    SaveDeviceBatteryHistory(keyB, hb);

    auto loadedA = LoadDeviceBatteryHistory(keyA);
    auto loadedB = LoadDeviceBatteryHistory(keyB);

    assert(loadedA.calibrated && loadedA.promptShown);
    assert(loadedA.lastRealPercent == 77);
    assert(loadedA.activeUsageSec == 1800ULL);
    assert(loadedA.totalLearnedUsageSec == 10800ULL);
    assert(loadedA.learnedRateMilliPctPerHr == 4500);
    assert(loadedA.rateDeviationMilliPctPerHr == 250);
    assert(loadedA.sampleCount == 4);
    assert(BatteryIntelligence::ComputeConfidence(loadedA) == ConfidenceLevel::High);

    assert(loadedB.calibrated && !loadedB.promptShown);
    assert(loadedB.lastRealPercent == 42);
    assert(loadedB.learnedRateMilliPctPerHr == 8200);
    assert(loadedB.sampleCount == 2);

    // Clean up temporary test subkeys from HKCU\Software\WirelessBattery\Devices
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\WirelessBattery\\Devices", 0, KEY_WRITE | KEY_ENUMERATE_SUB_KEYS, &root) == ERROR_SUCCESS) {
        wchar_t sub[256] = {};
        for (DWORD idx = 0; RegEnumKeyW(root, idx, sub, 256) == ERROR_SUCCESS;) {
            if (wcsncmp(sub, L"unit_test_", 10) == 0) {
                RegDeleteKeyW(root, sub);
                idx = 0;
            } else {
                idx++;
            }
        }
        RegCloseKey(root);
    }
}

}  // namespace

int main() {
    TestChargingStateNormalization();
    TestDischargeRateAndRuntimeEstimation();
    TestChargingAndDischargingTransitions();
    TestInactiveDisconnectionAndUnmonitoredGap();
    TestConfidenceLevelsAndRecalibration();
    TestDeviceIsolationAndG435Compatibility();
    TestRegistryPersistenceAndIsolation();

    std::puts("battery_intelligence_test: ok");
    return 0;
}
