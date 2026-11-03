#include "circuit/pg_rc_stage.h"
#include "circuit/pg_resistor.h"
#include "circuit/pg_capacitor.h"
#include "circuit/pg_inductor.h"
#include "circuit/pg_bjt_ebers_moll.h"
#include "circuit/pg_mna_solver.h"
#include "circuit/pg_preisach_core.h"
#include "circuit/pg_magnetic_transformer.h"
#include "circuit/pg_fixed_common_emitter.h"
#include "circuit/pg_a_stage_profile.h"
#include "circuit/pg_fixed_magnetic_transformer.h"
#include "circuit/pg_iron_calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <iostream>

namespace {
bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

template <std::size_t SampleCount>
double dftAmplitude(const std::array<double, SampleCount>& samples, int bin)
{
    double real = 0.0;
    double imaginary = 0.0;
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t n = 0; n < SampleCount; ++n)
    {
        const double phase = 2.0 * pi * bin * static_cast<double>(n) /
                             static_cast<double>(SampleCount);
        real += samples[n] * std::cos(phase);
        imaginary -= samples[n] * std::sin(phase);
    }
    return (2.0 / static_cast<double>(SampleCount)) *
           std::sqrt(real * real + imaginary * imaginary);
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
    using PGAudio::Circuit::BjtAdvancedParameters;
    using PGAudio::Circuit::PreisachCore;
    using PGAudio::Circuit::MagneticTransformer;
    using PGAudio::Circuit::FixedCommonEmitterStage;
    using PGAudio::Circuit::MnaCircuit;

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
    if (!near(resistor.currentForVoltage(2.0), 0.0002, 1.0e-12)) return 8;
    if (!near(resistor.voltageForCurrent(0.0002), 2.0, 1.0e-12)) return 9;
    if (!near(resistor.powerForVoltage(2.0), 0.0004, 1.0e-12)) return 10;

    Resistor tempcoResistor(1000.0);
    tempcoResistor.setTemperatureCoefficients(0.0039, 0.0);
    const double hotResistance = tempcoResistor.resistanceAtTemperature(313.15);
    if (!near(hotResistance, 1078.0, 1.0e-9)) return 11;
    if (!near(tempcoResistor.currentForVoltage(1.0, 313.15), 1.0 / 1078.0, 1.0e-12))
        return 12;

    constexpr double boltzmann = 1.380649e-23;
    const double expectedDensity = std::sqrt(4.0 * boltzmann * 293.15 * 10000.0);
    if (!near(resistor.thermalNoiseDensity(), expectedDensity, expectedDensity * 1.0e-12)) return 13;
    const double expectedBandRms = expectedDensity * std::sqrt(20000.0);
    if (!near(resistor.thermalNoiseRms(20000.0), expectedBandRms, expectedBandRms * 1.0e-12)) return 14;
    if (resistor.thermalNoiseRms(0.0) != 0.0 ||
        resistor.thermalNoiseRms(1000.0, -1.0) != 0.0) return 15;

    ResistorThermalNoise noiseA;
    ResistorThermalNoise noiseB;
    noiseA.prepare(48000.0, 10000.0, 293.15, 1234567);
    noiseB.prepare(48000.0, 10000.0, 293.15, 1234567);
    const double expectedNoiseRms = resistor.thermalNoiseRms(24000.0);
    if (!near(noiseA.noiseRmsVolts(), expectedNoiseRms, expectedNoiseRms * 1.0e-12)) return 16;

