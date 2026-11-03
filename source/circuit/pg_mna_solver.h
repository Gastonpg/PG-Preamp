#pragma once

#include "pg_resistor.h"
#include "pg_bjt_ebers_moll.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace PGAudio::Circuit {

// Small, double-precision modified-nodal-analysis solver intended for
// component/network validation. It is not a real-time audio processor:
// dense matrix allocation and Newton iterations are deliberately kept out
// of the plugin callback until circuit topology and cost are validated.
class MnaCircuit
{
public:
    using Node = int;
    static constexpr Node ground = 0;

    struct StepResult
    {
        bool converged = false;
        unsigned iterations = 0;
        double maxResidual = std::numeric_limits<double>::infinity();
    };

    Node createNode()
    {
        nodeVoltages_.push_back(0.0);
        return static_cast<Node>(nodeVoltages_.size() - 1);
    }

    std::size_t addResistor(Node positive, Node negative, double resistanceOhms)
    {
        ResistorElement element;
        element.positive = positive;
        element.negative = negative;
        element.device = Resistor(resistanceOhms);
        resistors_.push_back(element);
        return resistors_.size() - 1;
    }

    bool setResistorTemperatureCoefficients(std::size_t index, double tc1,
                                            double tc2 = 0.0) noexcept
    {
        if (index >= resistors_.size())
            return false;
        resistors_[index].device.setTemperatureCoefficients(tc1, tc2);
        return true;
    }

    void addCapacitor(Node positive, Node negative, double capacitanceFarads,
                      double esrOhms = 0.0,
                      double leakageResistanceOhms = std::numeric_limits<double>::infinity(),
                      double initialVoltage = 0.0)
    {
        CapacitorElement element;
        element.positive = positive;
        element.negative = negative;
        element.capacitanceFarads =
            std::isfinite(capacitanceFarads) && capacitanceFarads > 0.0
                ? capacitanceFarads : 1.0e-15;
        element.esrOhms = std::isfinite(esrOhms) && esrOhms > 0.0 ? esrOhms : 0.0;
        element.leakageResistanceOhms =
            std::isinf(leakageResistanceOhms) ||
            (std::isfinite(leakageResistanceOhms) && leakageResistanceOhms > 0.0)
                ? leakageResistanceOhms : std::numeric_limits<double>::infinity();
        element.initialVoltage = std::isfinite(initialVoltage) ? initialVoltage : 0.0;
        element.previousVoltage = element.initialVoltage;

        // Internal dielectric node models terminal -- ESR -- (C || leakage).
        if (element.esrOhms > 0.0)
        {
            element.dielectricNode = createNode();
            // Seed the hidden dielectric node so the initial capacitor
            // voltage is consistent with the caller's node-voltage guesses.
            nodeVoltages_[static_cast<std::size_t>(element.dielectricNode)] =
                (negative == ground ? 0.0 : nodeVoltages_[static_cast<std::size_t>(negative)]) +
                element.initialVoltage;
        }
        else
            element.dielectricNode = positive;

        capacitors_.push_back(element);
    }

    void addInductor(Node positive, Node negative, double inductanceHenries,
                     double windingResistanceOhms = 0.0,
                     double initialCurrentAmps = 0.0)
    {
        InductorElement element;
        element.positive = positive;
        element.negative = negative;
        element.inductanceHenries =
            std::isfinite(inductanceHenries) && inductanceHenries > 0.0
                ? inductanceHenries : 1.0e-12;
        element.windingResistanceOhms =
            std::isfinite(windingResistanceOhms) && windingResistanceOhms > 0.0
                ? windingResistanceOhms : 0.0;
        element.previousCurrentAmps =
            std::isfinite(initialCurrentAmps) ? initialCurrentAmps : 0.0;
        inductors_.push_back(element);
    }

    std::size_t addVoltageSource(Node positive, Node negative, double voltage)
    {
        voltageSources_.push_back({positive, negative,
                                   std::isfinite(voltage) ? voltage : 0.0});
        return voltageSources_.size() - 1;
    }

