#pragma once
// Battery Intelligence Engine & Universal Charging State Normalization.
// Kept free of Win32/OS headers in the core algorithm structures so all learning, confidence,
// charging-state, and runtime-estimation rules can be unit-tested deterministically.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Normalized universal battery charging states across USB/HID, Bluetooth, Windows Power APIs,
// and vendor-specific protocols.
enum class ChargingState {
    Unknown = 0,  // Device or transport does not report a reliable charging status
    Charging,     // Actively charging (confirmed by hardware register or OS power report)
    Discharging,  // Running on battery power (confirmed by hardware or OS power report)
    Idle,         // Connected to external power, neither charging nor full (e.g. charge paused)
    Full,         // Battery fully charged
};

// Confirmed activity state of a peripheral during a poll cycle.
enum class ActivityState {
    Unknown = 0,  // Driver does not provide explicit usage telemetry; engine uses system input heuristics
    Active,       // Confirmed active usage (e.g. audio stream, button/input activity)
    Idle,         // Confirmed inactive / sleeping / off
};

// Statistical confidence level of the consumption rate and remaining runtime estimate.
enum class ConfidenceLevel {
    Insufficient = 0,  // Not enough discharge observations to compute a runtime estimate
    Low,               // 1–2 discharge observations or high variance between observations
    Medium,            // 2–3 consistent discharge observations with >= 45 min active usage
    High,              // 4+ consistent discharge observations with >= 2 hours active usage and low variance
};

