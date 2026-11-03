#include "circuit/pg_rc_stage.h"
#include "circuit/pg_resistor.h"
#include "circuit/pg_capacitor.h"
#include "circuit/pg_inductor.h"
#include "circuit/pg_bjt_ebers_moll.h"

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
    using PGAudio::Circuit::Resistor;
    using PGAudio::Circuit::ResistorThermalNoise;
    using PGAudio::Circuit::Capacitor;
    using PGAudio::Circuit::Inductor;
    using PGAudio::Circuit::EbersMollNpn;

    RcStage rc;
    rc.prepare(48000.0, 10000.0, 10.0e-9);
    if (!near(rc.timeConstantSeconds(), 100.0e-6, 1.0e-12)) return 1;
    if (!near(rc.cutoffHz(), 1591.549, 0.01)) return 2;

    double previous = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double value = rc.process(1.0);
        if (value < previous || value > 1.000001) return 3;
        previous = value;
    }
    if (!near(rc.voltage(), 1.0, 1.0e-9)) return 4;

    constexpr int samplesPerCycle = 30;
    constexpr int settleCycles = 40;
    constexpr int measureCycles = 200;
    const double pi = 3.14159265358979323846;
    const double cutoff = 48000.0 / samplesPerCycle;
    const double capacitance = 1.0 / (2.0 * pi * 10000.0 * cutoff);
    rc.prepare(48000.0, 10000.0, capacitance);
    rc.reset();
    double inputSquares = 0.0;
    double outputSquares = 0.0;
    const int totalSamples = (settleCycles + measureCycles) * samplesPerCycle;
    for (int n = 0; n < totalSamples; ++n)
    {
        const double input = std::sin(2.0 * pi * (n % samplesPerCycle) / samplesPerCycle);
        const double output = rc.process(static_cast<float>(input));
        if (n >= settleCycles * samplesPerCycle)
        {
            inputSquares += input * input;
            outputSquares += output * output;
        }
    }
    if (!near(std::sqrt(outputSquares / inputSquares), 0.70710678, 0.002)) return 5;
    rc.reset();
    if (!near(rc.voltage(), 0.0, 1.0e-15)) return 6;

    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        rc.prepare(rate, 600.0, 47.0e-6);
        rc.reset();
        for (int i = 0; i < 1000; ++i)
        {
            const float y = rc.process(0.75f);
            if (!std::isfinite(y) || y < 0.0f || y > 0.750001f) return 7;
        }
    }

    Resistor resistor(10000.0);
    constexpr double boltzmann = 1.380649e-23;
    const double expectedDensity = std::sqrt(4.0 * boltzmann * 293.15 * 10000.0);
    if (!near(resistor.thermalNoiseDensity(), expectedDensity, expectedDensity * 1.0e-12)) return 8;
    const double expectedBandRms = expectedDensity * std::sqrt(20000.0);
    if (!near(resistor.thermalNoiseRms(20000.0), expectedBandRms, expectedBandRms * 1.0e-12)) return 9;
    if (resistor.thermalNoiseRms(0.0) != 0.0 ||
        resistor.thermalNoiseRms(1000.0, -1.0) != 0.0) return 10;

    ResistorThermalNoise noiseA;
    ResistorThermalNoise noiseB;
    noiseA.prepare(48000.0, 10000.0, 293.15, 1234567);
    noiseB.prepare(48000.0, 10000.0, 293.15, 1234567);
    const double expectedNoiseRms = resistor.thermalNoiseRms(24000.0);
    if (!near(noiseA.noiseRmsVolts(), expectedNoiseRms, expectedNoiseRms * 1.0e-12)) return 11;

    constexpr int noiseSamples = 200000;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < noiseSamples; ++i)
    {
        const float a = noiseA.process();
        const float b = noiseB.process();
        if (a != b || !std::isfinite(a)) return 12;
        sum += a;
        sumSquares += static_cast<double>(a) * a;
    }
    const double mean = sum / noiseSamples;
    const double measuredNoiseRms = std::sqrt(sumSquares / noiseSamples - mean * mean);
    if (std::abs(mean) > expectedNoiseRms * 0.015) return 13;
    if (std::abs(measuredNoiseRms - expectedNoiseRms) > expectedNoiseRms * 0.015) return 14;

    Capacitor capacitor;
    constexpr double capValue = 10.0e-6;
    constexpr double leakValue = 10000.0;
    constexpr double chargeCurrent = 1.0e-3;
    capacitor.prepare(48000.0, capValue, 0.5, leakValue);
    const double tau = leakValue * capValue;
    for (int i = 0; i < static_cast<int>(5.0 * tau * 48000.0); ++i)
        capacitor.processCurrent(static_cast<float>(chargeCurrent));
    const double expectedCapVoltage = chargeCurrent * leakValue * (1.0 - std::exp(-5.0));
    if (!near(capacitor.dielectricVoltage(), expectedCapVoltage, 1.0e-4)) return 15;
    if (!near(capacitor.terminalVoltage(), expectedCapVoltage + chargeCurrent * 0.5, 1.0e-4)) return 16;
    capacitor.reset();
    if (!near(capacitor.dielectricVoltage(), 0.0, 1.0e-15)) return 17;

    capacitor.prepare(48000.0, capValue, 0.5);
    for (int i = 0; i < 48000; ++i)
        capacitor.processCurrent(static_cast<float>(chargeCurrent));
    if (!near(capacitor.dielectricVoltage(), 100.0, 0.02)) return 18;
    if (!near(capacitor.terminalVoltage(), 100.0005, 0.02)) return 19;

    Inductor inductor;
    inductor.prepare(48000.0, 1.0, 10.0);
    const double rlTau = 1.0 / 10.0;
    for (int i = 0; i < static_cast<int>(5.0 * rlTau * 48000.0); ++i)
        inductor.processVoltage(1.0f);
    const double expectedCurrent = 0.1 * (1.0 - std::exp(-5.0));
    if (!near(inductor.currentAmps(), expectedCurrent, 1.0e-5)) return 20;
    if (!near(inductor.electricalTimeConstantSeconds(), rlTau, 1.0e-12)) return 21;
    inductor.prepare(48000.0, 1.0, 0.0);
    inductor.reset();
    for (int i = 0; i < 480; ++i)
        inductor.processVoltage(1.0f);
    if (!near(inductor.currentAmps(), 0.01, 1.0e-9)) return 22;

    EbersMollNpn transistor;
    transistor.prepare(1.0e-14, 100.0, 1.0, 300.15);
    const double targetCollectorCurrent = 1.0e-3;
    const double vbe = transistor.thermalVoltage() * std::log1p(
        targetCollectorCurrent / transistor.saturationCurrentAmps());
    const auto forwardActive = transistor.evaluate(vbe, 5.0, 0.0);
    if (!near(forwardActive.collectorAmps, targetCollectorCurrent, 1.0e-8)) return 23;
    if (!near(forwardActive.collectorAmps / forwardActive.baseAmps, 100.0, 0.02)) return 24;
    if (!near(forwardActive.collectorAmps + forwardActive.baseAmps + forwardActive.emitterAmps,
              0.0, 1.0e-15)) return 25;

    const auto saturation = transistor.evaluate(0.72, 0.55, 0.0);
    if (!std::isfinite(saturation.collectorAmps) ||
        !std::isfinite(saturation.baseAmps) ||
        !near(saturation.collectorAmps + saturation.baseAmps + saturation.emitterAmps,
              0.0, 1.0e-12)) return 26;

    EbersMollNpn hotTransistor;
    hotTransistor.prepare(1.0e-14, 100.0, 1.0, 350.0);
    if (!(hotTransistor.thermalVoltage() > transistor.thermalVoltage())) return 27;
    if (!(hotTransistor.saturationCurrentAmps() > transistor.saturationCurrentAmps())) return 28;
    const auto hotAtSameBias = hotTransistor.evaluate(vbe, 5.0, 0.0);
    if (!(hotAtSameBias.collectorAmps > forwardActive.collectorAmps)) return 29;

    std::cout << "PG Circuit Engine: R, L, C and NPN device tests passed.\n";
    return 0;
}