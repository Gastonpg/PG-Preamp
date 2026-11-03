#pragma once

#include "pg_fixed_magnetic_transformer.h"

namespace PGAudio::Circuit {

// PG Iron M1: an original PG input-transformer voicing informed by the
// published Neve 10468 role and the VTB9045 / Sowter 9145 replacement data.
// This is a declared design profile, not a reconstruction of undisclosed
// Marinair core or winding measurements.
struct PgIronCalibrationV1
{
    inline static constexpr char profileName[] = "PG Iron M1";
    static constexpr double smallSignalReferenceGain = 1.2545;
    static constexpr double referenceSampleRateHz = 48000.0;
    static constexpr double referenceFrequencyHz = 1000.0;
    static constexpr double referenceInputPeakVolts = 0.005;

    static FixedMagneticTransformerStage::Parameters makeParameters() noexcept
    {
        FixedMagneticTransformerStage::Parameters p;

        // PG magnetic voicing parameters. These are explicit effective
        // macro-model choices and remain tunable against listening tests
        // and future measurements of a physical reference unit.
        p.saturationFluxDensityTesla = 0.8;
        p.coreAreaSquareMeters = 1.0e-4;
        p.magneticPathLengthMeters = 0.08;
        p.relativePermeability = 16000.0;
        p.coerciveFieldAmpsPerMeter = 1.0e-6;
        p.magneticRelaxationSeconds = 1.0e-6;
        p.branchMemoryRelaxationSeconds = 1.0e-3;
        p.branchTransitionFieldAmpsPerMeter = 1.0e-5;

        // Electrical reference: 1:2 ratio, 12/65-ohm parallel-winding DCR,
        // 150-ohm nominal mic source and 1.2-kohm secondary load.
        // Effective turns and leakage are PG model values, not Marinair data.
        p.primaryTurns = 1000.0;
        p.secondaryTurns = 2000.0;
        p.sourceResistanceOhms = 150.0;
        p.primaryWindingResistanceOhms = 12.0;
        p.secondaryWindingResistanceOhms = 65.0;
        p.primaryLeakageInductanceHenries = 1.0e-5;
        p.secondaryLeakageInductanceHenries = 1.0e-5;
        p.loadResistanceOhms = 1200.0;

        return p;
    }
};

} // namespace PGAudio::Circuit