    bool setVoltageSource(std::size_t index, double voltage) noexcept
    {
        if (index >= voltageSources_.size() || !std::isfinite(voltage))
            return false;
        voltageSources_[index].voltage = voltage;
        return true;
    }

    void addCurrentSource(Node positive, Node negative, double currentAmps)
    {
        currentSources_.push_back({positive, negative,
                                   std::isfinite(currentAmps) ? currentAmps : 0.0});
    }

    void addNpn(Node base, Node collector, Node emitter, const EbersMollNpn& device)
    {
        transistors_.push_back({base, collector, emitter, device});
    }

    bool setInitialNodeVoltage(Node node, double voltage) noexcept
    {
        if (!validNode(node) || !std::isfinite(voltage))
            return false;
        if (node != ground)
            nodeVoltages_[static_cast<std::size_t>(node)] = voltage;
        return true;
    }

    double nodeVoltage(Node node) const noexcept
    {
        if (!validNode(node) || node == ground)
            return 0.0;
        return nodeVoltages_[static_cast<std::size_t>(node)];
    }

    double voltageSourceCurrent(std::size_t index) const noexcept
    {
        return index < lastVoltageSourceCurrents_.size()
            ? lastVoltageSourceCurrents_[index] : 0.0;
    }

    double inductorCurrent(std::size_t index) const noexcept
    {
        return index < lastInductorCurrents_.size()
            ? lastInductorCurrents_[index] : 0.0;
    }

    std::size_t nodeCount() const noexcept
    {
        return nodeVoltages_.empty() ? 0 : nodeVoltages_.size() - 1;
    }