    constexpr int noiseSamples = 200000;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < noiseSamples; ++i)
    {
        const float a = noiseA.process();
        const float b = noiseB.process();
        if (a != b || !std::isfinite(a)) return 17;
        sum += a;
        sumSquares += static_cast<double>(a) * a;
    }
    const double mean = sum / noiseSamples;
    const double measuredNoiseRms = std::sqrt(sumSquares / noiseSamples - mean * mean);
    if (std::abs(mean) > expectedNoiseRms * 0.015) return 18;
    if (std::abs(measuredNoiseRms - expectedNoiseRms) > expectedNoiseRms * 0.015) return 19;

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

    // Check the analytic BJT Jacobian against finite differences.
    const auto analytic = transistor.evaluateWithDerivatives(0.68, 5.0, 0.0);
    constexpr double derivativeStep = 1.0e-6;
    const double terminalVoltages[3] = {0.68, 5.0, 0.0};
    for (int column = 0; column < 3; ++column)
    {
        double perturbed[3] = {terminalVoltages[0], terminalVoltages[1], terminalVoltages[2]};
        perturbed[column] += derivativeStep;
        const auto plus = transistor.evaluate(perturbed[0], perturbed[1], perturbed[2]);
        perturbed[column] -= 2.0 * derivativeStep;
        const auto minus = transistor.evaluate(perturbed[0], perturbed[1], perturbed[2]);
        const double plusCurrents[3] = {plus.baseAmps, plus.collectorAmps, plus.emitterAmps};
        const double minusCurrents[3] = {minus.baseAmps, minus.collectorAmps, minus.emitterAmps};
        for (int row = 0; row < 3; ++row)
        {
            const double numerical = (plusCurrents[row] - minusCurrents[row]) /
                                     (2.0 * derivativeStep);
            if (!near(analytic.conductance[row][column], numerical,
                      std::max(1.0e-10, std::abs(numerical) * 1.0e-5)))
                return 30;
        }
    }

    // Advanced transistor: Early effect, ohmic parasitics, depletion
    // capacitances and transit-time diffusion charge.
    EbersMollNpn detailedTransistor;
    detailedTransistor.prepare(2.0e-14, 120.0, 2.0, 300.15);
    BjtAdvancedParameters detailedParameters;
    detailedParameters.forwardEarlyVoltage = 85.0;
    detailedParameters.reverseEarlyVoltage = 45.0;
    detailedParameters.baseResistanceOhms = 120.0;
    detailedParameters.collectorResistanceOhms = 4.0;
    detailedParameters.emitterResistanceOhms = 1.5;
    detailedParameters.baseEmitterCapacitanceFarads = 22.0e-12;
    detailedParameters.baseCollectorCapacitanceFarads = 7.0e-12;
    detailedParameters.forwardTransitTimeSeconds = 0.35e-9;
    detailedParameters.reverseTransitTimeSeconds = 2.0e-9;
    detailedTransistor.setAdvancedParameters(detailedParameters);

    const double detailedVoltages[3] = {0.67, 5.0, 0.0};
    const auto detailedEvaluation = detailedTransistor.evaluateWithDerivatives(
        detailedVoltages[0], detailedVoltages[1], detailedVoltages[2]);
    if (!near(detailedEvaluation.currents.baseAmps +
              detailedEvaluation.currents.collectorAmps +
              detailedEvaluation.currents.emitterAmps, 0.0, 1.0e-14))
        return 50;
    if (!near(detailedEvaluation.terminalChargeCoulombs[0] +
              detailedEvaluation.terminalChargeCoulombs[1] +
              detailedEvaluation.terminalChargeCoulombs[2], 0.0, 1.0e-18))
        return 51;

    // Validate the analytic nonlinear conductance matrix and charge Jacobian
    // against finite differences in all three terminal-voltage directions.
    constexpr double detailedDerivativeStep = 1.0e-6;
    for (int column = 0; column < 3; ++column)
    {
        double plusVoltage[3] = {
            detailedVoltages[0], detailedVoltages[1], detailedVoltages[2]
        };
        plusVoltage[column] += detailedDerivativeStep;
        const auto plus = detailedTransistor.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);
        plusVoltage[column] -= 2.0 * detailedDerivativeStep;
        const auto minus = detailedTransistor.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);

        const double plusCurrent[3] = {
            plus.currents.baseAmps, plus.currents.collectorAmps, plus.currents.emitterAmps
        };
        const double minusCurrent[3] = {
            minus.currents.baseAmps, minus.currents.collectorAmps, minus.currents.emitterAmps
        };
        for (int row = 0; row < 3; ++row)
        {
            const double numericalG = (plusCurrent[row] - minusCurrent[row]) /
                                      (2.0 * detailedDerivativeStep);
            if (!near(detailedEvaluation.conductance[row][column], numericalG,
                      std::max(1.0e-10, std::abs(numericalG) * 1.0e-5)))
                return 52;

            const double numericalC =
                (plus.terminalChargeCoulombs[row] - minus.terminalChargeCoulombs[row]) /
                (2.0 * detailedDerivativeStep);
            if (!near(detailedEvaluation.capacitance[row][column], numericalC,
                      std::max(1.0e-15, std::abs(numericalC) * 1.0e-4)))
                return 53;
        }
    }

    // Optional Gummel-Poon-style terms: high-current beta roll-off and
    // explicit BE / BC leakage branches. They are tested here but remain
    // disabled in the current PG A Stage M1 voicing until device fitting.
    BjtAdvancedParameters extendedParameters = detailedParameters;
    extendedParameters.baseEmitterLeakageSaturationCurrentAmps = 1.0e-14;
    extendedParameters.baseCollectorLeakageSaturationCurrentAmps = 1.0e-12;
    extendedParameters.forwardBetaRollOffCurrentAmps = 2.0e-3;
    extendedParameters.reverseBetaRollOffCurrentAmps = 1.0e-3;
    EbersMollNpn extendedTransistor;
    extendedTransistor.prepare(2.0e-14, 120.0, 2.0, 300.15);
    extendedTransistor.setAdvancedParameters(extendedParameters);

    const double extendedVoltages[3] = {0.64, 0.35, 0.0};
    const auto extendedEvaluation = extendedTransistor.evaluateWithDerivatives(
        extendedVoltages[0], extendedVoltages[1], extendedVoltages[2]);
    if (!near(extendedEvaluation.currents.baseAmps +
              extendedEvaluation.currents.collectorAmps +
              extendedEvaluation.currents.emitterAmps, 0.0, 1.0e-12))
        return 123;

    constexpr double extendedDerivativeStep = 1.0e-7;
    for (int column = 0; column < 3; ++column)
    {
        double plusVoltage[3] = {
            extendedVoltages[0], extendedVoltages[1], extendedVoltages[2]
        };
        plusVoltage[column] += extendedDerivativeStep;
        const auto plus = extendedTransistor.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);
        plusVoltage[column] -= 2.0 * extendedDerivativeStep;
        const auto minus = extendedTransistor.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);

        const double plusCurrent[3] = {
            plus.currents.baseAmps, plus.currents.collectorAmps, plus.currents.emitterAmps
        };
        const double minusCurrent[3] = {
            minus.currents.baseAmps, minus.currents.collectorAmps, minus.currents.emitterAmps
        };
        for (int row = 0; row < 3; ++row)
        {
            const double numericalG = (plusCurrent[row] - minusCurrent[row]) /
                                      (2.0 * extendedDerivativeStep);
            if (!near(extendedEvaluation.conductance[row][column], numericalG,
                      std::max(1.0e-9, std::abs(numericalG) * 2.0e-4)))
                return 124;
        }
    }

    // A base-collector leakage diode must exchange current between B and C,
    // not incorrectly route it through the emitter branch.
    EbersMollNpn noCollectorLeakage;
    noCollectorLeakage.prepare(2.0e-14, 120.0, 2.0, 300.15);
    BjtAdvancedParameters noLeakageParameters = detailedParameters;
    noLeakageParameters.baseCollectorLeakageSaturationCurrentAmps = 0.0;
    noCollectorLeakage.setAdvancedParameters(noLeakageParameters);
    const auto withoutBcLeak = noCollectorLeakage.evaluate(0.65, 0.0, 0.0);

    EbersMollNpn withCollectorLeakage;
    withCollectorLeakage.prepare(2.0e-14, 120.0, 2.0, 300.15);
    BjtAdvancedParameters bcLeakageParameters = noLeakageParameters;
    bcLeakageParameters.baseCollectorLeakageSaturationCurrentAmps = 1.0e-12;
    bcLeakageParameters.baseCollectorLeakageEmissionCoefficient = 2.0;
    withCollectorLeakage.setAdvancedParameters(bcLeakageParameters);
    const auto withBcLeak = withCollectorLeakage.evaluate(0.65, 0.0, 0.0);
    const double expectedBcLeak = 1.0e-12 * std::expm1(
        0.65 / (2.0 * withCollectorLeakage.thermalVoltage()));
    if (!near(withBcLeak.baseAmps - withoutBcLeak.baseAmps,
              expectedBcLeak, expectedBcLeak * 1.0e-5) ||
        !near(withBcLeak.collectorAmps - withoutBcLeak.collectorAmps,
              -expectedBcLeak, expectedBcLeak * 1.0e-5) ||
        !near(withBcLeak.emitterAmps - withoutBcLeak.emitterAmps,
              0.0, expectedBcLeak * 1.0e-5))
        return 125;

    // Finite IKF reduces common-emitter beta as collector current rises.
    EbersMollNpn noRollOff;
    noRollOff.prepare(2.0e-14, 120.0, 2.0, 300.15);
    BjtAdvancedParameters noRollOffParameters = detailedParameters;
    noRollOffParameters.forwardBetaRollOffCurrentAmps =
        std::numeric_limits<double>::infinity();
    noRollOff.setAdvancedParameters(noRollOffParameters);

    EbersMollNpn withRollOff;
    withRollOff.prepare(2.0e-14, 120.0, 2.0, 300.15);
    BjtAdvancedParameters rollOffParameters = detailedParameters;
    rollOffParameters.forwardBetaRollOffCurrentAmps = 2.0e-3;
    withRollOff.setAdvancedParameters(rollOffParameters);
    const auto lowCurrent = withRollOff.evaluate(0.60, 5.0, 0.0);
    const auto highCurrent = withRollOff.evaluate(0.68, 5.0, 0.0);
    const auto highCurrentWithoutRollOff = noRollOff.evaluate(0.68, 5.0, 0.0);
    const double lowBeta = lowCurrent.collectorAmps / lowCurrent.baseAmps;
    const double highBeta = highCurrent.collectorAmps / highCurrent.baseAmps;
    const double unrolledHighBeta =
        highCurrentWithoutRollOff.collectorAmps /
        highCurrentWithoutRollOff.baseAmps;
    if (!(highCurrent.collectorAmps > lowCurrent.collectorAmps) ||
        !(highBeta < lowBeta) ||
        !(highBeta < unrolledHighBeta * 0.8))
        return 126;

    // Early voltage must increase forward-active collector current with VCE.
    const auto lowVce = detailedTransistor.evaluate(0.67, 2.0, 0.0);
    const auto highVce = detailedTransistor.evaluate(0.67, 8.0, 0.0);
    if (!(highVce.collectorAmps > lowVce.collectorAmps))
        return 54;

    // Hysteretic magnetic core: remanence, coercive switching, saturation,
    // closed-loop hysteresis work and non-negative eddy-current loss.
    PreisachCore<512> magneticCore;
    if (!magneticCore.prepare(1.25, 1.0e-4, 0.08, 2.0e-4))
        return 55;
    if (magneticCore.configureSymmetricDomains(15.0, 900.0, 256) != 256)
        return 56;

    constexpr double coreDt = 1.0e-5;
    auto coreSample = magneticCore.advance(1200.0, coreDt);
    if (!(coreSample.fluxDensityTesla > 1.0))
        return 57;
    coreSample = magneticCore.advance(0.0, coreDt);
    if (!(coreSample.fluxDensityTesla > 0.5))
        return 58; // positive remanent flux after positive saturation

    magneticCore.reset();
    magneticCore.advance(-1200.0, coreDt);
    magneticCore.resetLossAccumulator();
    constexpr int coreHalfCycleSteps = 1200;
    for (int step = 0; step <= coreHalfCycleSteps; ++step)
    {
        const double field = -1200.0 + 2400.0 * step / coreHalfCycleSteps;
        coreSample = magneticCore.advance(field, coreDt);
        if (!std::isfinite(coreSample.fluxDensityTesla) ||
            !std::isfinite(coreSample.eddyLossDensityWattsPerM3) ||
            coreSample.eddyLossDensityWattsPerM3 < 0.0)
            return 59;
    }
    for (int step = 0; step <= coreHalfCycleSteps; ++step)
    {
        const double field = 1200.0 - 2400.0 * step / coreHalfCycleSteps;
        coreSample = magneticCore.advance(field, coreDt);
    }
    if (!(magneticCore.hysteresisWorkDensity() > 0.0))
        return 60;

    // After negative saturation, zero applied field retains negative
    // remanent flux instead of returning to the virgin B=0 state.
    magneticCore.advance(-1200.0, coreDt);
    const auto remanentState = magneticCore.advance(0.0, coreDt);
    if (!(remanentState.fluxDensityTesla < -0.5))
        return 61;

    // Use the detailed device in subsequent assembled amplifier tests too.
    transistor.setAdvancedParameters(detailedParameters);

    // RC transient solved as an assembled network (1 kOhm driving 1 uF).
    MnaCircuit rcNetwork;
    const auto rcInput = rcNetwork.createNode();
    const auto rcOutput = rcNetwork.createNode();
    rcNetwork.addVoltageSource(rcInput, MnaCircuit::ground, 1.0);
    rcNetwork.addResistor(rcInput, rcOutput, 1000.0);
    rcNetwork.addCapacitor(rcOutput, MnaCircuit::ground, 1.0e-6);
    constexpr double solverDt = 1.0 / 48000.0;
    const double rcAlpha = solverDt / (1000.0 * 1.0e-6 + solverDt);
    double expectedRcOutput = 0.0;
    for (int sample = 0; sample < 120; ++sample)
    {
        const auto solved = rcNetwork.step(solverDt, 293.15, 80, 1.0e-14);
        if (!solved.converged)
            return 31;
        expectedRcOutput += rcAlpha * (1.0 - expectedRcOutput);
    }
    if (!near(rcNetwork.nodeVoltage(rcOutput), expectedRcOutput, 1.0e-8))
    {
        std::cerr.precision(17);
        std::cerr << "RC MNA mismatch: actual=" << rcNetwork.nodeVoltage(rcOutput)
                  << " expected=" << expectedRcOutput << "\n";
        return 32;
    }

    // RL transient solved as a branch-current MNA unknown.
    MnaCircuit rlNetwork;
    const auto rlSupply = rlNetwork.createNode();
    rlNetwork.addVoltageSource(rlSupply, MnaCircuit::ground, 1.0);
    rlNetwork.addInductor(rlSupply, MnaCircuit::ground, 1.0, 10.0);
    double expectedRlCurrent = 0.0;
    const double rlAlpha = solverDt / (1.0 / 10.0 + solverDt);
    for (int sample = 0; sample < 120; ++sample)
    {
        const auto solved = rlNetwork.step(solverDt, 293.15, 80, 1.0e-14);
        if (!solved.converged)
            return 33;
        expectedRlCurrent += rlAlpha * (0.1 - expectedRlCurrent);
    }
    if (!near(rlNetwork.inductorCurrent(0), expectedRlCurrent, 1.0e-8))
        return 34;

    // Two linearly coupled windings loaded on the secondary. For a turns
    // ratio of 0.1, their inductances have a 100:1 ratio. The test checks the
    // voltage ratio with finite winding resistance and a load.
    MnaCircuit transformerNetwork;
    const auto transformerSource = transformerNetwork.createNode();
    const auto transformerPrimary = transformerNetwork.createNode();
    const auto transformerSecondary = transformerNetwork.createNode();
    const auto transformerSourceId =
        transformerNetwork.addVoltageSource(transformerSource, MnaCircuit::ground, 0.0);
    transformerNetwork.addResistor(transformerSource, transformerPrimary, 10.0);
    const auto primaryWinding =
        transformerNetwork.addInductor(transformerPrimary, MnaCircuit::ground, 100.0, 5.0);
    const auto secondaryWinding =
        transformerNetwork.addInductor(transformerSecondary, MnaCircuit::ground, 1.0, 0.5);
    if (!transformerNetwork.addMutualCoupling(primaryWinding, secondaryWinding, 0.999))
        return 45;
    transformerNetwork.addResistor(transformerSecondary, MnaCircuit::ground, 100.0);

    double primarySquares = 0.0;
    double secondarySquares = 0.0;
    constexpr int transformerSamples = 960;
    for (int sample = 0; sample < transformerSamples; ++sample)
    {
        const double input = std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        if (!transformerNetwork.setVoltageSource(transformerSourceId, input))
            return 46;
        const auto solved = transformerNetwork.step(solverDt, 293.15, 80, 1.0e-9);
        if (!solved.converged)
            return 47;
        if (sample >= 480)
        {
            const double primaryVoltage = transformerNetwork.nodeVoltage(transformerPrimary);
            const double secondaryVoltage = transformerNetwork.nodeVoltage(transformerSecondary);
            primarySquares += primaryVoltage * primaryVoltage;
            secondarySquares += secondaryVoltage * secondaryVoltage;
        }
    }
    const double turnsRatioMeasured = std::sqrt(secondarySquares / primarySquares);
    if (!(turnsRatioMeasured > 0.085 && turnsRatioMeasured < 0.115))
        return 48;

    // Complete common-emitter bias network: divider, collector/emitter
    // resistors and a nonlinear NPN transistor are solved simultaneously.
    MnaCircuit amplifierBias;
    const auto supply = amplifierBias.createNode();
    const auto base = amplifierBias.createNode();
    const auto collector = amplifierBias.createNode();
    const auto emitter = amplifierBias.createNode();
    amplifierBias.addVoltageSource(supply, MnaCircuit::ground, 12.0);
    amplifierBias.addResistor(supply, base, 100000.0);
    amplifierBias.addResistor(base, MnaCircuit::ground, 18000.0);
    amplifierBias.addResistor(supply, collector, 4700.0);
    amplifierBias.addResistor(emitter, MnaCircuit::ground, 1000.0);
    amplifierBias.addNpn(base, collector, emitter, transistor);
    amplifierBias.setInitialNodeVoltage(supply, 12.0);
    amplifierBias.setInitialNodeVoltage(base, 1.65);
    amplifierBias.setInitialNodeVoltage(collector, 7.5);
    amplifierBias.setInitialNodeVoltage(emitter, 0.95);
    const auto biasResult = amplifierBias.step(solverDt, 300.15, 100, 1.0e-9);
    if (!biasResult.converged)
        return 35;
    if (!near(amplifierBias.nodeVoltage(supply), 12.0, 1.0e-8))
        return 36;
    if (!(amplifierBias.nodeVoltage(base) > 1.4 &&
          amplifierBias.nodeVoltage(base) < 1.8))
        return 37;
    if (!(amplifierBias.nodeVoltage(emitter) > 0.7 &&
          amplifierBias.nodeVoltage(emitter) < 1.2))
        return 38;
    if (!(amplifierBias.nodeVoltage(collector) > 5.0 &&
          amplifierBias.nodeVoltage(collector) < 10.0))
        return 39;

    // Full common-emitter stage with both coupling capacitors, source
    // resistance, divider bias, collector/emitter resistors and output load.
    MnaCircuit amplifier;
    const auto ampSupply = amplifier.createNode();
    const auto signalSource = amplifier.createNode();
    const auto coupledInput = amplifier.createNode();
    const auto ampBase = amplifier.createNode();
    const auto ampCollector = amplifier.createNode();
    const auto ampEmitter = amplifier.createNode();
    const auto ampOutput = amplifier.createNode();

    amplifier.setInitialNodeVoltage(ampSupply, 12.0);
    amplifier.setInitialNodeVoltage(signalSource, 0.0);
    amplifier.setInitialNodeVoltage(coupledInput, 0.0);
    amplifier.setInitialNodeVoltage(ampBase, 1.65);
    amplifier.setInitialNodeVoltage(ampCollector, 7.5);
    amplifier.setInitialNodeVoltage(ampEmitter, 0.95);
    amplifier.setInitialNodeVoltage(ampOutput, 0.0);

    amplifier.addVoltageSource(ampSupply, MnaCircuit::ground, 12.0);
    const auto signalSourceId =
        amplifier.addVoltageSource(signalSource, MnaCircuit::ground, 0.0);
    amplifier.addResistor(ampSupply, ampBase, 100000.0);
    amplifier.addResistor(ampBase, MnaCircuit::ground, 18000.0);
    amplifier.addResistor(ampSupply, ampCollector, 4700.0);
    amplifier.addResistor(ampEmitter, MnaCircuit::ground, 1000.0);
    amplifier.addResistor(signalSource, coupledInput, 600.0);
    amplifier.addResistor(ampOutput, MnaCircuit::ground, 100000.0);
    amplifier.addNpn(ampBase, ampCollector, ampEmitter, transistor);
    amplifier.addCapacitor(coupledInput, ampBase, 10.0e-6, 0.5, 1.0e9, -1.65);
    amplifier.addCapacitor(ampCollector, ampOutput, 10.0e-6, 0.5, 1.0e9, 7.5);

    const auto initialAmpSolve = amplifier.step(solverDt, 300.15, 100, 1.0e-9);
    if (!initialAmpSolve.converged)
        return 40;

    double inputOutputCorrelation = 0.0;
    double amplifierInputSquares = 0.0;
    double amplifierOutputSquares = 0.0;
    double outputSum = 0.0;
    double outputPeak = -std::numeric_limits<double>::infinity();
    double outputTrough = std::numeric_limits<double>::infinity();
    constexpr int ampSamples = 480;
    constexpr int measurementStart = 48;
    constexpr double inputAmplitude = 0.005;
    int measuredSamples = 0;
    for (int sample = 0; sample < ampSamples; ++sample)
    {
        const double input = inputAmplitude *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        if (!amplifier.setVoltageSource(signalSourceId, input))
            return 41;
        const auto solved = amplifier.step(solverDt, 300.15, 100, 1.0e-9);
        if (!solved.converged)
            return 42;

        const double output = amplifier.nodeVoltage(ampOutput);
        if (sample >= measurementStart)
        {
            inputOutputCorrelation += input * output;
            amplifierInputSquares += input * input;
            amplifierOutputSquares += output * output;
            outputSum += output;
            outputPeak = std::max(outputPeak, output);
            outputTrough = std::min(outputTrough, output);
            ++measuredSamples;
        }
    }

    const double outputMean = outputSum / measuredSamples;
    const double outputAcRms = std::sqrt(std::max(
        0.0, amplifierOutputSquares / measuredSamples - outputMean * outputMean));
    const double inputRms = std::sqrt(amplifierInputSquares / measuredSamples);
    const double measuredVoltageGain = outputAcRms / inputRms;
    const double mnaReferenceVoltageGain = measuredVoltageGain;

    if (!(outputPeak - outputTrough > 0.02 && measuredVoltageGain > 2.0))
    {
        std::cerr.precision(17);
        std::cerr << "CE AC measurement: peak=" << outputPeak
                  << " trough=" << outputTrough
                  << " gain=" << measuredVoltageGain
                  << " correlation=" << inputOutputCorrelation << "\n";
        return 43;
    }
    if (!(inputOutputCorrelation < 0.0))
        return 44;

    // Current-driven nonlinear transformer. In the unswitched low-field
    // region, Faraday's law must preserve the turns-voltage ratio.
    MagneticTransformer<256> linearCoreTransformer;
    if (!linearCoreTransformer.prepare(1.2, 1.0e-4, 0.08,
                                       100.0, 10.0, 0.0, 0.0,
                                       0.0, 0.0, 0.0))
        return 62;
    if (linearCoreTransformer.configureCoreDomains(1000.0, 2000.0, 128) != 128)
        return 63;

    typename MagneticTransformer<256>::Sample transformerSample;
    for (int sample = 0; sample < 480; ++sample)
    {
        const double primaryCurrent = 0.01 * (sample + 1) / 480.0;
        transformerSample =
            linearCoreTransformer.processCurrents(primaryCurrent, 0.0, solverDt);
    }
    if (!(std::abs(transformerSample.primaryVoltage) > 1.0e-9))
        return 64;
    if (!near(transformerSample.secondaryVoltage /
              transformerSample.primaryVoltage, 0.1, 1.0e-10))
        return 65;
    if (!near(transformerSample.secondaryFluxLinkageWebers /
              transformerSample.primaryFluxLinkageWebers, 0.1, 1.0e-12))
        return 66;

    // A higher magnetizing current drives the domain ensemble toward
    // saturation, then a reverse cycle leaves remanent core flux.
    MagneticTransformer<256> saturatedTransformer;
    if (!saturatedTransformer.prepare(1.2, 1.0e-4, 0.08,
                                       500.0, 50.0, 2.0, 0.5,
                                       1.0e-3, 1.0e-5, 2.0e-4))
        return 67;
    if (saturatedTransformer.configureCoreDomains(20.0, 400.0, 256) != 256)
        return 68;
    auto nonlinearTransformerSample =
        saturatedTransformer.processCurrents(0.5, 0.0, solverDt);
    if (!(nonlinearTransformerSample.coreFluxDensityTesla > 1.0))
        return 69;
    for (int sample = 0; sample < 480; ++sample)
    {
        const double current = -0.5 * (sample + 1) / 480.0;
        nonlinearTransformerSample =
            saturatedTransformer.processCurrents(current, 0.0, solverDt);
        if (!std::isfinite(nonlinearTransformerSample.primaryVoltage) ||
            !std::isfinite(nonlinearTransformerSample.secondaryVoltage) ||
            !std::isfinite(nonlinearTransformerSample.coreEddyLossPowerWatts) ||
            nonlinearTransformerSample.coreEddyLossPowerWatts < 0.0)
            return 70;
    }
    nonlinearTransformerSample =
        saturatedTransformer.processCurrents(0.0, 0.0, solverDt);
    if (!(std::abs(nonlinearTransformerSample.coreFluxDensityTesla) > 0.5))
        return 71;

    // The processor's provisional transistor model must start from its solved
    // DC bias. Silence immediately after prepare must not produce an output pop.
    auto pluginDefaultTransistor =
        PGAudio::Circuit::PgAStageProfileV1::makeTransistor();
    FixedCommonEmitterStage startupStage;
    if (!startupStage.prepare(
            48000.0,
            PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
            pluginDefaultTransistor))
        return 83;

    double startupSilencePeak = 0.0;
    for (int sample = 0; sample < 256; ++sample)
    {
        FixedCommonEmitterStage::ProcessResult startupResult;
        if (!startupStage.processSample(0.0f, startupResult))
            return 84;
        startupSilencePeak = std::max(
            startupSilencePeak, std::abs(static_cast<double>(startupResult.output)));
    }
    if (startupSilencePeak > 1.0e-5)
    {
        std::cerr << "Startup silence transient: peak=" << startupSilencePeak << "\\n";
        return 85;
    }

    // Measure the exact provisional transistor used by the VST3 processor,
    // not the more generic transistor used in the independent topology test.
    FixedCommonEmitterStage pluginStage;
    if (!pluginStage.prepare(
            48000.0,
            PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
            pluginDefaultTransistor))
        return 106;

    // Verify that the solved quiescent point is genuinely forward-active.
    // This establishes a Class-A bias condition at rest; the following
    // full-cycle test checks that the usable input envelope does not cut off.
    const auto& pluginBias = pluginStage.biasOperatingPoint();
    const double biasCurrentSum = pluginBias.collectorCurrentAmps +
        pluginBias.baseCurrentAmps + pluginBias.emitterCurrentAmps;
    if (!pluginStage.isBiasReady() || !pluginBias.converged ||
        !(pluginBias.collectorCurrentAmps > 0.0) ||
        !(pluginBias.emitterCurrentAmps < 0.0) ||
        !(pluginBias.collectorEmitterVolts > 1.0) ||
        std::abs(biasCurrentSum) > 1.0e-9)
        return 111;
    std::cout << "PG A Stage M1 bias: Ic="
              << pluginBias.collectorCurrentAmps * 1000.0
              << " mA, Vce=" << pluginBias.collectorEmitterVolts
              << " V, residual=" << pluginBias.residualAmps << " A\\n";

    double pluginStageInputSquares = 0.0;
    double pluginStageOutputSquares = 0.0;
    double pluginStageOutputSum = 0.0;
    double pluginStageCorrelation = 0.0;
    constexpr int pluginStageSamples = 960;
    constexpr int pluginStageMeasurementStart = 96;
    for (int sample = 0; sample < pluginStageSamples; ++sample)
    {
        const double input = 0.005 *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        FixedCommonEmitterStage::ProcessResult pluginStageResult;
        if (!pluginStage.processSample(
                static_cast<float>(input), pluginStageResult))
            return 107;
        if (sample >= pluginStageMeasurementStart)
        {
            pluginStageInputSquares += input * input;
            pluginStageOutputSquares +=
                static_cast<double>(pluginStageResult.output) *
                pluginStageResult.output;
            pluginStageOutputSum += pluginStageResult.output;
            pluginStageCorrelation += input * pluginStageResult.output;
        }
    }
    const double pluginStageMeasuredSamples =
        pluginStageSamples - pluginStageMeasurementStart;
    const double pluginStageOutputMean =
        pluginStageOutputSum / pluginStageMeasuredSamples;
    const double pluginStageGain = std::sqrt(std::max(
        0.0, pluginStageOutputSquares / pluginStageMeasuredSamples -
             pluginStageOutputMean * pluginStageOutputMean) /
        (pluginStageInputSquares / pluginStageMeasuredSamples));
    if (!(pluginStageGain > 2.0 && pluginStageGain < 8.0) ||
        !near(pluginStageGain, 4.14469, 0.01))
        return 108;
    if (!(pluginStageCorrelation < 0.0))
        return 109;
    std::cout << "Plugin A Stage reference gain=" << pluginStageGain << "x\\n";

    // Full-cycle headroom: at the maximum nominal +18 dB input setting,
    // a full-scale host sample maps to roughly 40 mV peak at this circuit
    // boundary. The transistor must remain conducting through the waveform.
    FixedCommonEmitterStage classAHeadroomStage;
    if (!classAHeadroomStage.prepare(
            48000.0,
            PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
            pluginDefaultTransistor))
        return 112;
    double minimumCollectorCurrent = std::numeric_limits<double>::infinity();
    double minimumCollectorEmitterVolts = std::numeric_limits<double>::infinity();
    constexpr double maximumNominalInputPeakVolts = 0.04;
    constexpr int classAHeadroomSamples = 4800;
    for (int sample = 0; sample < classAHeadroomSamples; ++sample)
    {
        const double input = maximumNominalInputPeakVolts *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        FixedCommonEmitterStage::ProcessResult result;
        if (!classAHeadroomStage.processSample(static_cast<float>(input), result))
            return 113;
        minimumCollectorCurrent = std::min(
            minimumCollectorCurrent, result.collectorCurrentAmps);
        minimumCollectorEmitterVolts = std::min(
            minimumCollectorEmitterVolts, result.collectorEmitterVolts);
        if (!(result.collectorCurrentAmps > 0.0) ||
            !(result.collectorEmitterVolts > 0.2))
            return 114;
    }
    std::cout << "PG A Stage M1 +18 dB full-scale headroom: minimum Ic="
              << minimumCollectorCurrent * 1000.0
              << " mA, minimum Vce=" << minimumCollectorEmitterVolts
              << " V (no cutoff)\\n";

    // Measure distortion at two useful drive levels with an integer-cycle
    // DFT window. This is a deterministic circuit-model regression, not a
    // physical hardware measurement.
    const auto measureStageThd = [&](double peakVolts)
    {
        FixedCommonEmitterStage measurementStage;
        if (!measurementStage.prepare(
                48000.0,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                pluginDefaultTransistor))
            return std::numeric_limits<double>::quiet_NaN();

        constexpr int totalSamples = 9600;
        constexpr int discardSamples = 4800;
        constexpr int measuredCount = totalSamples - discardSamples;
        std::array<double, measuredCount> measuredOutput{};
        for (int sample = 0; sample < totalSamples; ++sample)
        {
            const double input = peakVolts *
                std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
            FixedCommonEmitterStage::ProcessResult result;
            if (!measurementStage.processSample(static_cast<float>(input), result))
                return std::numeric_limits<double>::quiet_NaN();
            if (sample >= discardSamples)
                measuredOutput[static_cast<std::size_t>(sample - discardSamples)] =
                    result.output;
        }

        const double fundamental = dftAmplitude(measuredOutput, 100);
        double harmonicPower = 0.0;
        for (int harmonic = 2; harmonic <= 23; ++harmonic)
        {
            const double amplitude = dftAmplitude(measuredOutput, 100 * harmonic);
            harmonicPower += amplitude * amplitude;
        }
        return fundamental > 0.0 ? std::sqrt(harmonicPower) / fundamental
                                 : std::numeric_limits<double>::quiet_NaN();
    };

    const double lowDriveThd = measureStageThd(0.005);
    const double nominalMaximumDriveThd = measureStageThd(0.04);
    if (!std::isfinite(lowDriveThd) ||
        !std::isfinite(nominalMaximumDriveThd) ||
        !(nominalMaximumDriveThd > lowDriveThd))
        return 115;
    std::cout << "PG A Stage M1 THD at 1 kHz: 5 mV="
              << lowDriveThd * 100.0 << "%, 40 mV="
              << nominalMaximumDriveThd * 100.0 << "%\\n";

    // Small-signal frequency-response survey at integer-cycle frequencies.
    constexpr double stageSweepRate = 240000.0;
    const std::array<int, 8> stagePeriods = {
        12000, 4800, 2400, 240, 48, 24, 16, 12
    }; // 20 Hz, 50 Hz, 100 Hz, 1 kHz, 5 kHz, 10 kHz, 15 kHz, 20 kHz
    std::array<double, stagePeriods.size()> stageFrequencyGains{};
    double stageOneKhzGain = 0.0;
    double maximumStageResponseDeviationDb = 0.0;
    for (std::size_t frequencyIndex = 0;
         frequencyIndex < stagePeriods.size(); ++frequencyIndex)
    {
        const int period = stagePeriods[frequencyIndex];
        FixedCommonEmitterStage sweepStage;
        if (!sweepStage.prepare(
                stageSweepRate,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                pluginDefaultTransistor))
            return 116;

        const int settleSamples = period * 6;
        const int measurementSamples = period * 4;
        double inputEnergy = 0.0;
        double outputEnergy = 0.0;
        for (int sample = 0; sample < settleSamples + measurementSamples; ++sample)
        {
            const double input = 0.005 *
                std::sin(2.0 * pi * sample / static_cast<double>(period));
            FixedCommonEmitterStage::ProcessResult result;
            if (!sweepStage.processSample(static_cast<float>(input), result))
                return 117;
            if (sample >= settleSamples)
            {
                inputEnergy += input * input;
                outputEnergy += static_cast<double>(result.output) * result.output;
            }
        }
        const double gain = std::sqrt(outputEnergy / inputEnergy);
        if (!std::isfinite(gain) || !(gain > 0.0))
            return 118;
        stageFrequencyGains[frequencyIndex] = gain;
        if (frequencyIndex == 3)
            stageOneKhzGain = gain;
    }
    if (!(stageOneKhzGain > 0.0))
        return 119;
    for (std::size_t frequencyIndex = 0;
         frequencyIndex < stageFrequencyGains.size(); ++frequencyIndex)
    {
        const double deviationDb = 20.0 * std::log10(
            stageFrequencyGains[frequencyIndex] / stageOneKhzGain);
        maximumStageResponseDeviationDb = std::max(
            maximumStageResponseDeviationDb, std::abs(deviationDb));
    }
    std::cout << "PG A Stage M1 20 Hz..20 kHz sweep: 1 kHz gain="
              << stageOneKhzGain << "x, max deviation="
              << maximumStageResponseDeviationDb << " dB\\n";


    // Real-time loaded transformer: the primary is driven through a source
    // resistance, the secondary is terminated, and winding currents satisfy
    // Ampere's law while induced voltages obey Faraday's law.
    using FixedMagneticTransformerStage =
        PGAudio::Circuit::FixedMagneticTransformerStage;
    auto transformerParameters =
        PGAudio::Circuit::PgIronCalibrationV1::makeParameters();
    FixedMagneticTransformerStage transformerStage;
    if (!near(transformerParameters.primaryTurns, 1000.0, 1.0e-12) ||
        !near(transformerParameters.secondaryTurns, 2000.0, 1.0e-12) ||
        !near(transformerParameters.sourceResistanceOhms, 150.0, 1.0e-12) ||
        !near(transformerParameters.primaryWindingResistanceOhms, 12.0, 1.0e-12) ||
        !near(transformerParameters.secondaryWindingResistanceOhms, 65.0, 1.0e-12) ||
        !near(transformerParameters.loadResistanceOhms, 1200.0, 1.0e-12) ||
        !near(PGAudio::Circuit::PgIronCalibrationV1::smallSignalReferenceGain,
              1.2545, 1.0e-12))
        return 110;
    if (!transformerStage.prepare(48000.0, transformerParameters))
        return 86;

    double transformerStartupPeak = 0.0;
    for (int sample = 0; sample < 256; ++sample)
    {
        FixedMagneticTransformerStage::ProcessResult transformerResult;
        if (!transformerStage.processSample(0.0f, transformerResult))
            return 87;
        transformerStartupPeak = std::max(
            transformerStartupPeak,
            std::abs(static_cast<double>(transformerResult.outputVolts)));
    }
    if (transformerStartupPeak > 1.0e-7)
        return 88;

    double transformerInputSquares = 0.0;
    double transformerOutputSquares = 0.0;
    double transformerFluxPeak = 0.0;
    constexpr int transformerAudioSamples = 4800;
    constexpr int transformerMeasurementStart = 480;
    constexpr int transformerMeasurementSamples =
        transformerAudioSamples - transformerMeasurementStart;
    std::array<double, transformerMeasurementSamples> transformerAudioOutput{};
    for (int sample = 0; sample < transformerAudioSamples; ++sample)
    {
        const double input = 0.005 *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        FixedMagneticTransformerStage::ProcessResult transformerResult;
        if (!transformerStage.processSample(
                static_cast<float>(input), transformerResult))
            return 89;
        if (!std::isfinite(transformerResult.outputVolts) ||
            !std::isfinite(transformerResult.primaryCurrentAmps) ||
            !std::isfinite(transformerResult.secondaryCurrentAmps) ||
            !std::isfinite(transformerResult.fluxDensityTesla))
            return 90;
        transformerFluxPeak = std::max(
            transformerFluxPeak, std::abs(transformerResult.fluxDensityTesla));
        if (sample >= transformerMeasurementStart)
        {
            const std::size_t measuredIndex =
                static_cast<std::size_t>(sample - transformerMeasurementStart);
            transformerAudioOutput[measuredIndex] = transformerResult.outputVolts;
            transformerInputSquares += input * input;
            transformerOutputSquares +=
                static_cast<double>(transformerResult.outputVolts) *
                transformerResult.outputVolts;
        }
    }
    const double transformerGain = std::sqrt(
        transformerOutputSquares / transformerInputSquares);
    if (!(transformerGain > 0.95 && transformerGain < 1.5) ||
        !near(transformerGain, 1.2545, 0.002))
        return 91;
    if (!near(transformerStage.turnsRatio(), 2.0, 1.0e-12))
        return 99;

    // The smooth core model must keep a clean single tone periodic. Measure
    // a non-harmonic DFT bin to catch broadband reversal artifacts that can
    // sound like hiss even when the input itself is a pure sine.
    const auto spectralAmplitude = [&](int bin)
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (int n = 0; n < transformerMeasurementSamples; ++n)
        {
            const double phase = 2.0 * pi * bin * n /
                static_cast<double>(transformerMeasurementSamples);
            real += transformerAudioOutput[static_cast<std::size_t>(n)] *
                    std::cos(phase);
            imaginary -= transformerAudioOutput[static_cast<std::size_t>(n)] *
                         std::sin(phase);
        }
        const double scale = 2.0 / transformerMeasurementSamples;
        return scale * std::sqrt(real * real + imaginary * imaginary);
    };
    const double transformerFundamental = spectralAmplitude(90);
    const double transformerNonHarmonicSpur = std::max(
        spectralAmplitude(101),
        std::max(spectralAmplitude(137), spectralAmplitude(173)));

    // Estimate the complete non-harmonic spectral residual (noise-like bins),
    // not just a few hand-picked frequencies. Goertzel recurrence avoids
    // repeated sin/cos evaluation for every sample/bin.
    double transformerBroadbandResidualPower = 0.0;
    constexpr int transformerSpectrumBins =
        transformerMeasurementSamples / 2;
    for (int bin = 1; bin <= transformerSpectrumBins; ++bin)
    {
        if ((bin % 90) == 0)
            continue; // exclude the fundamental and its integer harmonics

        const double omega = 2.0 * pi * bin /
            static_cast<double>(transformerMeasurementSamples);
        const double coefficient = 2.0 * std::cos(omega);
        double state1 = 0.0;
        double state2 = 0.0;
        for (int n = 0; n < transformerMeasurementSamples; ++n)
        {
            const double state0 =
                transformerAudioOutput[static_cast<std::size_t>(n)] +
                coefficient * state1 - state2;
            state2 = state1;
            state1 = state0;
        }
        const double power = state1 * state1 + state2 * state2 -
                             coefficient * state1 * state2;
        transformerBroadbandResidualPower += std::max(0.0, power);
    }
    const double transformerBroadbandResidual =
        2.0 * std::sqrt(transformerBroadbandResidualPower) /
        (static_cast<double>(transformerMeasurementSamples) *
         transformerFundamental);

    double transformerHarmonicPower = 0.0;
    for (int harmonic = 2; harmonic <= 24; ++harmonic)
    {
        const double amplitude =
            spectralAmplitude(90 * harmonic);
        transformerHarmonicPower += amplitude * amplitude;
    }
    const double transformerLowLevelThd =
        std::sqrt(transformerHarmonicPower) / transformerFundamental;

    if (!(transformerFundamental > 1.0e-6) ||
        transformerNonHarmonicSpur > transformerFundamental * 1.0e-3 ||
        transformerBroadbandResidual > 1.0e-3 ||
        transformerLowLevelThd > 0.005)
        return 98;

    std::cout << "Iron 10468-equivalent reference: small-signal gain="
              << transformerGain << "x, non-harmonic spur="
              << transformerNonHarmonicSpur / transformerFundamental
              << ", low-level THD=" << transformerLowLevelThd
              << ", broadband residual=" << transformerBroadbandResidual
              << "\\n";
    if (!(transformerFluxPeak > 1.0e-6 && transformerFluxPeak < 0.1))
        return 92;

    // Frequency-response sanity sweep for the microphone-transformer proxy.
    // A 240 kHz simulation permits a 60 kHz endpoint while keeping integer-
    // cycle windows for every measurement frequency. The upper limit follows
    // the published 10 Hz..60 kHz response envelope of a 10468 replacement.
    constexpr double transformerSweepRate = 240000.0;
    const std::array<int, 8> transformerPeriods = {
        24000, 12000, 4800, 2400, 240, 24, 12, 4
    };
    // Sowter's published 10468 replacement response condition is 1 kohm
    // source / 600 ohm load, so use those source/load values for this sweep.
    auto transformerFrequencyParameters = transformerParameters;
    transformerFrequencyParameters.sourceResistanceOhms = 1000.0;
    transformerFrequencyParameters.loadResistanceOhms = 600.0;
    std::array<double, transformerPeriods.size()> transformerFrequencyGains{};
    double transformerOneKhzGain = 0.0;
    double maximumTransformerResponseDeviationDb = 0.0;
    for (std::size_t frequencyIndex = 0;
         frequencyIndex < transformerPeriods.size(); ++frequencyIndex)
    {
        const int period = transformerPeriods[frequencyIndex];
        const double frequency = transformerSweepRate / period;
        FixedMagneticTransformerStage sweepTransformer;
        if (!sweepTransformer.prepare(
                transformerSweepRate, transformerFrequencyParameters))
            return 100;

        const int settleSamples = period * 8;
        const int measurementSamples = period * 4;
        double inputEnergy = 0.0;
        double outputEnergy = 0.0;
        for (int sample = 0; sample < settleSamples + measurementSamples; ++sample)
        {
            const double input = 0.005 *
                std::sin(2.0 * pi * sample / static_cast<double>(period));
            FixedMagneticTransformerStage::ProcessResult sweepResult;
            if (!sweepTransformer.processSample(
                    static_cast<float>(input), sweepResult))
                return 101;
            if (!std::isfinite(sweepResult.outputVolts))
                return 102;

            if (sample >= settleSamples)
            {
                inputEnergy += input * input;
                outputEnergy += static_cast<double>(sweepResult.outputVolts) *
                                sweepResult.outputVolts;
            }
        }

        const double gain = std::sqrt(outputEnergy / inputEnergy);
        transformerFrequencyGains[frequencyIndex] = gain;
        if (frequencyIndex == 4)
            transformerOneKhzGain = gain;
        if (!std::isfinite(gain) || !(gain > 0.0))
            return 103;
    }

    if (!(transformerOneKhzGain > 0.0) ||
        !std::isfinite(transformerOneKhzGain))
        return 105;
    for (std::size_t frequencyIndex = 0;
         frequencyIndex < transformerFrequencyGains.size(); ++frequencyIndex)
    {
        const double frequency =
            transformerSweepRate / transformerPeriods[frequencyIndex];
        const double deviationDb = 20.0 * std::log10(
            transformerFrequencyGains[frequencyIndex] / transformerOneKhzGain);
        maximumTransformerResponseDeviationDb = std::max(
            maximumTransformerResponseDeviationDb, std::abs(deviationDb));
        if (std::abs(deviationDb) > 0.5)
        {
            std::cerr << "Iron response at " << frequency << " Hz deviated "
                      << deviationDb << " dB from its 1 kHz reference\\n";
            return 104;
        }
    }
    std::cout << "Iron frequency-response sweep: 1 kHz reference gain="
              << transformerOneKhzGain << "x, max deviation="
              << maximumTransformerResponseDeviationDb << " dB\\n";

    // A deliberately high-level, low-frequency drive exercises the nonlinear
    // core region. The reduced-order core must remain finite and bounded.
    transformerStage.reset();
    double highLevelFluxPeak = 0.0;
    for (int sample = 0; sample < 9600; ++sample)
    {
        const double input = 5.0 *
            std::sin(2.0 * pi * 10.0 * sample / 48000.0);
        FixedMagneticTransformerStage::ProcessResult transformerResult;
        if (!transformerStage.processSample(
                static_cast<float>(input), transformerResult))
            return 93;
        if (!std::isfinite(transformerResult.outputVolts) ||
            !std::isfinite(transformerResult.fluxDensityTesla))
            return 94;
        highLevelFluxPeak = std::max(
            highLevelFluxPeak, std::abs(transformerResult.fluxDensityTesla));
    }
    if (!(highLevelFluxPeak > transformerFluxPeak &&
          highLevelFluxPeak < 1.5))
        return 95;

    // The fixed transformer must remain stable at common host sample rates.
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        FixedMagneticTransformerStage multirateTransformer;
        if (!multirateTransformer.prepare(rate, transformerParameters))
            return 96;
        for (int sample = 0; sample < 512; ++sample)
        {
            const float input = static_cast<float>(
                0.02 * std::sin(2.0 * pi * 300.0 * sample / rate));
            FixedMagneticTransformerStage::ProcessResult transformerResult;
            if (!multirateTransformer.processSample(input, transformerResult) ||
                !std::isfinite(transformerResult.outputVolts))
                return 97;
        }
    }

    // Fixed-topology real-time candidate: same device model, fixed resistor
    // network and coupling-capacitor states, solved with stack-only arrays.
    FixedCommonEmitterStage realtimeStage;
    FixedCommonEmitterStage::Parameters realtimeParameters;
    if (!realtimeStage.prepare(48000.0, realtimeParameters, transistor))
        return 72;

    double realtimeInputCorrelation = 0.0;
    double realtimeInputSquares = 0.0;
    double realtimeOutputSquares = 0.0;
    double realtimeOutputSum = 0.0;
    unsigned maximumObservedIterations = 0;
    constexpr int realtimeSamples = 960;
    constexpr int realtimeMeasureStart = 96;
    for (int sample = 0; sample < realtimeSamples; ++sample)
    {
        const double input = 0.005 *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        FixedCommonEmitterStage::ProcessResult realtimeResult;
        if (!realtimeStage.processSample(static_cast<float>(input), realtimeResult))
            return 73;
        maximumObservedIterations =
            std::max(maximumObservedIterations, realtimeResult.iterations);

        if (sample >= realtimeMeasureStart)
        {
            realtimeInputCorrelation += input * realtimeResult.output;
            realtimeInputSquares += input * input;
            realtimeOutputSquares +=
                static_cast<double>(realtimeResult.output) * realtimeResult.output;
            realtimeOutputSum += realtimeResult.output;
        }
    }

    const int realtimeMeasuredSamples = realtimeSamples - realtimeMeasureStart;
    const double realtimeOutputMean = realtimeOutputSum / realtimeMeasuredSamples;
    const double realtimeInputRms =
        std::sqrt(realtimeInputSquares / realtimeMeasuredSamples);
    const double realtimeOutputRms = std::sqrt(std::max(
        0.0, realtimeOutputSquares / realtimeMeasuredSamples -
             realtimeOutputMean * realtimeOutputMean));
    const double realtimeVoltageGain = realtimeOutputRms / realtimeInputRms;
    if (!(realtimeVoltageGain > 2.0) ||
        !near(realtimeVoltageGain, 4.1316, 0.05))
        return 74;
    if (!near(realtimeVoltageGain, mnaReferenceVoltageGain,
              mnaReferenceVoltageGain * 0.05))
        return 79;
    if (!(realtimeInputCorrelation < 0.0))
        return 75;
    std::cout.precision(10);
    std::cout << "Fixed common-emitter: gain=" << realtimeVoltageGain
              << "x, max Newton iterations=" << maximumObservedIterations << "\n";
    if (maximumObservedIterations > realtimeParameters.maximumNewtonIterations)
        return 76;
    if (!realtimeStage.lastResult().converged ||
        !std::isfinite(realtimeStage.lastResult().residualAmps))
        return 77;

    realtimeStage.reset();
    for (int sample = 0; sample < 128; ++sample)
    {
        const float output = realtimeStage.processSample(0.0f);
        if (!std::isfinite(output))
            return 78;
    }

    // Fixed-topology stage must remain convergent at common host rates.
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        FixedCommonEmitterStage multirateStage;
        if (!multirateStage.prepare(rate, realtimeParameters, transistor))
            return 80;
        for (int sample = 0; sample < 512; ++sample)
        {
            const float input = static_cast<float>(
                0.02 * std::sin(2.0 * pi * 1000.0 * sample / rate));
            FixedCommonEmitterStage::ProcessResult rateResult;
            if (!multirateStage.processSample(input, rateResult))
                return 81;
            if (!std::isfinite(rateResult.output) ||
                !std::isfinite(rateResult.collectorVolts) ||
                rateResult.collectorVolts < -0.1 ||
                rateResult.collectorVolts > realtimeParameters.supplyVolts + 0.1)
                return 82;
        }
    }

    // Release-mode throughput estimate for the actual stereo signal path:
    // two transformer + two common-emitter instances for one second of audio.
    // This is a runner baseline, not a substitute for profiling the user's PC.
    std::array<FixedCommonEmitterStage, 2> benchmarkStages;
    std::array<FixedMagneticTransformerStage, 2> benchmarkTransformers;
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        if (!benchmarkStages[channel].prepare(
                48000.0,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                pluginDefaultTransistor) ||
            !benchmarkTransformers[channel].prepare(48000.0, transformerParameters))
            return 120;
    }

    unsigned maximumStageIterations = 0;
    unsigned maximumTransformerIterations = 0;
    constexpr int benchmarkSamples = 48000;
    const auto benchmarkStart = std::chrono::steady_clock::now();
    for (int sample = 0; sample < benchmarkSamples; ++sample)
    {
        const float hostInput = static_cast<float>(
            0.75 * std::sin(2.0 * pi * 997.0 * sample / 48000.0));
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            FixedMagneticTransformerStage::ProcessResult transformerResult;
            if (!benchmarkTransformers[channel].processSample(
                    hostInput * 0.005f, transformerResult))
                return 121;
            maximumTransformerIterations = std::max(
                maximumTransformerIterations, transformerResult.iterations);

            FixedCommonEmitterStage::ProcessResult stageResult;
            if (!benchmarkStages[channel].processSample(
                    transformerResult.outputVolts /
                        static_cast<float>(PGAudio::Circuit::PgIronCalibrationV1::smallSignalReferenceGain),
                    stageResult))
                return 122;
            maximumStageIterations = std::max(
                maximumStageIterations, stageResult.iterations);
        }
    }
    const double benchmarkSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - benchmarkStart).count();
    std::cout << "48 kHz stereo DSP benchmark: "
              << benchmarkSeconds * 100.0 << "% of real time, "
              << "max Newton iterations A Stage=" << maximumStageIterations
              << ", Iron=" << maximumTransformerIterations << "\n";

    std::cout << "PG Circuit Engine: R, L, C, NPN, coupled windings, MNA and common-emitter audio-stage tests passed.\n";
    return 0;
}