namespace BatteryIntelligence {

struct DeviceHistory {
    bool calibrated = false;                        // True once at least one real battery reading (0..100%) is recorded
    bool promptShown = false;                       // One-time calibration prompt flag (used by G435 LIGHTSPEED)
    int lastRealPercent = -1;                       // Most recent real battery percentage (0..100)
    std::uint64_t lastRealTimestampSec = 0;         // Unix epoch timestamp (seconds) of lastRealPercent
    std::uint64_t activeUsageSec = 0;               // Confirmed active usage seconds accumulated since lastRealPercent
    std::uint64_t totalLearnedUsageSec = 0;         // Cumulative active usage seconds across all valid learning intervals
    std::uint32_t learnedRateMilliPctPerHr = 0;     // Learned discharge rate in 0.001% / hour of active use (0 = unlearned)
    std::uint32_t rateDeviationMilliPctPerHr = 0;   // Smoothed absolute deviation of observed rates (0.001% / hour)
    std::uint32_t sampleCount = 0;                  // Number of valid discharge intervals incorporated
    std::uint64_t lastPollTimestampSec = 0;         // Timestamp of previous active poll (0 when disconnected/idle)
    bool unmonitoredGap = false;                    // True if the device had an unmonitored time gap since lastRealPercent
};

// Physical sanity bounds for learning peripheral discharge rates:
//  - Minimum confirmed active usage in a discharge interval before computing a rate: 10 minutes (600 s).
//  - Maximum poll step considered contiguous active usage: 10 minutes (600 s).
//  - Maximum gap before marking an unmonitored window (e.g. app closed or long sleep): 15 minutes (900 s).
//  - Plausible peripheral discharge rate range: 0.2% / hour (200 m%/h) to 50.0% / hour (50000 m%/h).
constexpr std::uint64_t kMinLearningUsageSec   = 600;
constexpr std::uint64_t kMaxPollStepSec        = 600;
constexpr std::uint64_t kMaxUnmonitoredGapSec  = 900;
constexpr double        kMinRatePctPerHr       = 0.2;
constexpr double        kMaxRatePctPerHr       = 50.0;

// Normalizes a driver's legacy `bool charging` flag and explicit `ChargingState` into a single consistent state.
// Rule: never infer Charging from percentage increases or mere USB presence; only confirm Charging when
// explicitly reported by the driver or OS power API.
inline ChargingState ResolveChargingState(ChargingState explicitState, bool legacyChargingFlag, int percent) {
    if (explicitState != ChargingState::Unknown) {
        if (explicitState == ChargingState::Charging && percent >= 100) {
            // Keep Charging if hardware still reports active charging, or Full if explicitly Full
            return ChargingState::Charging;
        }
        return explicitState;
    }
    if (legacyChargingFlag) {
        return ChargingState::Charging;
    }
    return ChargingState::Unknown;
}

// Evaluates the confidence level of the learned discharge rate based on sample count, cumulative
// active usage duration, and consistency (relative deviation) across observations.
inline ConfidenceLevel ComputeConfidence(const DeviceHistory& h) {
    if (!h.calibrated || h.learnedRateMilliPctPerHr == 0 || h.sampleCount == 0) {
        return ConfidenceLevel::Insufficient;
    }

    // Relative deviation = rateDeviation / learnedRate
    double relDev = (h.learnedRateMilliPctPerHr > 0)
                        ? static_cast<double>(h.rateDeviationMilliPctPerHr) / static_cast<double>(h.learnedRateMilliPctPerHr)
                        : 1.0;

    if (h.sampleCount == 1 || h.totalLearnedUsageSec < 2700 || relDev > 0.45) {
        return ConfidenceLevel::Low;
    }
    if (h.sampleCount >= 4 && h.totalLearnedUsageSec >= 7200 && relDev <= 0.20) {
        return ConfidenceLevel::High;
    }
    return ConfidenceLevel::Medium;
}

// Accumulates active usage seconds only while the device is connected, awake, and confirmed active,
// and not actively charging. Returns true if persistent state changed.
inline bool AccumulateUsage(DeviceHistory& h, bool connectedAndActive, ChargingState chargeState, std::uint64_t nowSec) {
    if (chargeState == ChargingState::Charging || chargeState == ChargingState::Full || chargeState == ChargingState::Idle) {
        // Connected to external power: do not accumulate battery discharge time.
        h.lastPollTimestampSec = nowSec;
        return false;
    }

    if (!connectedAndActive) {
        if (h.lastPollTimestampSec > 0 && nowSec > h.lastPollTimestampSec &&
            (nowSec - h.lastPollTimestampSec) > kMaxUnmonitoredGapSec) {
            h.unmonitoredGap = true;
        }
        h.lastPollTimestampSec = 0;
        return false;
    }

    bool changed = false;
    if (h.calibrated && h.lastPollTimestampSec > 0 && nowSec > h.lastPollTimestampSec) {
        std::uint64_t dt = nowSec - h.lastPollTimestampSec;
        if (dt <= kMaxPollStepSec) {
            h.activeUsageSec += dt;
            changed = true;
        } else if (dt > kMaxUnmonitoredGapSec) {
            // App was closed or system slept for a long period; mark gap so unmonitored drain
            // does not distort the learned active discharge rate.
            h.unmonitoredGap = true;
            changed = true;
        }
    }
    h.lastPollTimestampSec = nowSec;
    return changed;
}

// Updates a device's history with a real battery percentage reading (0..100%).
// Handles initial calibration, discharge rate learning (EMA + deviation tracking), recharges,
// charging sessions, and inconsistent readings. Returns true if persistent state changed.
inline bool UpdateWithRealReading(DeviceHistory& h, int realPct, ChargingState chargeState, std::uint64_t nowSec) {
    if (realPct < 0 || realPct > 100) return false;

    // 1. Initial calibration (first real reading ever recorded for this device).
    if (!h.calibrated || h.lastRealPercent < 0) {
        h.calibrated = true;
        h.lastRealPercent = realPct;
        h.lastRealTimestampSec = nowSec;
        h.activeUsageSec = 0;
        h.lastPollTimestampSec = nowSec;
        h.unmonitoredGap = false;
        return true;
    }

    // 2. Device is actively charging or on external power: recalibrate reference without learning discharge.
    if (chargeState == ChargingState::Charging || chargeState == ChargingState::Full || chargeState == ChargingState::Idle) {
        bool changed = (h.lastRealPercent != realPct) || (h.activeUsageSec != 0) || h.unmonitoredGap;
        h.lastRealPercent = realPct;
        h.lastRealTimestampSec = nowSec;
        h.activeUsageSec = 0;
        h.lastPollTimestampSec = nowSec;
        h.unmonitoredGap = false;
        return changed;
    }

    // 3. Battery percentage decreased (discharge observed).
    if (realPct < h.lastRealPercent) {
        int dropPct = h.lastRealPercent - realPct;
        // Check if there was a long unmonitored gap with no tracked active usage.
        bool longGapWithoutUsage = (h.lastRealTimestampSec > 0 && nowSec > h.lastRealTimestampSec &&
                                    (nowSec - h.lastRealTimestampSec) > kMaxUnmonitoredGapSec &&
                                    h.activeUsageSec == 0);
        if (!h.unmonitoredGap && !longGapWithoutUsage && h.activeUsageSec >= kMinLearningUsageSec && dropPct >= 1) {
            double hours = static_cast<double>(h.activeUsageSec) / 3600.0;
            double observedRate = static_cast<double>(dropPct) / hours;
            if (observedRate >= kMinRatePctPerHr && observedRate <= kMaxRatePctPerHr) {
                auto observedMilli = static_cast<std::uint32_t>(observedRate * 1000.0 + 0.5);
                if (h.learnedRateMilliPctPerHr == 0 || h.sampleCount == 0) {
                    h.learnedRateMilliPctPerHr = observedMilli;
                    h.rateDeviationMilliPctPerHr = 0;
                } else {
                    std::uint32_t diff = (observedMilli > h.learnedRateMilliPctPerHr)
                                             ? (observedMilli - h.learnedRateMilliPctPerHr)
                                             : (h.learnedRateMilliPctPerHr - observedMilli);
                    // Exponential moving average: 70% historical + 30% new observation.
                    h.learnedRateMilliPctPerHr = (h.learnedRateMilliPctPerHr * 7 + observedMilli * 3) / 10;
                    if (h.sampleCount == 1) {
                        h.rateDeviationMilliPctPerHr = diff;
                    } else {
                        h.rateDeviationMilliPctPerHr = (h.rateDeviationMilliPctPerHr * 7 + diff * 3) / 10;
                    }
                }
                h.sampleCount++;
                h.totalLearnedUsageSec += h.activeUsageSec;
            }
        }
        h.lastRealPercent = realPct;
        h.lastRealTimestampSec = nowSec;
        h.activeUsageSec = 0;
        h.lastPollTimestampSec = nowSec;
        h.unmonitoredGap = false;
        return true;
    }

    // 4. Battery percentage increased (recharged while disconnected or between polls).
    //    Note: We never assume the device is currently charging solely from a higher percentage;
    //    we simply reset the reference level while preserving the learned discharge rate.
    if (realPct > h.lastRealPercent) {
        h.lastRealPercent = realPct;
        h.lastRealTimestampSec = nowSec;
        h.activeUsageSec = 0;
        h.lastPollTimestampSec = nowSec;
        h.unmonitoredGap = false;
        return true;
    }

    // 5. Same percentage step: keep accumulating activeUsageSec until the next percentage drop.
    h.lastPollTimestampSec = nowSec;
    return false;
}

// Computes the remaining battery runtime in minutes from the available percentage (1..100%)
// and the learned discharge rate. Returns -1 when history is insufficient or while charging.
inline int ComputeRuntimeMinutes(int effectivePercent, ChargingState chargeState, const DeviceHistory& h) {
    if (effectivePercent <= 0 || effectivePercent > 100) return -1;
    if (chargeState == ChargingState::Charging || chargeState == ChargingState::Full) return -1;
    if (ComputeConfidence(h) == ConfidenceLevel::Insufficient) return -1;
    if (h.learnedRateMilliPctPerHr == 0) return -1;

    // Remaining hours = effectivePercent / (learnedRateMilliPctPerHr / 1000.0)
    double hours = (static_cast<double>(effectivePercent) * 1000.0) / static_cast<double>(h.learnedRateMilliPctPerHr);
    if (hours <= 0.0) return -1;
    int minutes = static_cast<int>( std::round(hours * 60.0) );
    return std::clamp(minutes, 1, 60000);
}

}  // namespace BatteryIntelligence
