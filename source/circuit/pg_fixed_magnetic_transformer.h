#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace PGAudio::Circuit {

// Reduced-order, fixed-topology two-winding transformer for the real-time
// audio path. The nonlinear core uses a smooth, stateful hysteresis law so
// Faraday voltage remains finite at audio sample rates. It is intentionally
// separate from the detailed Preisach reference model used for offline tests.
//
// The electrical equations use backward-Euler winding currents, Ampere's
// law for the magnetizing field, and Faraday's law for both windings. The
// magnetic law is a compact macro-model pending calibration against a chosen
// transformer and measured B-H / impedance data; it is not a particular
// commercial transformer clone.
class FixedMagneticTransformerStage
{
public:
    struct Parameters
    {
        double saturationFluxDensityTesla = 0.8;
        double coreAreaSquareMeters = 1.0e-4;
        double magneticPathLengthMeters = 0.08;
        double relativePermeability = 4000.0;
        double coerciveFieldAmpsPerMeter = 0.05;
        double magneticRelaxationSeconds = 0.01;

        double primaryTurns = 1200.0;
        double secondaryTurns = 1200.0;
        double sourceResistanceOhms = 600.0;
        double primaryWindingResistanceOhms = 80.0;
        double secondaryWindingResistanceOhms = 60.0;
        double primaryLeakageInductanceHenries = 1.0e-3;
        double secondaryLeakageInductanceHenries = 1.0e-3;
        double loadResistanceOhms = 15000.0;

        unsigned maximumNewtonIterations = 10;
        double residualToleranceVolts = 1.0e-9;
    };

    struct ProcessResult
    {
        bool converged = false;
        float outputVolts = 0.0f;
        unsigned iterations = 0;
        double residualVolts = std::numeric_limits<double>::infinity();
        double magnetizingFieldAmpsPerMeter = 0.0;
        double fluxDensityTesla = 0.0;
        double primaryCurrentAmps = 0.0;
        double secondaryCurrentAmps = 0.0;
        double primaryInducedVoltageVolts = 0.0;
        double secondaryInducedVoltageVolts = 0.0;
    };

    bool prepare(double sampleRate, const Parameters& parameters = Parameters{}) noexcept
    {
        if (!validPositive(sampleRate) ||
            !validPositive(parameters.saturationFluxDensityTesla) ||
            !validPositive(parameters.coreAreaSquareMeters) ||
            !validPositive(parameters.magneticPathLengthMeters) ||
            !std::isfinite(parameters.relativePermeability) ||
            parameters.relativePermeability <= 1.0 ||
            !validNonNegative(parameters.coerciveFieldAmpsPerMeter) ||
            !validNonNegative(parameters.magneticRelaxationSeconds) ||
            !validPositive(parameters.primaryTurns) ||
            !validPositive(parameters.secondaryTurns) ||
            !validPositive(parameters.sourceResistanceOhms) ||
            !validNonNegative(parameters.primaryWindingResistanceOhms) ||
            !validNonNegative(parameters.secondaryWindingResistanceOhms) ||
            !validNonNegative(parameters.primaryLeakageInductanceHenries) ||
            !validNonNegative(parameters.secondaryLeakageInductanceHenries) ||
            !validPositive(parameters.loadResistanceOhms) ||
            parameters.maximumNewtonIterations == 0 ||
            !validPositive(parameters.residualToleranceVolts))
            return false;

        parameters_ = parameters;
        sampleRate_ = sampleRate;
        timeStep_ = 1.0 / sampleRate;
        inverseTimeStep_ = sampleRate;
        magneticKneeField_ =
            parameters_.saturationFluxDensityTesla /
            (PreisachVacuumPermeability * (parameters_.relativePermeability - 1.0));

        primaryLeakageOverDt_ =
            parameters_.primaryLeakageInductanceHenries * inverseTimeStep_;
        secondaryLeakageOverDt_ =
            parameters_.secondaryLeakageInductanceHenries * inverseTimeStep_;
        secondaryBranchDenominator_ =
            parameters_.loadResistanceOhms +
            parameters_.secondaryWindingResistanceOhms +
            secondaryLeakageOverDt_;
        primaryBranchCoefficient_ =
            parameters_.sourceResistanceOhms +
            parameters_.primaryWindingResistanceOhms +
            primaryLeakageOverDt_;

        configured_ = true;
        reset();
        return true;
    }

    void reset() noexcept
    {
        core_ = {};
        previousPrimaryCurrentAmps_ = 0.0;
        previousSecondaryCurrentAmps_ = 0.0;
        lastResult_ = {};
        lastResult_.converged = configured_;
    }

