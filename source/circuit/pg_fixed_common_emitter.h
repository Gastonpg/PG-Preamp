#pragma once

#include "pg_bjt_ebers_moll.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace PGAudio::Circuit {

// Real-time candidate for one fixed common-emitter stage. The circuit is
// solved with a fixed-size Newton/MNA system using stack arrays only.
// There are no heap allocations, containers or locks in processSample().
//
// This is a deliberately fixed engineering topology, not the final PG Preamp
// schematic and not a claim of exact emulation of a commercial preamp.
class FixedCommonEmitterStage
{
public:
    struct Parameters
    {
        double supplyVolts = 12.0;
        double baseFeedResistanceOhms = 100000.0;
        double baseReturnResistanceOhms = 18000.0;
        double collectorResistanceOhms = 4700.0;
        double emitterResistanceOhms = 1000.0;
        double sourceResistanceOhms = 600.0;
        double outputLoadResistanceOhms = 100000.0;
        double inputCapacitanceFarads = 10.0e-6;
        double outputCapacitanceFarads = 10.0e-6;
        double inputCapacitorEsrOhms = 0.5;
        double outputCapacitorEsrOhms = 0.5;
        unsigned maximumNewtonIterations = 16;
        double residualToleranceAmps = 1.0e-9;
    };

    struct ProcessResult
    {
        bool converged = false;
        double output = 0.0;
        unsigned iterations = 0;
        double residualAmps = std::numeric_limits<double>::infinity();
        double baseVolts = 0.0;
        double collectorVolts = 0.0;
        double emitterVolts = 0.0;
        double collectorEmitterVolts = 0.0;
        double collectorCurrentAmps = 0.0;
        double baseCurrentAmps = 0.0;
        double emitterCurrentAmps = 0.0;
    };

