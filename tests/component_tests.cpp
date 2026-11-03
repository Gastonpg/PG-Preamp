#include "circuit/pg_rc_stage.h"
#include "circuit/pg_resistor.h"
#include "circuit/pg_capacitor.h"

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

    // A 10 kOhm / 10 nF network has tau = 100 us and fc ~= 1591.55 Hz.
    RcStage rc;
    rc.prepare(48000.0, 10000.0, 10.0e-9);
    if (!near(rc.timeConstantSeconds(), 100.0e-6, 1.0e-12)) return 1;
    if (!near(rc.cutoffHz(), 1591.549, 0.01)) return 2;

    // Step response should approach the applied voltage monotonically.
    double previous = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double value = rc.process(1.0);
        if (value < previous || value > 1.000001) return 3;
        previous = value;
    }
    if (!near(rc.voltage(), 1.0, 1.0e-9)) return 4;

    // Check the -3 dB point with a coherent 1.6 kHz sine at 48 kHz.
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
    const double measuredGain = std::sqrt(outputSquares / inputSquares);
    if (!near(measuredGain, 0.70710678, 0.002)) return 5;

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

    // Johnson-Nyquist resistor noise density and bandwidth integration.
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

    // Current-driven capacitor with ESR and finite leakage resistance.
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

    std::cout << "PG Circuit Engine: RC, resistor and capacitor tests passed.\n";
    return 0;
}