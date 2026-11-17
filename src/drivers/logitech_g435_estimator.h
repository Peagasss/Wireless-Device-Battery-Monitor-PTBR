#pragma once
// Adaptive battery calibration and heuristic estimation for the Logitech G435 headset.
// Kept independent of Win32 APIs so the state machine and learning rules can be unit-tested.
#include <algorithm>
#include <cstdint>

namespace G435Estimator {

enum class ReadingKind {
    Unknown,        // Uncalibrated: no real Bluetooth battery reading has been recorded yet
    ReferenceOnly,  // 1 real reading recorded: no discharge rate learned yet; keeps last real % as reference
    Estimated,      // 2+ readings with a learned discharge rate: estimates % from active LIGHTSPEED usage time
    RealBluetooth,  // Connected via Bluetooth with a live HFP battery reading
};

struct State {
    bool calibrated = false;
    bool promptShown = false;
    int lastRealPercent = -1;                    // 0..100
    std::uint64_t lastRealTimestampSec = 0;      // Unix epoch timestamp (seconds) of lastRealPercent
    std::uint64_t activeUsageSec = 0;            // Accumulated seconds of confirmed active usage since lastRealPercent
    std::uint32_t learnedRateMilliPctPerHr = 0;  // Learned discharge rate in 0.001% / hour of active use (0 = unlearned)
    std::uint32_t sampleCount = 0;               // Number of valid discharge intervals incorporated
    std::uint64_t lastPollTimestampSec = 0;      // Timestamp of previous active poll (0 when disconnected/idle)
};

struct Evaluation {
    ReadingKind kind = ReadingKind::Unknown;
    int percent = -1;           // -1 when Unknown, 0..100 otherwise
    int referencePercent = -1;  // lastRealPercent used as base reference
    bool estimated = false;     // true for ReferenceOnly and Estimated
    bool showCalibrationPrompt = false;
};

// Physical sanity bounds for learning a discharge rate on the G435 Li-Po cell:
//  - Minimum tracked active usage before computing a discharge rate: 10 minutes (600 s).
//  - Plausible discharge rate range: 0.5% / hour (500 m%/h) to 25.0% / hour (25000 m%/h).
constexpr std::uint64_t kMinLearningUsageSec = 600;
constexpr std::uint64_t kMaxPollStepSec      = 600;
constexpr double        kMinRatePctPerHr     = 0.5;
constexpr double        kMaxRatePctPerHr     = 25.0;

// Updates calibration state with a real battery percentage (0..100) read over Bluetooth HFP.
// Returns true if persistent state changed and should be saved.
inline bool RecordBluetoothReading(State& st, int realPct, std::uint64_t nowSec) {
    if (realPct < 0 || realPct > 100) return false;

    if (!st.calibrated || st.lastRealPercent < 0) {
        st.calibrated = true;
        st.lastRealPercent = realPct;
        st.lastRealTimestampSec = nowSec;
        st.activeUsageSec = 0;
        st.lastPollTimestampSec = nowSec;
        return true;
    }

    if (realPct < st.lastRealPercent) {
        int dropPct = st.lastRealPercent - realPct;
        if (st.activeUsageSec >= kMinLearningUsageSec && dropPct >= 1) {
            double hours = static_cast<double>(st.activeUsageSec) / 3600.0;
            double observedRate = static_cast<double>(dropPct) / hours;
            if (observedRate >= kMinRatePctPerHr && observedRate <= kMaxRatePctPerHr) {
                auto observedMilli = static_cast<std::uint32_t>(observedRate * 1000.0 + 0.5);
                if (st.learnedRateMilliPctPerHr == 0 || st.sampleCount == 0) {
                    st.learnedRateMilliPctPerHr = observedMilli;
                } else {
                    // Exponential moving average (70% prior + 30% new observation)
                    st.learnedRateMilliPctPerHr = (st.learnedRateMilliPctPerHr * 7 + observedMilli * 3) / 10;
                }
                st.sampleCount++;
            }
        }
        st.lastRealPercent = realPct;
        st.lastRealTimestampSec = nowSec;
        st.activeUsageSec = 0;
        st.lastPollTimestampSec = nowSec;
        return true;
    }

    if (realPct > st.lastRealPercent) {
        // Headset was recharged: keep learned discharge rate, reset reference to the new charge level.
        st.lastRealPercent = realPct;
        st.lastRealTimestampSec = nowSec;
        st.activeUsageSec = 0;
        st.lastPollTimestampSec = nowSec;
        return true;
    }

    // Same percentage step (HFP reports in coarse steps): preserve accumulated activeUsageSec.
    st.lastPollTimestampSec = nowSec;
    return false;
}

// Accumulates active usage time only when the headset is connected and actively in use.
// Never discounts battery while the headset is off, disconnected, or idle.
// Returns true if persistent state changed.
inline bool AccumulateUsage(State& st, bool connectedAndInUse, std::uint64_t nowSec) {
    if (!connectedAndInUse) {
        st.lastPollTimestampSec = 0;
        return false;
    }

    bool changed = false;
    if (st.calibrated && st.lastPollTimestampSec > 0 && nowSec > st.lastPollTimestampSec) {
        std::uint64_t dt = nowSec - st.lastPollTimestampSec;
        if (dt <= kMaxPollStepSec) {
            st.activeUsageSec += dt;
            changed = true;
        }
    }
    st.lastPollTimestampSec = nowSec;
    return changed;
}

// Computes the battery status to display while connected via LIGHTSPEED.
inline Evaluation EvaluateLightspeed(State& st) {
    Evaluation ev;
    if (!st.calibrated || st.lastRealPercent < 0) {
        ev.kind = ReadingKind::Unknown;
        ev.percent = -1;
        ev.referencePercent = -1;
        ev.estimated = false;
        if (!st.promptShown) {
            ev.showCalibrationPrompt = true;
            st.promptShown = true;
        }
        return ev;
    }

    ev.referencePercent = st.lastRealPercent;
    ev.estimated = true;

    if (st.learnedRateMilliPctPerHr == 0 || st.sampleCount == 0) {
        // Only one real reading recorded so far: keep the last real percentage as reference
        // without inventing a temporal discharge estimate.
        ev.kind = ReadingKind::ReferenceOnly;
        ev.percent = st.lastRealPercent;
        return ev;
    }

    ev.kind = ReadingKind::Estimated;
    double hours = static_cast<double>(st.activeUsageSec) / 3600.0;
    int drop = static_cast<int>((hours * static_cast<double>(st.learnedRateMilliPctPerHr)) / 1000.0);
    ev.percent = std::clamp(st.lastRealPercent - drop, 1, 100);
    return ev;
}

}  // namespace G435Estimator