    bool prepare(double sampleRate, const Parameters& parameters,
                 const EbersMollNpn& transistor) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 ||
            !validPositive(parameters.supplyVolts) ||
            !validPositive(parameters.baseFeedResistanceOhms) ||
            !validPositive(parameters.baseReturnResistanceOhms) ||
            !validPositive(parameters.collectorResistanceOhms) ||
            !validPositive(parameters.emitterResistanceOhms) ||
            !validNonNegative(parameters.sourceResistanceOhms) ||
            !validPositive(parameters.outputLoadResistanceOhms) ||
            !validPositive(parameters.inputCapacitanceFarads) ||
            !validPositive(parameters.outputCapacitanceFarads) ||
            !validNonNegative(parameters.inputCapacitorEsrOhms) ||
            !validNonNegative(parameters.outputCapacitorEsrOhms) ||
            parameters.maximumNewtonIterations == 0 ||
            !validPositive(parameters.residualToleranceAmps))
            return false;

        const auto& deviceParameters = transistor.advancedParameters();
        if (!validPositive(deviceParameters.baseResistanceOhms) ||
            !validPositive(deviceParameters.collectorResistanceOhms) ||
            !validPositive(deviceParameters.emitterResistanceOhms))
            return false;

        sampleRate_ = sampleRate;
        timeStep_ = 1.0 / sampleRate;
        parameters_ = parameters;
        transistor_ = transistor;

        inputCapacitorDenominator_ =
            parameters_.sourceResistanceOhms + parameters_.inputCapacitorEsrOhms +
            timeStep_ / parameters_.inputCapacitanceFarads;
        outputCapacitorDenominator_ =
            parameters_.outputCapacitorEsrOhms +
            timeStep_ / parameters_.outputCapacitanceFarads;

        baseResistance_ = deviceParameters.baseResistanceOhms;
        collectorDeviceResistance_ = deviceParameters.collectorResistanceOhms;
        emitterDeviceResistance_ = deviceParameters.emitterResistanceOhms;

        configured_ = true;
        biasReady_ = false;
        reset();
        return biasReady_;
    }

    void reset() noexcept
    {
        if (!configured_)
            return;

        // Re-solve the DC operating point whenever the circuit is reset.
        // Coupling-capacitor states are then initialized to that exact bias,
        // so the first audio sample cannot create a false output transient.
        biasReady_ = initializeBiasPoint();
    }

    bool processSample(double inputVolts, ProcessResult& result) noexcept
    {
        result = lastResult_;
        if (!configured_ || !biasReady_ || !std::isfinite(inputVolts))
        {
            result.converged = false;
            return false;
        }

        auto candidate = solution_;
        bool converged = false;
        double residualNorm = std::numeric_limits<double>::infinity();
        unsigned iterations = 0;

        for (; iterations < parameters_.maximumNewtonIterations; ++iterations)
        {
            Matrix jacobian{};
            Vector residual{};
            assemble(candidate, static_cast<double>(inputVolts), jacobian, residual);

            residualNorm = maxAbs(residual);
            if (residualNorm <= parameters_.residualToleranceAmps)
            {
                converged = true;
                break;
            }

            Vector delta{};
            for (std::size_t i = 0; i < UnknownCount; ++i)
                delta[i] = -residual[i];

            if (!solveLinearSystem(jacobian, delta))
                break;

            double updateNorm = 0.0;
            for (std::size_t i = 0; i < UnknownCount; ++i)
            {
                if (!std::isfinite(delta[i]))
                    break;
                updateNorm = std::max(updateNorm, std::abs(delta[i]));
                candidate[i] += delta[i];
            }

            if (!allFinite(candidate))
                break;

            if (updateNorm < parameters_.residualToleranceAmps)
            {
                Matrix checkJacobian{};
                Vector checkResidual{};
                assemble(candidate, static_cast<double>(inputVolts),
                         checkJacobian, checkResidual);
                residualNorm = maxAbs(checkResidual);
                converged = residualNorm <= parameters_.residualToleranceAmps * 10.0;
                if (converged)
                {
                    ++iterations;
                    break;
                }
            }
        }

        if (!converged)
        {
            result.converged = false;
            result.iterations = iterations;
            result.residualAmps = residualNorm;
            return false;
        }

        const auto finalDevice = transistor_.evaluateWithDerivatives(
            candidate[1], candidate[3], candidate[5]);

        const double inputCapCurrent =
            (static_cast<double>(inputVolts) - candidate[0] -
             inputCapacitorVoltage_) / inputCapacitorDenominator_;
        const double outputCapCurrent =
            (candidate[2] - candidate[6] - outputCapacitorVoltage_) /
            outputCapacitorDenominator_;

        inputCapacitorVoltage_ += timeStep_ / parameters_.inputCapacitanceFarads *
                                  inputCapCurrent;
        outputCapacitorVoltage_ += timeStep_ / parameters_.outputCapacitanceFarads *
                                   outputCapCurrent;

        for (int terminal = 0; terminal < 3; ++terminal)
            previousTransistorCharge_[terminal] =
                finalDevice.terminalChargeCoulombs[terminal];

        solution_ = candidate;
        lastResult_.converged = true;
        lastResult_.output = candidate[6];
        lastResult_.iterations = iterations;
        lastResult_.residualAmps = residualNorm;
        lastResult_.baseVolts = candidate[0];
        lastResult_.collectorVolts = candidate[2];
        lastResult_.emitterVolts = candidate[4];
        lastResult_.collectorEmitterVolts = candidate[3] - candidate[5];
        lastResult_.collectorCurrentAmps = finalDevice.currents.collectorAmps;
        lastResult_.baseCurrentAmps = finalDevice.currents.baseAmps;
        lastResult_.emitterCurrentAmps = finalDevice.currents.emitterAmps;

        result = lastResult_;
        return true;
    }

    double processSample(double inputVolts) noexcept
    {
        ProcessResult result;
        return processSample(inputVolts, result) ? result.output : lastResult_.output;
    }

    const ProcessResult& lastResult() const noexcept { return lastResult_; }
    const ProcessResult& biasOperatingPoint() const noexcept { return biasOperatingPoint_; }
    double sampleRate() const noexcept { return sampleRate_; }
    bool isPrepared() const noexcept { return configured_; }
    bool isBiasReady() const noexcept { return biasReady_; }

    // A forward-active NPN remains biased with positive collector current
    // and collector voltage above its emitter. This is a point-in-time
    // electrical check; full-cycle Class-A headroom is covered by tests.
    bool isForwardActive() const noexcept
    {
        return lastResult_.converged &&
               lastResult_.collectorCurrentAmps > 0.0 &&
               lastResult_.collectorEmitterVolts > 0.2;
    }