    StepResult step(double timeStepSeconds, double temperatureKelvin = 293.15,
                    unsigned maxIterations = 80, double tolerance = 1.0e-10)
    {
        StepResult result;
        if (!std::isfinite(timeStepSeconds) || timeStepSeconds <= 0.0 ||
            !std::isfinite(temperatureKelvin) || temperatureKelvin <= 0.0 ||
            maxIterations == 0 || !std::isfinite(tolerance) || tolerance <= 0.0)
            return result;

        const std::size_t nodeUnknowns = nodeCount();
        const std::size_t sourceOffset = nodeUnknowns;
        const std::size_t inductorOffset = sourceOffset + voltageSources_.size();
        const std::size_t unknowns = inductorOffset + inductors_.size();
        if (unknowns == 0)
        {
            result.converged = true;
            result.iterations = 0;
            result.maxResidual = 0.0;
            return result;
        }

        std::vector<double> x(unknowns, 0.0);
        for (std::size_t i = 0; i < nodeUnknowns; ++i)
            x[i] = nodeVoltages_[i + 1];
        for (std::size_t i = 0; i < voltageSources_.size(); ++i)
            x[sourceOffset + i] = i < lastVoltageSourceCurrents_.size()
                ? lastVoltageSourceCurrents_[i] : 0.0;
        for (std::size_t i = 0; i < inductors_.size(); ++i)
            x[inductorOffset + i] = inductors_[i].previousCurrentAmps;

        for (unsigned iteration = 0; iteration < maxIterations; ++iteration)
        {
            std::vector<std::vector<double>> jacobian(
                unknowns, std::vector<double>(unknowns, 0.0));
            std::vector<double> residual(unknowns, 0.0);
            assemble(x, timeStepSeconds, temperatureKelvin,
                     sourceOffset, inductorOffset, jacobian, residual);

            double residualNorm = 0.0;
            for (double value : residual)
                residualNorm = std::max(residualNorm, std::abs(value));

            result.iterations = iteration + 1;
            result.maxResidual = residualNorm;
            if (residualNorm <= tolerance)
            {
                result.converged = true;
                break;
            }

            for (double& value : residual)
                value = -value;

            std::vector<double> delta;
            if (!solveLinearSystem(jacobian, residual, delta))
                return result;

            double updateNorm = 0.0;
            for (std::size_t i = 0; i < unknowns; ++i)
            {
                if (!std::isfinite(delta[i]))
                    return result;
                updateNorm = std::max(updateNorm, std::abs(delta[i]));
                x[i] += 0.8 * delta[i];
            }

            if (updateNorm <= tolerance)
            {
                std::vector<std::vector<double>> checkJacobian(
                    unknowns, std::vector<double>(unknowns, 0.0));
                std::vector<double> checkResidual(unknowns, 0.0);
                assemble(x, timeStepSeconds, temperatureKelvin,
                         sourceOffset, inductorOffset, checkJacobian, checkResidual);
                result.maxResidual = 0.0;
                for (double value : checkResidual)
                    result.maxResidual = std::max(result.maxResidual, std::abs(value));
                result.converged = result.maxResidual <= tolerance * 10.0;
                if (result.converged)
                    break;
            }
        }

        if (!result.converged)
            return result;

        for (std::size_t i = 0; i < nodeUnknowns; ++i)
            nodeVoltages_[i + 1] = x[i];

        lastVoltageSourceCurrents_.resize(voltageSources_.size(), 0.0);
        for (std::size_t i = 0; i < voltageSources_.size(); ++i)
            lastVoltageSourceCurrents_[i] = x[sourceOffset + i];

        lastInductorCurrents_.resize(inductors_.size(), 0.0);
        for (std::size_t i = 0; i < inductors_.size(); ++i)
        {
            inductors_[i].previousCurrentAmps = x[inductorOffset + i];
            lastInductorCurrents_[i] = x[inductorOffset + i];
        }

        for (auto& capacitor : capacitors_)
            capacitor.previousVoltage =
                valueAt(x, capacitor.dielectricNode) - valueAt(x, capacitor.negative);

        return result;
    }

private:
    struct ResistorElement
    {
        Node positive = ground;
        Node negative = ground;
        Resistor device;
    };
    struct CapacitorElement
    {
        Node positive = ground;
        Node negative = ground;
        Node dielectricNode = ground;
        double capacitanceFarads = 1.0e-9;
        double esrOhms = 0.0;
        double leakageResistanceOhms = std::numeric_limits<double>::infinity();
        double initialVoltage = 0.0;
        double previousVoltage = 0.0;
    };
    struct InductorElement
    {
        Node positive = ground;
        Node negative = ground;
        double inductanceHenries = 1.0e-3;
        double windingResistanceOhms = 0.0;
        double previousCurrentAmps = 0.0;
    };
    struct VoltageSource
    {
        Node positive;
        Node negative;
        double voltage;
    };
    struct CurrentSource
    {
        Node positive;
        Node negative;
        double current;
    };
    struct Transistor
    {
        Node base;
        Node collector;
        Node emitter;
        EbersMollNpn device;
    };

    bool validNode(Node node) const noexcept
    {
        return node >= 0 && static_cast<std::size_t>(node) < nodeVoltages_.size();
    }

    static double valueAt(const std::vector<double>& x, Node node) noexcept
    {
        return node == ground ? 0.0 : x[static_cast<std::size_t>(node - 1)];
    }

    static void stampConductance(Node positive, Node negative, double conductance,
                                 const std::vector<double>& x,
                                 std::vector<std::vector<double>>& jacobian,
                                 std::vector<double>& residual)
    {
        const double current = conductance *
            (valueAt(x, positive) - valueAt(x, negative));
        if (positive != ground)
        {
            const std::size_t p = static_cast<std::size_t>(positive - 1);
            residual[p] += current;
            jacobian[p][p] += conductance;
            if (negative != ground)
                jacobian[p][static_cast<std::size_t>(negative - 1)] -= conductance;
        }
        if (negative != ground)
        {
            const std::size_t n = static_cast<std::size_t>(negative - 1);
            residual[n] -= current;
            jacobian[n][n] += conductance;
            if (positive != ground)
                jacobian[n][static_cast<std::size_t>(positive - 1)] -= conductance;
        }
    }