    bool processSample(float inputVolts, ProcessResult& result) noexcept
    {
        result = lastResult_;
        if (!configured_ || !std::isfinite(inputVolts))
        {
            result.converged = false;
            return false;
        }

        double field = core_.fieldAmpsPerMeter;
        Evaluation evaluation;
        bool converged = false;
        unsigned iterations = 0;

        for (; iterations < parameters_.maximumNewtonIterations; ++iterations)
        {
            evaluation = evaluate(field, static_cast<double>(inputVolts));
            if (!evaluation.finite)
                break;

            if (std::abs(evaluation.residualVolts) <=
                parameters_.residualToleranceVolts)
            {
                converged = true;
                break;
            }

            if (!validPositive(std::abs(evaluation.derivativeVoltsPerAmpPerMeter)))
                break;

            double step = evaluation.residualVolts /
                          evaluation.derivativeVoltsPerAmpPerMeter;
            // Bound Newton steps to keep large host automation/input jumps
            // from moving the magnetic state outside its useful operating range.
            step = std::clamp(step, -50000.0, 50000.0);
            field -= step;
            if (!std::isfinite(field) || std::abs(field) > 1.0e7)
                break;
        }

        if (!converged)
        {
            evaluation = evaluate(field, static_cast<double>(inputVolts));
            converged = evaluation.finite &&
                std::abs(evaluation.residualVolts) <=
                parameters_.residualToleranceVolts * 10.0;
        }

        if (!converged)
        {
            result.converged = false;
            result.iterations = iterations;
            result.residualVolts = evaluation.residualVolts;
            return false;
        }

        core_ = evaluation.nextCore;
        previousPrimaryCurrentAmps_ = evaluation.primaryCurrentAmps;
        previousSecondaryCurrentAmps_ = evaluation.secondaryCurrentAmps;

        lastResult_.converged = true;
        lastResult_.iterations = iterations;
        lastResult_.residualVolts = evaluation.residualVolts;
        lastResult_.magnetizingFieldAmpsPerMeter = field;
        lastResult_.fluxDensityTesla = evaluation.fluxDensityTesla;
        lastResult_.primaryCurrentAmps = evaluation.primaryCurrentAmps;
        lastResult_.secondaryCurrentAmps = evaluation.secondaryCurrentAmps;
        lastResult_.primaryInducedVoltageVolts = evaluation.primaryInducedVoltageVolts;
        lastResult_.secondaryInducedVoltageVolts = evaluation.secondaryInducedVoltageVolts;
        lastResult_.outputVolts = static_cast<float>(evaluation.outputVolts);

        result = lastResult_;
        return true;
    }

    float processSample(float inputVolts) noexcept
    {
        ProcessResult result;
        return processSample(inputVolts, result) ? result.outputVolts : lastResult_.outputVolts;
    }

    const ProcessResult& lastResult() const noexcept { return lastResult_; }
    double sampleRate() const noexcept { return sampleRate_; }
    bool isPrepared() const noexcept { return configured_; }
    double turnsRatio() const noexcept
    {
        return parameters_.secondaryTurns / parameters_.primaryTurns;
    }

private:
    static constexpr double PreisachVacuumPermeability = 1.25663706212e-6;

    struct CoreState
    {
        double magnetization = 0.0;
        double fieldAmpsPerMeter = 0.0;
        int branchDirection = 0;
    };

    struct Evaluation
    {
        CoreState nextCore{};
        double residualVolts = 0.0;
        double derivativeVoltsPerAmpPerMeter = 0.0;
        double fluxDensityTesla = 0.0;
        double primaryCurrentAmps = 0.0;
        double secondaryCurrentAmps = 0.0;
        double primaryInducedVoltageVolts = 0.0;
        double secondaryInducedVoltageVolts = 0.0;
        double outputVolts = 0.0;
        bool finite = false;
    };

    static bool validPositive(double value) noexcept
    {
        return std::isfinite(value) && value > 0.0;
    }

    static bool validNonNegative(double value) noexcept
    {
        return std::isfinite(value) && value >= 0.0;
    }

