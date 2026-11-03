#include "circuit/pg_rc_stage.h"

#include <cmath>
#include <iostream>

namespace {
bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    using PGAudio::Circuit::RcStage;

    // A 10 kOhm / 10 nF network has tau = 100 us and fc ~= 1591.55 Hz.
    RcStage rc;
    rc.prepare(48000.0, 10000.0, 10.0e-9);
    if (!near(rc.timeConstantSeconds(), 100.0e-6, 1.0e-12))
        return 1;
    if (!near(rc.cutoffHz(), 1591.549, 0.01))
        return 2;

    // Step response should approach the applied voltage monotonically.
    double previous = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double value = rc.process(1.0);
        if (value < previous || value > 1.000001)
            return 3;
        previous = value;
    }
    if (!near(rc.voltage(), 1.0, 1.0e-9))
        return 4;

    // Reset must restore a known capacitor state.
    rc.reset();
    if (!near(rc.voltage(), 0.0, 1.0e-15))
        return 5;

    // The exact exponential update must remain stable at common sample rates.
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        rc.prepare(rate, 600.0, 47.0e-6);
        rc.reset();
        for (int i = 0; i < 1000; ++i)
        {
            const float y = rc.process(0.75f);
            if (!std::isfinite(y) || y < 0.0f || y > 0.750001f)
                return 6;
        }
    }

    std::cout << "PG Circuit Engine: RC stage tests passed.\n";
    return 0;
}