    static void stampCurrent(Node positive, Node negative, double current,
                             std::vector<double>& residual)
    {
        if (positive != ground)
            residual[static_cast<std::size_t>(positive - 1)] += current;
        if (negative != ground)
            residual[static_cast<std::size_t>(negative - 1)] -= current;
    }

    void assemble(const std::vector<double>& x, double dt, double temperatureKelvin,
                  std::size_t sourceOffset, std::size_t inductorOffset,
                  std::vector<std::vector<double>>& jacobian,
                  std::vector<double>& residual) const
    {
        for (const auto& resistor : resistors_)
        {
            const double resistance =
                resistor.device.resistanceAtTemperature(temperatureKelvin);
            stampConductance(resistor.positive, resistor.negative,
                             1.0 / resistance, x, jacobian, residual);
        }

        for (const auto& capacitor : capacitors_)
        {
            if (capacitor.esrOhms > 0.0)
                stampConductance(capacitor.positive, capacitor.dielectricNode,
                                 1.0 / capacitor.esrOhms, x, jacobian, residual);
            if (std::isfinite(capacitor.leakageResistanceOhms))
                stampConductance(capacitor.dielectricNode, capacitor.negative,
                                 1.0 / capacitor.leakageResistanceOhms,
                                 x, jacobian, residual);

            const double conductance = capacitor.capacitanceFarads / dt;
            const double voltage = valueAt(x, capacitor.dielectricNode) -
                                   valueAt(x, capacitor.negative);
            stampCurrent(capacitor.dielectricNode, capacitor.negative,
                         conductance * (voltage - capacitor.previousVoltage), residual);

            if (capacitor.dielectricNode != ground)
            {
                const std::size_t p =
                    static_cast<std::size_t>(capacitor.dielectricNode - 1);
                jacobian[p][p] += conductance;
                if (capacitor.negative != ground)
                    jacobian[p][static_cast<std::size_t>(capacitor.negative - 1)] -= conductance;
            }
            if (capacitor.negative != ground)
            {
                const std::size_t n = static_cast<std::size_t>(capacitor.negative - 1);
                jacobian[n][n] += conductance;
                if (capacitor.dielectricNode != ground)
                    jacobian[n][static_cast<std::size_t>(capacitor.dielectricNode - 1)] -= conductance;
            }
        }

        for (const auto& source : currentSources_)
            stampCurrent(source.positive, source.negative, source.current, residual);

        for (std::size_t i = 0; i < voltageSources_.size(); ++i)
        {
            const auto& source = voltageSources_[i];
            const std::size_t branch = sourceOffset + i;
            stampCurrent(source.positive, source.negative, x[branch], residual);

            if (source.positive != ground)
                jacobian[static_cast<std::size_t>(source.positive - 1)][branch] += 1.0;
            if (source.negative != ground)
                jacobian[static_cast<std::size_t>(source.negative - 1)][branch] -= 1.0;

            residual[branch] += valueAt(x, source.positive) -
                                valueAt(x, source.negative) - source.voltage;
            if (source.positive != ground)
                jacobian[branch][static_cast<std::size_t>(source.positive - 1)] += 1.0;
            if (source.negative != ground)
                jacobian[branch][static_cast<std::size_t>(source.negative - 1)] -= 1.0;
        }

        for (std::size_t i = 0; i < inductors_.size(); ++i)
        {
            const auto& inductor = inductors_[i];
            const std::size_t branch = inductorOffset + i;
            const double current = x[branch];
            const double inductiveTerm = inductor.inductanceHenries / dt;
            const double branchResistance =
                inductiveTerm + inductor.windingResistanceOhms;

            stampCurrent(inductor.positive, inductor.negative, current, residual);
            if (inductor.positive != ground)
                jacobian[static_cast<std::size_t>(inductor.positive - 1)][branch] += 1.0;
            if (inductor.negative != ground)
                jacobian[static_cast<std::size_t>(inductor.negative - 1)][branch] -= 1.0;

            residual[branch] += valueAt(x, inductor.positive) -
                                valueAt(x, inductor.negative) -
                                branchResistance * current +
                                inductiveTerm * inductor.previousCurrentAmps;
            if (inductor.positive != ground)
                jacobian[branch][static_cast<std::size_t>(inductor.positive - 1)] += 1.0;
            if (inductor.negative != ground)
                jacobian[branch][static_cast<std::size_t>(inductor.negative - 1)] -= 1.0;
            jacobian[branch][branch] -= branchResistance;
        }

        for (const auto& transistor : transistors_)
        {
            const auto evaluation = transistor.device.evaluateWithDerivatives(
                valueAt(x, transistor.base),
                valueAt(x, transistor.collector),
                valueAt(x, transistor.emitter));
            const Node nodes[3] = {
                transistor.base, transistor.collector, transistor.emitter
            };
            const double currents[3] = {
                evaluation.currents.baseAmps,
                evaluation.currents.collectorAmps,
                evaluation.currents.emitterAmps
            };

            for (int row = 0; row < 3; ++row)
            {
                if (nodes[row] == ground)
                    continue;
                const std::size_t r = static_cast<std::size_t>(nodes[row] - 1);
                residual[r] += currents[row];
                for (int column = 0; column < 3; ++column)
                {
                    if (nodes[column] != ground)
                        jacobian[r][static_cast<std::size_t>(nodes[column] - 1)] +=
                            evaluation.conductance[row][column];
                }
            }
        }
    }