private:
    static constexpr std::size_t UnknownCount = 7;
    using Vector = std::array<double, UnknownCount>;
    using Matrix = std::array<std::array<double, UnknownCount>, UnknownCount>;

    static bool validPositive(double value) noexcept
    {
        return std::isfinite(value) && value > 0.0;
    }
    static bool validNonNegative(double value) noexcept
    {
        return std::isfinite(value) && value >= 0.0;
    }

    static double maxAbs(const Vector& values) noexcept
    {
        double result = 0.0;
        for (double value : values)
            result = std::max(result, std::abs(value));
        return result;
    }

    static bool allFinite(const Vector& values) noexcept
    {
        for (double value : values)
            if (!std::isfinite(value))
                return false;
        return true;
    }

    void assemble(const Vector& x, double input, Matrix& jacobian,
                  Vector& residual) const noexcept
    {
        const double vbExternal = x[0];
        const double vbInternal = x[1];
        const double vcExternal = x[2];
        const double vcInternal = x[3];
        const double veExternal = x[4];
        const double veInternal = x[5];
        const double output = x[6];

        const double invBaseFeed = 1.0 / parameters_.baseFeedResistanceOhms;
        const double invBaseReturn = 1.0 / parameters_.baseReturnResistanceOhms;
        const double invCollector = 1.0 / parameters_.collectorResistanceOhms;
        const double invEmitter = 1.0 / parameters_.emitterResistanceOhms;
        const double invLoad = 1.0 / parameters_.outputLoadResistanceOhms;
        const double invBaseDevice = 1.0 / baseResistance_;
        const double invCollectorDevice = 1.0 / collectorDeviceResistance_;
        const double invEmitterDevice = 1.0 / emitterDeviceResistance_;
        const double invInputCap = 1.0 / inputCapacitorDenominator_;
        const double invOutputCap = 1.0 / outputCapacitorDenominator_;

        // External base node: divider, source/coupling capacitor and base lead.
        residual[0] =
            (vbExternal - parameters_.supplyVolts) * invBaseFeed +
            vbExternal * invBaseReturn +
            (vbExternal - input + inputCapacitorVoltage_) * invInputCap +
            (vbExternal - vbInternal) * invBaseDevice;
        jacobian[0][0] = invBaseFeed + invBaseReturn + invInputCap + invBaseDevice;
        jacobian[0][1] = -invBaseDevice;

        // External collector node: load resistor, transistor collector lead,
        // and output coupling capacitor.
        residual[2] =
            (vcExternal - parameters_.supplyVolts) * invCollector +
            (vcExternal - vcInternal) * invCollectorDevice +
            (vcExternal - output - outputCapacitorVoltage_) * invOutputCap;
        jacobian[2][2] = invCollector + invCollectorDevice + invOutputCap;
        jacobian[2][3] = -invCollectorDevice;
        jacobian[2][6] = -invOutputCap;

        // External emitter node and its internal series lead.
        residual[4] = veExternal * invEmitter +
                      (veExternal - veInternal) * invEmitterDevice;
        jacobian[4][4] = invEmitter + invEmitterDevice;
        jacobian[4][5] = -invEmitterDevice;

        // Output load and coupling capacitor.
        residual[6] = output * invLoad +
                      (output - vcExternal + outputCapacitorVoltage_) * invOutputCap;
        jacobian[6][2] = -invOutputCap;
        jacobian[6][6] = invLoad + invOutputCap;

        // Internal transistor terminals, including nonlinear device currents,
        // depletion capacitance and forward/reverse diffusion charge.
        const auto device = transistor_.evaluateWithDerivatives(
            vbInternal, vcInternal, veInternal);
        const std::size_t nodeIndex[3] = {1, 3, 5};
        const double currents[3] = {
            device.currents.baseAmps,
            device.currents.collectorAmps,
            device.currents.emitterAmps
        };

        const std::size_t externalIndex[3] = {0, 2, 4};
        const double seriesConductance[3] = {
            invBaseDevice, invCollectorDevice, invEmitterDevice
        };

        for (int row = 0; row < 3; ++row)
        {
            const std::size_t r = nodeIndex[row];
            const std::size_t ext = externalIndex[row];
            residual[r] =
                (x[r] - x[ext]) * seriesConductance[row] +
                currents[row] +
                (device.terminalChargeCoulombs[row] -
                 previousTransistorCharge_[row]) / timeStep_;

            jacobian[r][r] = seriesConductance[row];
            jacobian[r][ext] = -seriesConductance[row];

            for (int column = 0; column < 3; ++column)
            {
                const std::size_t c = nodeIndex[column];
                jacobian[r][c] += device.conductance[row][column] +
                                  device.capacitance[row][column] / timeStep_;
            }
        }
    }

    void assembleBias(const Vector& x, Matrix& jacobian,
                      Vector& residual) const noexcept
    {
        jacobian = {};
        residual = {};

        const double vbExternal = x[0];
        const double vbInternal = x[1];
        const double vcExternal = x[2];
        const double vcInternal = x[3];
        const double veExternal = x[4];
        const double veInternal = x[5];

        const double invBaseFeed = 1.0 / parameters_.baseFeedResistanceOhms;
        const double invBaseReturn = 1.0 / parameters_.baseReturnResistanceOhms;
        const double invCollector = 1.0 / parameters_.collectorResistanceOhms;
        const double invEmitter = 1.0 / parameters_.emitterResistanceOhms;
        const double invBaseDevice = 1.0 / baseResistance_;
        const double invCollectorDevice = 1.0 / collectorDeviceResistance_;
        const double invEmitterDevice = 1.0 / emitterDeviceResistance_;

        // DC operating-point equations: coupling capacitors are open circuits.
        residual[0] = (vbExternal - parameters_.supplyVolts) * invBaseFeed +
                      vbExternal * invBaseReturn +
                      (vbExternal - vbInternal) * invBaseDevice;
        jacobian[0][0] = invBaseFeed + invBaseReturn + invBaseDevice;
        jacobian[0][1] = -invBaseDevice;

        residual[2] = (vcExternal - parameters_.supplyVolts) * invCollector +
                      (vcExternal - vcInternal) * invCollectorDevice;
        jacobian[2][2] = invCollector + invCollectorDevice;
        jacobian[2][3] = -invCollectorDevice;

        residual[4] = veExternal * invEmitter +
                      (veExternal - veInternal) * invEmitterDevice;
        jacobian[4][4] = invEmitter + invEmitterDevice;
        jacobian[4][5] = -invEmitterDevice;

        const auto device = transistor_.evaluateWithDerivatives(
            vbInternal, vcInternal, veInternal);
        const std::size_t nodeIndex[3] = {1, 3, 5};
        const std::size_t externalIndex[3] = {0, 2, 4};
        const double currents[3] = {
            device.currents.baseAmps,
            device.currents.collectorAmps,
            device.currents.emitterAmps
        };
        const double seriesConductance[3] = {
            invBaseDevice, invCollectorDevice, invEmitterDevice
        };

        for (int row = 0; row < 3; ++row)
        {
            const std::size_t r = nodeIndex[row];
            const std::size_t ext = externalIndex[row];
            residual[r] = (x[r] - x[ext]) * seriesConductance[row] +
                          currents[row];
            jacobian[r][r] = seriesConductance[row];
            jacobian[r][ext] = -seriesConductance[row];

            for (int column = 0; column < 3; ++column)
                jacobian[r][nodeIndex[column]] += device.conductance[row][column];
        }

        // The output coupling capacitor is open at DC and the load returns
        // the output node to ground.
        residual[6] = x[6];
        jacobian[6][6] = 1.0;
    }

    bool initializeBiasPoint() noexcept
    {
        Vector candidate = {1.65, 1.65, 7.5, 7.5, 0.95, 0.95, 0.0};
        bool converged = false;
        double residualNorm = std::numeric_limits<double>::infinity();
        unsigned iterations = 0;

        // This runs only during prepare/reset, never in the audio callback.
        constexpr unsigned maximumBiasIterations = 32;
        for (; iterations < maximumBiasIterations; ++iterations)
        {
            Matrix jacobian{};
            Vector residual{};
            assembleBias(candidate, jacobian, residual);
            residualNorm = maxAbs(residual);

            if (residualNorm <= parameters_.residualToleranceAmps)
            {
                converged = true;
                break;
            }

            Vector delta{};
            for (std::size_t i = 0; i < UnknownCount; ++i)
                delta[i] = -residual[i];

            if (!solveLinearSystem(jacobian, delta))
                break;

            for (std::size_t i = 0; i < UnknownCount; ++i)
                candidate[i] += delta[i];

            if (!allFinite(candidate))
                break;
        }

        if (!converged)
        {
            Matrix checkJacobian{};
            Vector checkResidual{};
            assembleBias(candidate, checkJacobian, checkResidual);
            residualNorm = maxAbs(checkResidual);
            converged = allFinite(candidate) &&
                        residualNorm <= parameters_.residualToleranceAmps * 10.0;
        }

        if (!converged)
        {
            lastResult_ = {};
            lastResult_.converged = false;
            lastResult_.residualAmps = residualNorm;
            return false;
        }

        const auto biasDevice = transistor_.evaluateWithDerivatives(
            candidate[1], candidate[3], candidate[5]);

        solution_ = candidate;
        inputCapacitorVoltage_ = -candidate[0];
        outputCapacitorVoltage_ = candidate[2] - candidate[6];
        for (int terminal = 0; terminal < 3; ++terminal)
            previousTransistorCharge_[terminal] =
                biasDevice.terminalChargeCoulombs[terminal];

        lastResult_ = {};
        lastResult_.converged = true;
        lastResult_.iterations = iterations;
        lastResult_.residualAmps = residualNorm;
        lastResult_.baseVolts = candidate[0];
        lastResult_.collectorVolts = candidate[2];
        lastResult_.emitterVolts = candidate[4];
        lastResult_.collectorEmitterVolts = candidate[3] - candidate[5];
        lastResult_.collectorCurrentAmps = biasDevice.currents.collectorAmps;
        lastResult_.baseCurrentAmps = biasDevice.currents.baseAmps;
        lastResult_.emitterCurrentAmps = biasDevice.currents.emitterAmps;
        lastResult_.output = static_cast<float>(candidate[6]);
        biasOperatingPoint_ = lastResult_;
        return true;
    }

    static bool solveLinearSystem(Matrix matrix, Vector& rhs) noexcept
    {
        for (std::size_t column = 0; column < UnknownCount; ++column)
        {
            std::size_t pivot = column;
            double pivotMagnitude = std::abs(matrix[column][column]);
            for (std::size_t row = column + 1; row < UnknownCount; ++row)
            {
                const double magnitude = std::abs(matrix[row][column]);
                if (magnitude > pivotMagnitude)
                {
                    pivotMagnitude = magnitude;
                    pivot = row;
                }
            }

            if (!std::isfinite(pivotMagnitude) || pivotMagnitude < 1.0e-18)
                return false;

            if (pivot != column)
            {
                std::swap(matrix[pivot], matrix[column]);
                std::swap(rhs[pivot], rhs[column]);
            }

            const double diagonal = matrix[column][column];
            for (std::size_t row = column + 1; row < UnknownCount; ++row)
            {
                const double factor = matrix[row][column] / diagonal;
                if (factor == 0.0)
                    continue;

                matrix[row][column] = 0.0;
                for (std::size_t c = column + 1; c < UnknownCount; ++c)
                    matrix[row][c] -= factor * matrix[column][c];
                rhs[row] -= factor * rhs[column];
            }
        }

        Vector solution{};
        for (std::size_t reverse = UnknownCount; reverse-- > 0;)
        {
            double value = rhs[reverse];
            for (std::size_t column = reverse + 1; column < UnknownCount; ++column)
                value -= matrix[reverse][column] * solution[column];

            const double diagonal = matrix[reverse][reverse];
            if (!std::isfinite(diagonal) || std::abs(diagonal) < 1.0e-18)
                return false;
            solution[reverse] = value / diagonal;
        }

        rhs = solution;
        return true;
    }

    double sampleRate_ = 44100.0;
    double timeStep_ = 1.0 / 44100.0;
    Parameters parameters_{};
    EbersMollNpn transistor_{};
    Vector solution_{};
    double baseResistance_ = 100.0;
    double collectorDeviceResistance_ = 1.0;
    double emitterDeviceResistance_ = 1.0;
    double inputCapacitorDenominator_ = 1.0;
    double outputCapacitorDenominator_ = 1.0;
    double inputCapacitorVoltage_ = 0.0;
    double outputCapacitorVoltage_ = 0.0;
    double previousTransistorCharge_[3]{};
    ProcessResult lastResult_{};
    ProcessResult biasOperatingPoint_{};
    bool configured_ = false;
    bool biasReady_ = false;
};

} // namespace PGAudio::Circuit
