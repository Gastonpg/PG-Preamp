#pragma once

#include "pg_fixed_common_emitter.h"

namespace PGAudio::Circuit {

// PG A Stage M1 is the current original PG common-emitter stage profile.
// Its operating point and passive network are explicit. The transistor
// constants are still a provisional low-noise small-signal model, not a
// calibrated BC184C/BC550C SPICE extraction or a copy of a commercial unit.
struct PgAStageProfileV1
{
    inline static constexpr char profileName[] = "PG A Stage M1";
    static constexpr double smallSignalReferenceGain = 4.14469;
    static constexpr double referenceSampleRateHz = 48000.0;
    static constexpr double referenceFrequencyHz = 1000.0;
    static constexpr double referenceInputPeakVolts = 0.005;
    static constexpr double nominalTemperatureKelvin = 300.15;

    static EbersMollNpn makeTransistor(
        double temperatureKelvin = nominalTemperatureKelvin) noexcept
    {
        EbersMollNpn transistor;
        transistor.prepare(1.0e-14, 120.0, 2.0, temperatureKelvin);

        BjtAdvancedParameters device;
        device.forwardEarlyVoltage = 85.0;
        device.reverseEarlyVoltage = 45.0;
        device.baseResistanceOhms = 120.0;
        device.collectorResistanceOhms = 4.0;
        device.emitterResistanceOhms = 1.5;
        device.baseEmitterCapacitanceFarads = 22.0e-12;
        device.baseCollectorCapacitanceFarads = 7.0e-12;
        device.forwardTransitTimeSeconds = 0.35e-9;
        device.reverseTransitTimeSeconds = 2.0e-9;
        transistor.setAdvancedParameters(device);
        return transistor;
    }

    static FixedCommonEmitterStage::Parameters makeStageParameters() noexcept
    {
        return FixedCommonEmitterStage::Parameters{};
    }
};

// Alternative reference-device fit using the Cordell BC550C SPICE model.
// Cordell states that its small-signal models were adjusted to datasheet
// information and DC measurements of physical samples; this is a published
// sample-derived reference, not a universal guarantee for every BC550C bin.
// It is deliberately kept separate from the currently auditioned PG A Stage M1.
struct PgAStageBc550CReference
{
    inline static constexpr char profileName[] = "BC550C Cordell reference";
    static constexpr double nominalTemperatureKelvin = 300.15;
    static constexpr double nominalCurrentGain = 689.0;

    static EbersMollNpn makeTransistor(
        double temperatureKelvin = nominalTemperatureKelvin) noexcept
    {
        EbersMollNpn transistor;
        // Cordell model: IS=45 fA, BF=689, BR=12.2, EG=1.2 eV,
        // XTI=3 and XTB=1.65, at 300.15 K nominal temperature.
        transistor.prepare(
            45.0e-15, 689.0, 12.2, temperatureKelvin,
            nominalTemperatureKelvin, 1.2, 3.0, 1.65);

        BjtAdvancedParameters device;
        device.forwardEarlyVoltage = 162.0;
        device.reverseEarlyVoltage = 120.0;
        device.baseResistanceOhms = 167.0;
        device.collectorResistanceOhms = 1.0;
        device.emitterResistanceOhms = 0.04;
        device.baseEmitterCapacitanceFarads = 18.7e-12;
        device.baseEmitterPotentialVolts = 0.75;
        device.baseEmitterGradingCoefficient = 0.35;
        device.baseCollectorCapacitanceFarads = 6.2e-12;
        device.baseCollectorPotentialVolts = 0.4;
        device.baseCollectorGradingCoefficient = 0.25;
        device.forwardTransitTimeSeconds = 595.0e-12;
        device.reverseTransitTimeSeconds = 10.0e-9;
        device.forwardEmissionCoefficient = 0.9965;
        device.reverseEmissionCoefficient = 1.0;
        device.baseEmitterLeakageSaturationCurrentAmps = 4.6e-12;
        device.baseEmitterLeakageEmissionCoefficient = 2.0;
        device.baseCollectorLeakageSaturationCurrentAmps = 5.0e-15;
        device.baseCollectorLeakageEmissionCoefficient = 0.996;
        device.forwardBetaRollOffCurrentAmps = 0.09;
        device.reverseBetaRollOffCurrentAmps = 0.34;
        device.betaRollOffExponent = 0.5;
        device.useGummelPoonBaseCharge = true;
        transistor.setAdvancedParameters(device);
        return transistor;
    }

    static FixedCommonEmitterStage::Parameters makeStageParameters() noexcept
    {
        return FixedCommonEmitterStage::Parameters{};
    }
};

} // namespace PGAudio::Circuit