    static bool solveLinearSystem(std::vector<std::vector<double>> matrix,
                                  std::vector<double> rhs,
                                  std::vector<double>& solution)
    {
        const std::size_t n = rhs.size();
        for (std::size_t column = 0; column < n; ++column)
        {
            std::size_t pivot = column;
            double pivotMagnitude = std::abs(matrix[column][column]);
            for (std::size_t row = column + 1; row < n; ++row)
            {
                const double magnitude = std::abs(matrix[row][column]);
                if (magnitude > pivotMagnitude)
                {
                    pivotMagnitude = magnitude;
                    pivot = row;
                }
            }
            if (!std::isfinite(pivotMagnitude) || pivotMagnitude < 1.0e-24)
                return false;

            if (pivot != column)
            {
                std::swap(matrix[pivot], matrix[column]);
                std::swap(rhs[pivot], rhs[column]);
            }

            const double diagonal = matrix[column][column];
            for (std::size_t row = column + 1; row < n; ++row)
            {
                const double factor = matrix[row][column] / diagonal;
                if (factor == 0.0)
                    continue;
                matrix[row][column] = 0.0;
                for (std::size_t c = column + 1; c < n; ++c)
                    matrix[row][c] -= factor * matrix[column][c];
                rhs[row] -= factor * rhs[column];
            }
        }

        solution.assign(n, 0.0);
        for (std::size_t reverse = n; reverse-- > 0;)
        {
            double value = rhs[reverse];
            for (std::size_t column = reverse + 1; column < n; ++column)
                value -= matrix[reverse][column] * solution[column];
            const double diagonal = matrix[reverse][reverse];
            if (!std::isfinite(diagonal) || std::abs(diagonal) < 1.0e-24)
                return false;
            solution[reverse] = value / diagonal;
        }
        return true;
    }

    std::vector<double> nodeVoltages_{0.0};
    std::vector<double> lastVoltageSourceCurrents_;
    std::vector<double> lastInductorCurrents_;
    std::vector<ResistorElement> resistors_;
    std::vector<CapacitorElement> capacitors_;
    std::vector<InductorElement> inductors_;
    std::vector<VoltageSource> voltageSources_;
    std::vector<CurrentSource> currentSources_;
    std::vector<Transistor> transistors_;
};

} // namespace PGAudio::Circuit
