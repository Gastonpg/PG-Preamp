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

} // namespace PGAudio::Circuit
