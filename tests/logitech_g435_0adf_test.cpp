#include "../src/drivers/logitech_g435_0adf.h"
#include "../src/drivers/logitech_g435_estimator.h"

#include <cassert>
#include <cstdio>
#include <vector>

static void TestEstimator() {
    G435Estimator::State st;

    // 1. First LIGHTSPEED connection without prior Bluetooth calibration:
    //    - Must return Unknown (-1%), never a fake percentage.
    //    - Must request showing the one-time calibration prompt on first connection.
    auto ev0 = G435Estimator::EvaluateLightspeed(st);
    assert(ev0.kind == G435Estimator::ReadingKind::Unknown);
    assert(ev0.percent == -1);
    assert(!ev0.estimated);
    assert(ev0.showCalibrationPrompt);
    assert(st.promptShown);

    // Subsequent poll before calibration does not re-spam the prompt.
    auto ev0b = G435Estimator::EvaluateLightspeed(st);
    assert(ev0b.kind == G435Estimator::ReadingKind::Unknown);
    assert(ev0b.percent == -1);
    assert(!ev0b.showCalibrationPrompt);

    // 2. First Bluetooth reading (calibration at 80% at t = 1000s):
    //    - Stores percentage and timestamp.
    //    - With only 1 reading, LIGHTSPEED mode keeps 80% as ReferenceOnly without inventing a decay rate.
    assert(G435Estimator::RecordBluetoothReading(st, 80, 1000));
    assert(st.calibrated);
    assert(st.lastRealPercent == 80);
    assert(st.lastRealTimestampSec == 1000);
    assert(st.learnedRateMilliPctPerHr == 0);
    assert(st.sampleCount == 0);

    // Simulate 2 hours (7200s) of active usage in 60s steps after only 1 reading:
    std::uint64_t t = 1000;
    for (int i = 0; i < 120; i++) {
        t += 60;
        G435Estimator::AccumulateUsage(st, true, t);
    }
    assert(st.activeUsageSec == 7200);
    auto evRef = G435Estimator::EvaluateLightspeed(st);
    assert(evRef.kind == G435Estimator::ReadingKind::ReferenceOnly);
    assert(evRef.percent == 80);
    assert(evRef.referencePercent == 80);
    assert(evRef.estimated);

    // 3. Disconnected / off periods must NOT accumulate active usage or discount battery:
    assert(!G435Estimator::AccumulateUsage(st, false, t + 60));
    assert(st.lastPollTimestampSec == 0);
    t += 36000;  // 10 hours with headset turned off
    assert(!G435Estimator::AccumulateUsage(st, false, t));
    assert(st.activeUsageSec == 7200);

    // 4. Second Bluetooth reading at 70% after 2 hours of active use (10% drop / 2h = 5.0%/h = 5000 m%/h):
    //    - Learns the discharge rate from the headset's observed behavior.
    //    - Resets activeUsageSec and updates reference to 70%.
    assert(G435Estimator::RecordBluetoothReading(st, 70, t));
    assert(st.lastRealPercent == 70);
    assert(st.activeUsageSec == 0);
    assert(st.sampleCount == 1);
    assert(st.learnedRateMilliPctPerHr == 5000);

    // 5. Heuristic estimation during active LIGHTSPEED usage:
    //    - After 1 hour (3600s) of active use at 5%/h, estimated battery should be 65%.
    for (int i = 0; i < 60; i++) {
        t += 60;
        G435Estimator::AccumulateUsage(st, true, t);
    }
    assert(st.activeUsageSec == 3600);
    auto evEst1 = G435Estimator::EvaluateLightspeed(st);
    assert(evEst1.kind == G435Estimator::ReadingKind::Estimated);
    assert(evEst1.percent == 65);
    assert(evEst1.referencePercent == 70);
    assert(evEst1.estimated);

    //    - Headset turned off for 5 hours: estimate must remain 65% (no passive time decay).
    G435Estimator::AccumulateUsage(st, false, t + 60);
    t += 18000;
    G435Estimator::AccumulateUsage(st, false, t);
    auto evEstOff = G435Estimator::EvaluateLightspeed(st);
    assert(evEstOff.percent == 65);

    // 6. Recalibration on a new Bluetooth reading (e.g. 60% after another hour -> 10% drop in 1h = 10%/h):
    //    - EMA blends 70% old (5000) + 30% new (10000) = 6500 m%/h.
    //    - Reference updates to 60%.
    assert(G435Estimator::RecordBluetoothReading(st, 60, t));
    assert(st.lastRealPercent == 60);
    assert(st.activeUsageSec == 0);
    assert(st.sampleCount == 2);
    assert(st.learnedRateMilliPctPerHr == 6500);

    // 7. Recharging the headset (Bluetooth reports 100%):
    //    - Updates reference to 100%, resets activeUsageSec, and preserves the learned discharge rate.
    t += 1800;
    assert(G435Estimator::RecordBluetoothReading(st, 100, t));
    assert(st.lastRealPercent == 100);
    assert(st.activeUsageSec == 0);
    assert(st.learnedRateMilliPctPerHr == 6500);
}

int main() {
    // CRC-16/XMODEM check value.
    const std::uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    assert(G435Adf::Crc16(check, sizeof(check)) == 0x31C3);

    // Frame layout: cmd, body length, CRC (little endian) over the frame with the CRC zeroed.
    auto f = G435Adf::Frame(0x84, { 0x01 });
    assert(f.size() == 64 && f[0] == 0x84 && f[1] == 1 && f[4] == 0x01 && f[5] == 0);
    auto zeroed = f;
    zeroed[2] = zeroed[3] = 0;
    std::uint16_t crc = G435Adf::Crc16(zeroed.data(), zeroed.size());
    assert(f[2] == (crc & 0xFF) && f[3] == (crc >> 8));

    // GET_BATTERY command as sent by the original tool (sequence 5 after the init and mode steps).
    auto pa = G435Adf::Pa(0x06, 5, 1, { 0x00, 0x31 });
    assert((pa == std::vector<std::uint8_t>{ 0x50, 0x41, 0x06, 0x05, 0x01, 0x00, 0x31 }));

    // Battery reply (synthetic, built on the pattern the original tool matches): 3780 mV = 0x0EC4.
    std::vector<std::uint8_t> reply(64, 0);
    const std::uint8_t body[] = { 0x50, 0x49, 0x00, 0x07, 0x00, 0x06, 0x85, 0x00, 0xC4, 0x0E };
    for (size_t i = 0; i < sizeof(body); i++) reply[10 + i] = body[i];
    assert(G435Adf::ParseMillivolts(reply.data(), reply.size()) == 3780);

    // Implausible voltage (dongle answering itself) and missing reply are rejected.
    reply[18] = 0xD2;
    reply[19] = 0x00;
    assert(G435Adf::ParseMillivolts(reply.data(), reply.size()) == -1);
    std::vector<std::uint8_t> empty(64, 0);
    assert(G435Adf::ParseMillivolts(empty.data(), empty.size()) == -1);

    // Voltage curve.
    assert(G435Adf::Percent(4250) == 100 && G435Adf::Percent(4200) == 100);
    assert(G435Adf::Percent(3800) == 55 && G435Adf::Percent(3775) == 50);
    assert(G435Adf::Percent(3400) == 0 && G435Adf::Percent(3000) == 0);

    TestEstimator();

    std::puts("logitech_g435_0adf_test: ok");
    return 0;
}