    Evaluation evaluate(double field, double inputVolts) const noexcept
    {
        Evaluation out;
        if (!std::isfinite(field) || !std::isfinite(inputVolts))
            return out;

        CoreState next = core_;
        const double fieldDelta = field - core_.fieldAmpsPerMeter;
        if (fieldDelta > 1.0e-12)
            next.branchDirection = 1;
        else if (fieldDelta < -1.0e-12)
            next.branchDirection = -1;

        const double branchOffset =
            static_cast<double>(next.branchDirection) *
            parameters_.coerciveFieldAmpsPerMeter;
        const double normalizedField = (field - branchOffset) / magneticKneeField_;
        const double targetMagnetization = std::tanh(normalizedField);
        const double relaxation = parameters_.magneticRelaxationSeconds > 0.0
            ? timeStep_ / (parameters_.magneticRelaxationSeconds + timeStep_)
            : 1.0;
        next.magnetization += relaxation *
            (targetMagnetization - next.magnetization);
        next.fieldAmpsPerMeter = field;

        const double sechSquared =
            std::max(0.0, 1.0 - targetMagnetization * targetMagnetization);
        const double fluxDensity =
            PreisachVacuumPermeability * field +
            parameters_.saturationFluxDensityTesla * next.magnetization;
        const double fluxDensityDerivative =
            PreisachVacuumPermeability +
            parameters_.saturationFluxDensityTesla * relaxation *
            sechSquared / magneticKneeField_;
        const double deltaFluxDensity = fluxDensity - previousFluxDensityTesla_;

        const double primaryCoreVoltage =
            parameters_.primaryTurns * parameters_.coreAreaSquareMeters *
            deltaFluxDensity * inverseTimeStep_;
        const double secondaryCoreVoltage =
            parameters_.secondaryTurns * parameters_.coreAreaSquareMeters *
            deltaFluxDensity * inverseTimeStep_;

        // Secondary winding and load are solved together with backward Euler.
        // Secondary current is positive into the dotted winding terminal, so
        // a delivered load current has the opposite sign.
        const double secondaryCurrent =
            (secondaryLeakageOverDt_ * previousSecondaryCurrentAmps_ -
             secondaryCoreVoltage) / secondaryBranchDenominator_;
        const double secondaryCurrentDerivative =
            -parameters_.secondaryTurns * parameters_.coreAreaSquareMeters *
            fluxDensityDerivative * inverseTimeStep_ /
            secondaryBranchDenominator_;

        // Ampere's law: H*l = Np*Ip + Ns*Is.
        const double turnsRatio = parameters_.secondaryTurns /
                                  parameters_.primaryTurns;
        const double magnetizingCurrent =
            field * parameters_.magneticPathLengthMeters /
            parameters_.primaryTurns;
        const double primaryCurrent =
            magnetizingCurrent - turnsRatio * secondaryCurrent;
        const double primaryCurrentDerivative =
            parameters_.magneticPathLengthMeters /
                parameters_.primaryTurns -
            turnsRatio * secondaryCurrentDerivative;

        const double residual =
            primaryBranchCoefficient_ * primaryCurrent -
            primaryLeakageOverDt_ * previousPrimaryCurrentAmps_ +
            primaryCoreVoltage - inputVolts;
        const double derivative =
            primaryBranchCoefficient_ * primaryCurrentDerivative +
            parameters_.primaryTurns * parameters_.coreAreaSquareMeters *
                fluxDensityDerivative * inverseTimeStep_;

        const double outputVolts =
            -parameters_.loadResistanceOhms * secondaryCurrent;

        out.nextCore = next;
        out.residualVolts = residual;
        out.derivativeVoltsPerAmpPerMeter = derivative;
        out.fluxDensityTesla = fluxDensity;
        out.primaryCurrentAmps = primaryCurrent;
        out.secondaryCurrentAmps = secondaryCurrent;
        out.primaryInducedVoltageVolts = primaryCoreVoltage;
        out.secondaryInducedVoltageVolts = secondaryCoreVoltage;
        out.outputVolts = outputVolts;
        out.finite = std::isfinite(residual) && std::isfinite(derivative) &&
                     std::isfinite(fluxDensity) && std::isfinite(primaryCurrent) &&
                     std::isfinite(secondaryCurrent) && std::isfinite(outputVolts);
        return out;
    }

    Parameters parameters_{};
    CoreState core_{};
    ProcessResult lastResult_{};

    double sampleRate_ = 44100.0;
    double timeStep_ = 1.0 / 44100.0;
    double inverseTimeStep_ = 44100.0;
    double magneticKneeField_ = 1.0;
    double previousFluxDensityTesla_ = 0.0;
    double previousPrimaryCurrentAmps_ = 0.0;
    double previousSecondaryCurrentAmps_ = 0.0;
    double primaryLeakageOverDt_ = 0.0;
    double secondaryLeakageOverDt_ = 0.0;
    double secondaryBranchDenominator_ = 1.0;
    double primaryBranchCoefficient_ = 1.0;
    bool configured_ = false;
};

} // namespace PGAudio::Circuit
