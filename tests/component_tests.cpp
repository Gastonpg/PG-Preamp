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
    using PGAudio::Circuit::FixedMagneticTransformerStage;
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

    // Temperature robustness: retain a forward-active bias and full-cycle
    // conduction over a moderate ambient-temperature span. These are model
    // sweeps, not a claim about package self-heating or a specific device.
    for (const double temperature : {273.15, 300.15, 343.15})
    {
        const auto temperatureTransistor =
            PGAudio::Circuit::PgAStageProfileV1::makeTransistor(temperature);
        FixedCommonEmitterStage temperatureStage;
        if (!temperatureStage.prepare(
                48000.0,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                temperatureTransistor))
            return 129;

        const auto& temperatureBias = temperatureStage.biasOperatingPoint();
        if (!temperatureBias.converged ||
            !(temperatureBias.collectorCurrentAmps > 0.0) ||
            !(temperatureBias.collectorEmitterVolts > 1.0))
            return 130;

        double temperatureMinimumCollectorCurrent =
            std::numeric_limits<double>::infinity();
        double temperatureMinimumVce =
            std::numeric_limits<double>::infinity();
        for (int sample = 0; sample < classAHeadroomSamples; ++sample)
        {
            const double input = maximumNominalInputPeakVolts *
                std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
            FixedCommonEmitterStage::ProcessResult result;
            if (!temperatureStage.processSample(static_cast<float>(input), result) ||
                !(result.collectorCurrentAmps > 0.0) ||
                !(result.collectorEmitterVolts > 0.2))
                return 131;
            temperatureMinimumCollectorCurrent = std::min(
                temperatureMinimumCollectorCurrent, result.collectorCurrentAmps);
            temperatureMinimumVce = std::min(
                temperatureMinimumVce, result.collectorEmitterVolts);
        }
        std::cout << "PG A Stage M1 temperature "
                  << temperature << " K: bias Ic="
                  << temperatureBias.collectorCurrentAmps * 1000.0
                  << " mA, Vce=" << temperatureBias.collectorEmitterVolts
                  << " V, min-cycle Ic="
                  << temperatureMinimumCollectorCurrent * 1000.0
                  << " mA\\n";
    }

    // Bias network tolerance corners: all 16 combinations of +/-5% on the
    // divider, collector resistor and emitter resistor must remain forward-
    // active through the nominal +18 dB input envelope.
    double worstToleranceMinimumCollectorCurrent =
        std::numeric_limits<double>::infinity();
    double worstToleranceMinimumVce =
        std::numeric_limits<double>::infinity();
    double minimumToleranceBiasCollectorCurrent =
        std::numeric_limits<double>::infinity();
    double maximumToleranceBiasCollectorCurrent = 0.0;
    for (int corner = 0; corner < 16; ++corner)
    {
        auto toleranceParameters =
            PGAudio::Circuit::PgAStageProfileV1::makeStageParameters();
        const auto toleranceScale = [corner](int bit)
        {
            return (corner & (1 << bit)) != 0 ? 1.05 : 0.95;
        };
        toleranceParameters.baseFeedResistanceOhms *= toleranceScale(0);
        toleranceParameters.baseReturnResistanceOhms *= toleranceScale(1);
        toleranceParameters.collectorResistanceOhms *= toleranceScale(2);
        toleranceParameters.emitterResistanceOhms *= toleranceScale(3);

        FixedCommonEmitterStage toleranceStage;
        if (!toleranceStage.prepare(48000.0, toleranceParameters,
                                    pluginDefaultTransistor))
            return 132;
        if (!toleranceStage.biasOperatingPoint().converged ||
            !(toleranceStage.biasOperatingPoint().collectorCurrentAmps > 0.0) ||
            !(toleranceStage.biasOperatingPoint().collectorEmitterVolts > 1.0))
            return 133;
        minimumToleranceBiasCollectorCurrent = std::min(
            minimumToleranceBiasCollectorCurrent,
            toleranceStage.biasOperatingPoint().collectorCurrentAmps);
        maximumToleranceBiasCollectorCurrent = std::max(
            maximumToleranceBiasCollectorCurrent,
            toleranceStage.biasOperatingPoint().collectorCurrentAmps);

        double minimumCornerCollectorCurrent =
            std::numeric_limits<double>::infinity();
        double minimumCornerVce = std::numeric_limits<double>::infinity();
        for (int sample = 0; sample < classAHeadroomSamples; ++sample)
        {
            const double input = maximumNominalInputPeakVolts *
                std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
            FixedCommonEmitterStage::ProcessResult result;
            if (!toleranceStage.processSample(static_cast<float>(input), result) ||
                !(result.collectorCurrentAmps > 0.0) ||
                !(result.collectorEmitterVolts > 0.2))
                return 134;
            minimumCornerCollectorCurrent = std::min(
                minimumCornerCollectorCurrent, result.collectorCurrentAmps);
            minimumCornerVce = std::min(
                minimumCornerVce, result.collectorEmitterVolts);
        }
        if (!std::isfinite(minimumCornerCollectorCurrent) ||
            !std::isfinite(minimumCornerVce))
            return 135;
        worstToleranceMinimumCollectorCurrent = std::min(
            worstToleranceMinimumCollectorCurrent, minimumCornerCollectorCurrent);
        worstToleranceMinimumVce = std::min(
            worstToleranceMinimumVce, minimumCornerVce);
    }
    std::cout << "PG A Stage M1 +/-5% tolerance sweep (16 corners): bias Ic="
              << minimumToleranceBiasCollectorCurrent * 1000.0 << ".."
              << maximumToleranceBiasCollectorCurrent * 1000.0
              << " mA, worst-cycle min Ic="
              << worstToleranceMinimumCollectorCurrent * 1000.0
              << " mA, worst-cycle min Vce="
              << worstToleranceMinimumVce << " V\\n";

    // Alternate device reference: Cordell's sample-derived BC550C model.
    // This is characterized independently; it does not replace PG A Stage M1.
    auto cordellBc550c =
        PGAudio::Circuit::PgAStageBc550CReference::makeTransistor();
    if (!near(cordellBc550c.forwardBeta(), 689.0, 1.0e-9) ||
        !near(cordellBc550c.reverseBeta(), 12.2, 1.0e-9) ||
        !near(cordellBc550c.saturationCurrentAmps(), 45.0e-15, 1.0e-24))
        return 136;

    // Datasheet operating point: BC550C gain grade C is specified
    // from 420 to 800 at 2 mA / 5 V. Fit VBE numerically and compare the
    // Cordell model's predicted DC gain at that same test point.
    double lowerBaseVoltage = 0.50;
    double upperBaseVoltage = 0.90;
    for (int iteration = 0; iteration < 80; ++iteration)
    {
        const double midpoint = 0.5 * (lowerBaseVoltage + upperBaseVoltage);
        const auto midpointCurrent = cordellBc550c.evaluate(
            midpoint, 5.0, 0.0).collectorAmps;
        if (midpointCurrent < 2.0e-3)
            lowerBaseVoltage = midpoint;
        else
            upperBaseVoltage = midpoint;
    }
    const auto bc550cDatasheetPoint = cordellBc550c.evaluate(
        0.5 * (lowerBaseVoltage + upperBaseVoltage), 5.0, 0.0);
    const double bc550cDatasheetGain =
        bc550cDatasheetPoint.collectorAmps / bc550cDatasheetPoint.baseAmps;
    if (!near(bc550cDatasheetPoint.collectorAmps, 2.0e-3, 2.0e-7) ||
        !(bc550cDatasheetGain >= 420.0 && bc550cDatasheetGain <= 800.0))
        return 140;
    std::cout << "Cordell BC550C 2 mA / 5 V datasheet point: Vbe="
              << 0.5 * (lowerBaseVoltage + upperBaseVoltage)
              << " V, predicted hFE=" << bc550cDatasheetGain << "\\n";

    // Validate the analytic conductance Jacobian in the new Gummel-Poon
    // base-charge path against central finite differences at its datasheet
    // operating point, including Early effect and leakage branches.
    const double bc550cBaseVoltage =
        0.5 * (lowerBaseVoltage + upperBaseVoltage);
    const auto bc550cAnalytic = cordellBc550c.evaluateWithDerivatives(
        bc550cBaseVoltage, 5.0, 0.0);
    constexpr double bc550cDerivativeStep = 1.0e-7;
    for (int column = 0; column < 3; ++column)
    {
        double plusVoltage[3] = {bc550cBaseVoltage, 5.0, 0.0};
        plusVoltage[column] += bc550cDerivativeStep;
        const auto plus = cordellBc550c.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);
        plusVoltage[column] -= 2.0 * bc550cDerivativeStep;
        const auto minus = cordellBc550c.evaluateWithDerivatives(
            plusVoltage[0], plusVoltage[1], plusVoltage[2]);
        const double plusCurrent[3] = {
            plus.currents.baseAmps, plus.currents.collectorAmps,
            plus.currents.emitterAmps
        };
        const double minusCurrent[3] = {
            minus.currents.baseAmps, minus.currents.collectorAmps,
            minus.currents.emitterAmps
        };
        for (int row = 0; row < 3; ++row)
        {
            const double numerical = (plusCurrent[row] - minusCurrent[row]) /
                                     (2.0 * bc550cDerivativeStep);
            const double analytic = bc550cAnalytic.conductance[row][column];
            if (!near(analytic, numerical,
                      std::max(1.0e-8, std::abs(numerical) * 3.0e-4)))
                return 148;
        }
    }
    if (!near(bc550cAnalytic.currents.baseAmps +
              bc550cAnalytic.currents.collectorAmps +
              bc550cAnalytic.currents.emitterAmps, 0.0, 1.0e-12))
        return 149;



    FixedCommonEmitterStage bc550cReferenceStage;
    if (!bc550cReferenceStage.prepare(
            48000.0,
            PGAudio::Circuit::PgAStageBc550CReference::makeStageParameters(),
            cordellBc550c))
        return 137;
    const auto& bc550cBias = bc550cReferenceStage.biasOperatingPoint();
    if (!bc550cBias.converged ||
        !(bc550cBias.collectorCurrentAmps > 0.0) ||
        !(bc550cBias.collectorEmitterVolts > 1.0) ||
        std::abs(bc550cBias.collectorCurrentAmps +
                 bc550cBias.baseCurrentAmps +
                 bc550cBias.emitterCurrentAmps) > 1.0e-9)
        return 138;

    // First-order transistor noise diagnostics are calculated at the solved
    // bias point but are not injected into the audio signal path.
    const auto bc550cNoise = cordellBc550c.evaluateNoiseSources(
        300.15, {
            bc550cBias.collectorCurrentAmps,
            bc550cBias.baseCurrentAmps,
            bc550cBias.emitterCurrentAmps
        });
    constexpr double noiseBoltzmann = 1.380649e-23;
    constexpr double noiseElectronCharge = 1.602176634e-19;
    if (!near(bc550cNoise.baseResistanceVoltageNoiseVPerSqrtHz,
              std::sqrt(4.0 * noiseBoltzmann * 300.15 * 167.0), 1.0e-15) ||
        !near(bc550cNoise.collectorResistanceVoltageNoiseVPerSqrtHz,
              std::sqrt(4.0 * noiseBoltzmann * 300.15 * 1.0), 1.0e-15) ||
        !near(bc550cNoise.emitterResistanceVoltageNoiseVPerSqrtHz,
              std::sqrt(4.0 * noiseBoltzmann * 300.15 * 0.04), 1.0e-15) ||
        !near(bc550cNoise.baseCurrentShotNoiseAPerSqrtHz,
              std::sqrt(2.0 * noiseElectronCharge *
                        std::abs(bc550cBias.baseCurrentAmps)), 1.0e-15) ||
        !near(bc550cNoise.collectorCurrentShotNoiseAPerSqrtHz,
              std::sqrt(2.0 * noiseElectronCharge *
                        std::abs(bc550cBias.collectorCurrentAmps)), 1.0e-15))
        return 150;

    double bc550cMinimumCollectorCurrent =
        std::numeric_limits<double>::infinity();
    double bc550cMinimumVce =
        std::numeric_limits<double>::infinity();
    double bc550cInputSquares = 0.0;
    double bc550cOutputSquares = 0.0;
    constexpr int bc550cSurveySamples = 4800;
    constexpr int bc550cDiscardSamples = 480;
    for (int sample = 0; sample < bc550cSurveySamples; ++sample)
    {
        const double input = 0.04 *
            std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
        FixedCommonEmitterStage::ProcessResult result;
        if (!bc550cReferenceStage.processSample(static_cast<float>(input), result) ||
            !(result.collectorCurrentAmps > 0.0) ||
            !(result.collectorEmitterVolts > 0.2))
            return 139;
        bc550cMinimumCollectorCurrent = std::min(
            bc550cMinimumCollectorCurrent, result.collectorCurrentAmps);
        bc550cMinimumVce = std::min(
            bc550cMinimumVce, result.collectorEmitterVolts);
        if (sample >= bc550cDiscardSamples)
        {
            bc550cInputSquares += input * input;
            bc550cOutputSquares +=
                static_cast<double>(result.output) * result.output;
        }
    }
    const double bc550cMeasuredGain = std::sqrt(
        bc550cOutputSquares / bc550cInputSquares);
    std::cout << "Cordell BC550C reference A Stage: bias Ic="
              << bc550cBias.collectorCurrentAmps * 1000.0
              << " mA, Vce=" << bc550cBias.collectorEmitterVolts
              << " V, min-cycle Ic="
              << bc550cMinimumCollectorCurrent * 1000.0
              << " mA, min-cycle Vce=" << bc550cMinimumVce
              << " V, 1 kHz gain at 40 mV="
              << bc550cMeasuredGain << "x\\n";

    // The alternate BC550C reference must also tolerate temperature and
    // bias-network spread without losing its forward-active Class-A envelope.
    for (const double temperature : {273.15, 300.15, 343.15})
    {
        const auto temperatureDevice =
            PGAudio::Circuit::PgAStageBc550CReference::makeTransistor(temperature);
        FixedCommonEmitterStage temperatureStage;
        if (!temperatureStage.prepare(
                48000.0,
                PGAudio::Circuit::PgAStageBc550CReference::makeStageParameters(),
                temperatureDevice))
            return 142;
        const auto& bias = temperatureStage.biasOperatingPoint();
        if (!bias.converged || !(bias.collectorCurrentAmps > 0.0) ||
            !(bias.collectorEmitterVolts > 1.0))
            return 143;

        double minimumCurrent = std::numeric_limits<double>::infinity();
        for (int sample = 0; sample < classAHeadroomSamples; ++sample)
        {
            const double input = maximumNominalInputPeakVolts *
                std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
            FixedCommonEmitterStage::ProcessResult result;
            if (!temperatureStage.processSample(static_cast<float>(input), result) ||
                !(result.collectorCurrentAmps > 0.0) ||
                !(result.collectorEmitterVolts > 0.2))
                return 144;
            minimumCurrent = std::min(minimumCurrent, result.collectorCurrentAmps);
        }
        std::cout << "BC550C reference A Stage temperature "
                  << temperature << " K: bias Ic="
                  << bias.collectorCurrentAmps * 1000.0
                  << " mA, Vce=" << bias.collectorEmitterVolts
                  << " V, min-cycle Ic=" << minimumCurrent * 1000.0
                  << " mA\\n";
    }

    double bc550cToleranceMinimumCurrent =
        std::numeric_limits<double>::infinity();
    double bc550cToleranceMinimumVce =
        std::numeric_limits<double>::infinity();
    for (int corner = 0; corner < 16; ++corner)
    {
        auto parameters =
            PGAudio::Circuit::PgAStageBc550CReference::makeStageParameters();
        const auto scale = [corner](int bit)
        {
            return (corner & (1 << bit)) != 0 ? 1.05 : 0.95;
        };
        parameters.baseFeedResistanceOhms *= scale(0);
        parameters.baseReturnResistanceOhms *= scale(1);
        parameters.collectorResistanceOhms *= scale(2);
        parameters.emitterResistanceOhms *= scale(3);

        FixedCommonEmitterStage cornerStage;
        if (!cornerStage.prepare(48000.0, parameters, cordellBc550c))
            return 145;
        if (!cornerStage.biasOperatingPoint().converged ||
            !(cornerStage.biasOperatingPoint().collectorCurrentAmps > 0.0) ||
            !(cornerStage.biasOperatingPoint().collectorEmitterVolts > 1.0))
            return 146;

        for (int sample = 0; sample < classAHeadroomSamples; ++sample)
        {
            const double input = maximumNominalInputPeakVolts *
                std::sin(2.0 * pi * 1000.0 * sample / 48000.0);
            FixedCommonEmitterStage::ProcessResult result;
            if (!cornerStage.processSample(static_cast<float>(input), result) ||
                !(result.collectorCurrentAmps > 0.0) ||
                !(result.collectorEmitterVolts > 0.2))
                return 147;
            bc550cToleranceMinimumCurrent = std::min(
                bc550cToleranceMinimumCurrent, result.collectorCurrentAmps);
            bc550cToleranceMinimumVce = std::min(
                bc550cToleranceMinimumVce, result.collectorEmitterVolts);
        }
    }
    std::cout << "BC550C reference A Stage +/-5% bias corners: worst-cycle min Ic="
              << bc550cToleranceMinimumCurrent * 1000.0
              << " mA, min Vce=" << bc550cToleranceMinimumVce
              << " V\\n";

    // Measure distortion at two useful drive levels with an integer-cycle
    // DFT window. This is a deterministic circuit-model regression, not a
    // physical hardware measurement.
    const auto measureStageThd = [&](double peakVolts,
                                      const EbersMollNpn& measurementTransistor)
    {
        FixedCommonEmitterStage measurementStage;
        if (!measurementStage.prepare(
                48000.0,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                measurementTransistor))
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

    const double lowDriveThd = measureStageThd(0.005, pluginDefaultTransistor);
    const double nominalMaximumDriveThd = measureStageThd(0.04, pluginDefaultTransistor);
    const double bc550cLowDriveThd = measureStageThd(0.005, cordellBc550c);
    const double bc550cMaximumDriveThd = measureStageThd(0.04, cordellBc550c);
    if (!std::isfinite(lowDriveThd) ||
        !std::isfinite(nominalMaximumDriveThd) ||
        !std::isfinite(bc550cLowDriveThd) ||
        !std::isfinite(bc550cMaximumDriveThd) ||
        !(nominalMaximumDriveThd > lowDriveThd) ||
        !(bc550cMaximumDriveThd > bc550cLowDriveThd))
        return 115;
    std::cout << "PG A Stage M1 THD at 1 kHz: 5 mV="
              << lowDriveThd * 100.0 << "%, 40 mV="
              << nominalMaximumDriveThd * 100.0 << "%\\n";
    std::cout << "BC550C reference A Stage THD at 1 kHz: 5 mV="
              << bc550cLowDriveThd * 100.0 << "%, 40 mV="
              << bc550cMaximumDriveThd * 100.0 << "%\\n";

    // Compare both A Stage transistor profiles with the same audio-band
    // small-signal sweep. The M1 profile remains the shipping/auditioned path.
    constexpr double stageSweepRate = 240000.0;
    const std::array<int, 8> stagePeriods = {
        12000, 4800, 2400, 240, 48, 24, 16, 12
    }; // 20 Hz, 50 Hz, 100 Hz, 1 kHz, 5 kHz, 10 kHz, 15 kHz, 20 kHz
    const auto measureStageResponse = [&](const EbersMollNpn& measurementTransistor,
                                           double& oneKhzGain,
                                           double& maximumDeviationDb)
    {
        std::array<double, stagePeriods.size()> frequencyGains{};
        oneKhzGain = 0.0;
        maximumDeviationDb = 0.0;
        for (std::size_t frequencyIndex = 0;
             frequencyIndex < stagePeriods.size(); ++frequencyIndex)
        {
            const int period = stagePeriods[frequencyIndex];
            FixedCommonEmitterStage sweepStage;
            if (!sweepStage.prepare(
                    stageSweepRate,
                    PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                    measurementTransistor))
                return false;

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
                    return false;
                if (sample >= settleSamples)
                {
                    inputEnergy += input * input;
                    outputEnergy += static_cast<double>(result.output) * result.output;
                }
            }
            const double gain = std::sqrt(outputEnergy / inputEnergy);
            if (!std::isfinite(gain) || !(gain > 0.0))
                return false;
            frequencyGains[frequencyIndex] = gain;
            if (frequencyIndex == 3)
                oneKhzGain = gain;
        }
        if (!(oneKhzGain > 0.0))
            return false;
        for (const double gain : frequencyGains)
        {
            const double deviationDb = 20.0 * std::log10(gain / oneKhzGain);
            maximumDeviationDb = std::max(maximumDeviationDb, std::abs(deviationDb));
        }
        return std::isfinite(maximumDeviationDb);
    };

    double stageOneKhzGain = 0.0;
    double maximumStageResponseDeviationDb = 0.0;
    double bc550cOneKhzGain = 0.0;
    double maximumBc550cResponseDeviationDb = 0.0;
    if (!measureStageResponse(pluginDefaultTransistor,
                              stageOneKhzGain,
                              maximumStageResponseDeviationDb))
        return 116;
    if (!measureStageResponse(cordellBc550c,
                              bc550cOneKhzGain,
                              maximumBc550cResponseDeviationDb))
        return 141;
    std::cout << "PG A Stage M1 20 Hz..20 kHz sweep: 1 kHz gain="
              << stageOneKhzGain << "x, max deviation="
              << maximumStageResponseDeviationDb << " dB\\n";
    std::cout << "BC550C reference A Stage 20 Hz..20 kHz sweep: 1 kHz gain="
              << bc550cOneKhzGain << "x, max deviation="
              << maximumBc550cResponseDeviationDb << " dB\\n";

    // Aliasing survey at a high vocal-band frequency and the maximum nominal
    // A Stage drive. At 48 kHz, harmonics above Nyquist fold back into band;
    // the 192 kHz run is a higher-rate reference for the same model/input.
    const auto measureHighFrequencyHarmonics = [&](double sampleRate,
                                                  const EbersMollNpn& measurementTransistor)
    {
        constexpr int measurementCount = 4800;
        constexpr int discardCount = 4800;
        FixedCommonEmitterStage spectrumStage;
        if (!spectrumStage.prepare(
                sampleRate,
                PGAudio::Circuit::PgAStageProfileV1::makeStageParameters(),
                measurementTransistor))
        {
            std::array<double, 5> invalid{};
            invalid.fill(std::numeric_limits<double>::quiet_NaN());
            return invalid;
        }

        std::array<double, measurementCount> spectrum{};
        for (int sample = 0; sample < discardCount + measurementCount; ++sample)
        {
            const double input = 0.04 *
                std::sin(2.0 * pi * 17000.0 * sample / sampleRate);
            FixedCommonEmitterStage::ProcessResult result;
            if (!spectrumStage.processSample(static_cast<float>(input), result))
            {
                std::array<double, 5> invalid{};
                invalid.fill(std::numeric_limits<double>::quiet_NaN());
                return invalid;
            }
            if (sample >= discardCount)
                spectrum[static_cast<std::size_t>(sample - discardCount)] = result.output;
        }

        const int fundamentalBin = static_cast<int>(std::llround(
            17000.0 * measurementCount / sampleRate));
        std::array<double, 5> amplitudes{};
        for (int harmonic = 1; harmonic <= 5; ++harmonic)
        {
            const int rawBin = (harmonic * fundamentalBin) % measurementCount;
            const int foldedBin = std::min(rawBin, measurementCount - rawBin);
            amplitudes[static_cast<std::size_t>(harmonic - 1)] =
                dftAmplitude(spectrum, foldedBin);
        }
        return amplitudes;
    };

    const auto reportHighFrequencyHarmonics =
        [&](const char* profileName, const EbersMollNpn& measurementTransistor)
    {
        const auto harmonicsAt48k =
            measureHighFrequencyHarmonics(48000.0, measurementTransistor);
        const auto harmonicsAt192k =
            measureHighFrequencyHarmonics(192000.0, measurementTransistor);
        double foldedHarmonicPower48k = 0.0;
        double harmonicPower192k = 0.0;
        for (std::size_t harmonic = 1; harmonic < harmonicsAt48k.size(); ++harmonic)
        {
            if (!std::isfinite(harmonicsAt48k[harmonic]) ||
                !std::isfinite(harmonicsAt192k[harmonic]))
                return false;
            foldedHarmonicPower48k +=
                harmonicsAt48k[harmonic] * harmonicsAt48k[harmonic];
            harmonicPower192k +=
                harmonicsAt192k[harmonic] * harmonicsAt192k[harmonic];
        }
        const double aliasRatio48k = std::sqrt(foldedHarmonicPower48k) /
                                     harmonicsAt48k[0];
        const double nativeHarmonicRatio192k = std::sqrt(harmonicPower192k) /
                                              harmonicsAt192k[0];
        if (!std::isfinite(aliasRatio48k) ||
            !std::isfinite(nativeHarmonicRatio192k))
            return false;
        std::cout << profileName << " 17 kHz / 40 mV harmonics: "
                  << "48 kHz folded H2-H5=" << aliasRatio48k * 100.0
                  << "%, 192 kHz reference H2-H5="
                  << nativeHarmonicRatio192k * 100.0 << "%\\n";
        return true;
    };
    if (!reportHighFrequencyHarmonics("PG A Stage M1", pluginDefaultTransistor))
        return 127;
    if (!reportHighFrequencyHarmonics("BC550C reference A Stage", cordellBc550c))
        return 128;

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
