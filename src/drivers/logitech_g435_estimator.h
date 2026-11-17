#pragma once
// Adaptive battery calibration and heuristic estimation for the Logitech G435 headset,
// built on top of the centralized BatteryIntelligence engine.
// Kept independent of Win32 APIs so the state machine and learning rules can be unit-tested.
#include "../battery_intelligence.h"

namespace G435Estimator {

enum class ReadingKind {
    Unknown,        // Uncalibrated: no real Bluetooth battery reading has been recorded yet
    ReferenceOnly,  // 1 real reading recorded: no discharge rate learned yet; keeps last real % as reference
    Estimated,      // 2+ readings with a learned discharge rate: estimates % from active LIGHTSPEED usage time
    RealBluetooth,  // Connected via Bluetooth with a live HFP battery reading
};

using State = BatteryIntelligence::DeviceHistory;

struct Evaluation {
    ReadingKind kind = ReadingKind::Unknown;
    int percent = -1;           // -1 when Unknown, 0..100 otherwise
    int referencePercent = -1;  // lastRealPercent used as base reference
    bool estimated = false;     // true for ReferenceOnly and Estimated
    bool showCalibrationPrompt = false;
    int runtimeMinutes = -1;
    ConfidenceLevel confidence = ConfidenceLevel::Insufficient;
};

constexpr std::uint64_t kMinLearningUsageSec = BatteryIntelligence::kMinLearningUsageSec;
constexpr std::uint64_t kMaxPollStepSec      = BatteryIntelligence::kMaxPollStepSec;
constexpr double        kMinRatePctPerHr     = BatteryIntelligence::kMinRatePctPerHr;
constexpr double        kMaxRatePctPerHr     = BatteryIntelligence::kMaxRatePctPerHr;

// Updates calibration state with a real battery percentage (0..100) read over Bluetooth HFP.
// Returns true if persistent state changed and should be saved.
inline bool RecordBluetoothReading(State& st, int realPct, std::uint64_t nowSec) {
    // Allow brief mode-switch transitions between LIGHTSPEED and Bluetooth without discarding
    // the activeUsageSec accumulated during LIGHTSPEED usage.
    st.unmonitoredGap = false;
    return BatteryIntelligence::UpdateWithRealReading(st, realPct, ChargingState::Unknown, nowSec);
}

// Accumulates active usage time only when the headset is connected and actively in use.
// Never discounts battery while the headset is off, disconnected, or idle.
// Returns true if persistent state changed.
inline bool AccumulateUsage(State& st, bool connectedAndInUse, std::uint64_t nowSec) {
    return BatteryIntelligence::AccumulateUsage(st, connectedAndInUse, ChargingState::Unknown, nowSec);
}

// Computes the battery status to display while connected via LIGHTSPEED.
inline Evaluation EvaluateLightspeed(State& st) {
    Evaluation ev;
    if (!st.calibrated || st.lastRealPercent < 0) {
        ev.kind = ReadingKind::Unknown;
        ev.percent = -1;
        ev.referencePercent = -1;
        ev.estimated = false;
        ev.confidence = ConfidenceLevel::Insufficient;
        ev.runtimeMinutes = -1;
        if (!st.promptShown) {
            ev.showCalibrationPrompt = true;
            st.promptShown = true;
        }
        return ev;
    }

    ev.referencePercent = st.lastRealPercent;
    ev.estimated = true;
    ev.confidence = BatteryIntelligence::ComputeConfidence(st);

    if (st.learnedRateMilliPctPerHr == 0 || st.sampleCount == 0) {
        // Only one real reading recorded so far: keep the last real percentage as reference
        // without inventing a temporal discharge estimate.
        ev.kind = ReadingKind::ReferenceOnly;
        ev.percent = st.lastRealPercent;
        ev.runtimeMinutes = -1;
        return ev;
    }

    ev.kind = ReadingKind::Estimated;
    double hours = static_cast<double>(st.activeUsageSec) / 3600.0;
    int drop = static_cast<int>((hours * static_cast<double>(st.learnedRateMilliPctPerHr)) / 1000.0);
    ev.percent = std::clamp(st.lastRealPercent - drop, 1, 100);
    ev.runtimeMinutes = BatteryIntelligence::ComputeRuntimeMinutes(ev.percent, ChargingState::Unknown, st);
    return ev;
}

}  // namespace G435Estimator
