#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace PGAudio::Circuit {

struct BjtTerminalCurrents
{
    // Currents are positive into each transistor terminal; their sum is zero.
    double collectorAmps = 0.0;
    double baseAmps = 0.0;
    double emitterAmps = 0.0;
};

struct BjtEvaluation
{
    BjtTerminalCurrents currents;
    // d(I_terminal[row])/d(V_terminal[column]), order B, C, E.
    double conductance[3][3]{};
    // Stored terminal charge and dQ_terminal[row]/dV_terminal[column].
    double terminalChargeCoulombs[3]{};
    double capacitance[3][3]{};
};

struct BjtNoiseEvaluation
{
    // One-sided, uncorrelated first-order source spectral densities.
    // These are analysis outputs only; the audio processor does not inject them.
    double baseResistanceVoltageNoiseVPerSqrtHz = 0.0;
    double collectorResistanceVoltageNoiseVPerSqrtHz = 0.0;
    double emitterResistanceVoltageNoiseVPerSqrtHz = 0.0;
    double baseCurrentShotNoiseAPerSqrtHz = 0.0;
    double collectorCurrentShotNoiseAPerSqrtHz = 0.0;
};

struct BjtAdvancedParameters
{
    double forwardEarlyVoltage = std::numeric_limits<double>::infinity();
    double reverseEarlyVoltage = std::numeric_limits<double>::infinity();
    double baseResistanceOhms = 0.0;
    double collectorResistanceOhms = 0.0;
    double emitterResistanceOhms = 0.0;
    double baseEmitterCapacitanceFarads = 0.0;
    double baseCollectorCapacitanceFarads = 0.0;
    double baseEmitterPotentialVolts = 0.75;
    double baseCollectorPotentialVolts = 0.75;
    double baseEmitterGradingCoefficient = 0.33;
    double baseCollectorGradingCoefficient = 0.33;
    double forwardTransitTimeSeconds = 0.0;
    double reverseTransitTimeSeconds = 0.0;
    double forwardEmissionCoefficient = 1.0;
    double reverseEmissionCoefficient = 1.0;
    bool useGummelPoonBaseCharge = false;
    double betaRollOffExponent = 0.5;

    // Optional Gummel-Poon-style base-current terms. Zero leakage and infinite
    // roll-off knees preserve the previous Ebers-Moll behavior by default.
    double baseEmitterLeakageSaturationCurrentAmps = 0.0;
    double baseCollectorLeakageSaturationCurrentAmps = 0.0;
    double baseEmitterLeakageEmissionCoefficient = 1.5;
    double baseCollectorLeakageEmissionCoefficient = 2.0;
    double forwardBetaRollOffCurrentAmps = std::numeric_limits<double>::infinity();
    double reverseBetaRollOffCurrentAmps = std::numeric_limits<double>::infinity();
};

// Charge-controlled Ebers-Moll / Gummel-Poon-style NPN primitive.
// The DC transport model remains Ebers-Moll, with a forward/reverse Early
// output-conductance correction. RB/RC/RE, depletion charge and transit-time
// diffusion charge are represented explicitly. This is a useful compact
// transistor model, not a full replacement for every SPICE Gummel-Poon term.
class EbersMollNpn
{
public:
    void prepare(double saturationCurrentAtNominalAmps = 1.0e-14,
                 double forwardBetaAtNominal = 100.0,
                 double reverseBetaAtNominal = 1.0,
                 double temperatureKelvin = 300.15,
                 double nominalTemperatureKelvin = 300.15,
                 double bandGapEnergyElectronVolts = 1.11,
                 double saturationCurrentExponent = 3.0,
                 double betaTemperatureExponent = 0.0) noexcept
    {
        nominalSaturationCurrentAmps_ =
            std::isfinite(saturationCurrentAtNominalAmps) && saturationCurrentAtNominalAmps > 0.0
                ? saturationCurrentAtNominalAmps : 1.0e-14;
        nominalForwardBeta_ = std::isfinite(forwardBetaAtNominal) && forwardBetaAtNominal > 0.0
                                  ? forwardBetaAtNominal : 100.0;
        nominalReverseBeta_ = std::isfinite(reverseBetaAtNominal) && reverseBetaAtNominal > 0.0
                                  ? reverseBetaAtNominal : 1.0;
        temperatureKelvin_ = std::isfinite(temperatureKelvin) && temperatureKelvin > 0.0
                                  ? temperatureKelvin : 300.15;
        nominalTemperatureKelvin_ =
            std::isfinite(nominalTemperatureKelvin) && nominalTemperatureKelvin > 0.0
                ? nominalTemperatureKelvin : 300.15;
        bandGapEnergyElectronVolts_ =
            std::isfinite(bandGapEnergyElectronVolts) && bandGapEnergyElectronVolts >= 0.0
                ? bandGapEnergyElectronVolts : 1.11;
        saturationCurrentExponent_ = std::isfinite(saturationCurrentExponent)
                                         ? saturationCurrentExponent : 3.0;
        betaTemperatureExponent_ = std::isfinite(betaTemperatureExponent)
                                      ? betaTemperatureExponent : 0.0;

        constexpr double boltzmann = 1.380649e-23;
        constexpr double electronCharge = 1.602176634e-19;
        thermalVoltage_ = boltzmann * temperatureKelvin_ / electronCharge;

        const double temperatureRatio = temperatureKelvin_ / nominalTemperatureKelvin_;
        const double exponent = bandGapEnergyElectronVolts_ *
            (temperatureKelvin_ - nominalTemperatureKelvin_) /
            (thermalVoltage_ * nominalTemperatureKelvin_);
        saturationCurrentAmps_ = nominalSaturationCurrentAmps_ *
            std::pow(temperatureRatio, saturationCurrentExponent_) * std::exp(exponent);

        forwardBeta_ = nominalForwardBeta_ * std::pow(temperatureRatio, betaTemperatureExponent_);
        reverseBeta_ = nominalReverseBeta_ * std::pow(temperatureRatio, betaTemperatureExponent_);
        alphaForward_ = forwardBeta_ / (forwardBeta_ + 1.0);
        alphaReverse_ = reverseBeta_ / (reverseBeta_ + 1.0);
        prepared_ = true;
    }

    void setAdvancedParameters(const BjtAdvancedParameters& parameters) noexcept
    {
        advanced_.forwardEarlyVoltage = positiveOrInfinity(parameters.forwardEarlyVoltage);
        advanced_.reverseEarlyVoltage = positiveOrInfinity(parameters.reverseEarlyVoltage);
        advanced_.baseResistanceOhms = nonNegative(parameters.baseResistanceOhms);
        advanced_.collectorResistanceOhms = nonNegative(parameters.collectorResistanceOhms);
        advanced_.emitterResistanceOhms = nonNegative(parameters.emitterResistanceOhms);
        advanced_.baseEmitterCapacitanceFarads =
            nonNegative(parameters.baseEmitterCapacitanceFarads);
        advanced_.baseCollectorCapacitanceFarads =
            nonNegative(parameters.baseCollectorCapacitanceFarads);
        advanced_.baseEmitterPotentialVolts = positiveOr(parameters.baseEmitterPotentialVolts, 0.75);
        advanced_.baseCollectorPotentialVolts = positiveOr(parameters.baseCollectorPotentialVolts, 0.75);
        advanced_.baseEmitterGradingCoefficient =
            std::isfinite(parameters.baseEmitterGradingCoefficient)
                ? std::clamp(parameters.baseEmitterGradingCoefficient, 0.0, 0.95) : 0.33;
        advanced_.baseCollectorGradingCoefficient =
            std::isfinite(parameters.baseCollectorGradingCoefficient)
                ? std::clamp(parameters.baseCollectorGradingCoefficient, 0.0, 0.95) : 0.33;
        advanced_.forwardTransitTimeSeconds = nonNegative(parameters.forwardTransitTimeSeconds);
        advanced_.reverseTransitTimeSeconds = nonNegative(parameters.reverseTransitTimeSeconds);
        advanced_.forwardEmissionCoefficient =
            positiveOr(parameters.forwardEmissionCoefficient, 1.0);
        advanced_.reverseEmissionCoefficient =
            positiveOr(parameters.reverseEmissionCoefficient, 1.0);
        advanced_.useGummelPoonBaseCharge = parameters.useGummelPoonBaseCharge;
        advanced_.betaRollOffExponent = std::isfinite(parameters.betaRollOffExponent)
            ? std::clamp(parameters.betaRollOffExponent, 0.1, 1.0) : 0.5;
        advanced_.baseEmitterLeakageSaturationCurrentAmps =
            nonNegative(parameters.baseEmitterLeakageSaturationCurrentAmps);
        advanced_.baseCollectorLeakageSaturationCurrentAmps =
            nonNegative(parameters.baseCollectorLeakageSaturationCurrentAmps);
        advanced_.baseEmitterLeakageEmissionCoefficient =
            positiveOr(parameters.baseEmitterLeakageEmissionCoefficient, 1.5);
        advanced_.baseCollectorLeakageEmissionCoefficient =
            positiveOr(parameters.baseCollectorLeakageEmissionCoefficient, 2.0);
        advanced_.forwardBetaRollOffCurrentAmps =
            positiveOrInfinity(parameters.forwardBetaRollOffCurrentAmps);
        advanced_.reverseBetaRollOffCurrentAmps =
            positiveOrInfinity(parameters.reverseBetaRollOffCurrentAmps);
    }

    const BjtAdvancedParameters& advancedParameters() const noexcept { return advanced_; }
    double baseResistanceOhms() const noexcept { return advanced_.baseResistanceOhms; }
    double collectorResistanceOhms() const noexcept { return advanced_.collectorResistanceOhms; }
    double emitterResistanceOhms() const noexcept { return advanced_.emitterResistanceOhms; }

    BjtTerminalCurrents evaluate(double baseVolts, double collectorVolts,
                                 double emitterVolts) const noexcept
    {
        return evaluateWithDerivatives(baseVolts, collectorVolts, emitterVolts).currents;
    }

    BjtEvaluation evaluateWithDerivatives(double baseVolts, double collectorVolts,
                                          double emitterVolts) const noexcept
    {
        BjtEvaluation result;
        if (!prepared_ || !std::isfinite(baseVolts) ||
            !std::isfinite(collectorVolts) || !std::isfinite(emitterVolts))
            return result;

        const double vbe = baseVolts - emitterVolts;
        const double vbc = baseVolts - collectorVolts;
        const double forwardThermalVoltage =
            thermalVoltage_ * advanced_.forwardEmissionCoefficient;
        const double reverseThermalVoltage =
            thermalVoltage_ * advanced_.reverseEmissionCoefficient;
        const double vbeNormalized = vbe / forwardThermalVoltage;
        const double vbcNormalized = vbc / reverseThermalVoltage;
        const double vbeClamped = std::clamp(vbeNormalized, -80.0, 40.0);
        const double vbcClamped = std::clamp(vbcNormalized, -80.0, 40.0);
        const double forwardJunction = saturationCurrentAmps_ * std::expm1(vbeClamped);
        const double reverseJunction = saturationCurrentAmps_ * std::expm1(vbcClamped);
        const double forwardDerivative = vbeNormalized == vbeClamped
            ? saturationCurrentAmps_ * std::exp(vbeClamped) / forwardThermalVoltage : 0.0;
        const double reverseDerivative = vbcNormalized == vbcClamped
            ? saturationCurrentAmps_ * std::exp(vbcClamped) / reverseThermalVoltage : 0.0;

        const double forwardLeakageNormalized =
            vbe / (thermalVoltage_ *
                  advanced_.baseEmitterLeakageEmissionCoefficient);
        const double reverseLeakageNormalized =
            vbc / (thermalVoltage_ *
                  advanced_.baseCollectorLeakageEmissionCoefficient);
        const double forwardLeakageClamped =
            std::clamp(forwardLeakageNormalized, -80.0, 40.0);
        const double reverseLeakageClamped =
            std::clamp(reverseLeakageNormalized, -80.0, 40.0);
        const double forwardLeakage =
            advanced_.baseEmitterLeakageSaturationCurrentAmps *
            std::expm1(forwardLeakageClamped);
        const double reverseLeakage =
            advanced_.baseCollectorLeakageSaturationCurrentAmps *
            std::expm1(reverseLeakageClamped);
        const double forwardLeakageDerivative =
            forwardLeakageNormalized == forwardLeakageClamped
                ? advanced_.baseEmitterLeakageSaturationCurrentAmps *
                    std::exp(forwardLeakageClamped) /
                    (advanced_.baseEmitterLeakageEmissionCoefficient * thermalVoltage_)
                : 0.0;
        const double reverseLeakageDerivative =
            reverseLeakageNormalized == reverseLeakageClamped
                ? advanced_.baseCollectorLeakageSaturationCurrentAmps *
                    std::exp(reverseLeakageClamped) /
                    (advanced_.baseCollectorLeakageEmissionCoefficient * thermalVoltage_)
                : 0.0;

        const double transport = forwardJunction - reverseJunction / alphaReverse_;
        const double ibForward = 1.0 / alphaForward_ - 1.0;
        const double ibReverse = 1.0 / alphaReverse_ - 1.0;
        const double dTransport[3] = {
            forwardDerivative - reverseDerivative / alphaReverse_,
            reverseDerivative / alphaReverse_,
            -forwardDerivative
        };

        double earlyFactor = 1.0;
        double earlySlope = 0.0;
        const double vce = collectorVolts - emitterVolts;
        if (vce >= 0.0 && std::isfinite(advanced_.forwardEarlyVoltage))
        {
            const double raw = 1.0 + vce / advanced_.forwardEarlyVoltage;
            earlyFactor = std::max(0.05, raw);
            if (raw > 0.05)
                earlySlope = 1.0 / advanced_.forwardEarlyVoltage;
        }
        else if (vce < 0.0 && std::isfinite(advanced_.reverseEarlyVoltage))
        {
            const double raw = 1.0 - vce / advanced_.reverseEarlyVoltage;
            earlyFactor = std::max(0.05, raw);
            if (raw > 0.05)
                earlySlope = -1.0 / advanced_.reverseEarlyVoltage;
        }

        const double forwardRollOff = std::isfinite(
            advanced_.forwardBetaRollOffCurrentAmps) &&
            advanced_.forwardBetaRollOffCurrentAmps > 0.0 &&
            forwardJunction > 0.0
                ? 1.0 + forwardJunction / advanced_.forwardBetaRollOffCurrentAmps
                : 1.0;
        const double reverseRollOff = std::isfinite(
            advanced_.reverseBetaRollOffCurrentAmps) &&
            advanced_.reverseBetaRollOffCurrentAmps > 0.0 &&
            reverseJunction > 0.0
                ? 1.0 + reverseJunction / advanced_.reverseBetaRollOffCurrentAmps
                : 1.0;

        const double baseWithoutCollectorLeakage =
            forwardJunction * ibForward *
                (advanced_.useGummelPoonBaseCharge ? 1.0 : forwardRollOff) +
            reverseJunction * ibReverse *
                (advanced_.useGummelPoonBaseCharge ? 1.0 : reverseRollOff) +
            forwardLeakage;
        const double base = baseWithoutCollectorLeakage + reverseLeakage;

        double collectorTransport = transport * earlyFactor;
        double collectorDerivative[3] = {
            dTransport[0] * earlyFactor,
            dTransport[1] * earlyFactor + transport * earlySlope,
            dTransport[2] * earlyFactor - transport * earlySlope
        };

        if (advanced_.useGummelPoonBaseCharge)
        {
            // Compact static Gummel-Poon base-charge term:
            // QB = Q1 * (1 + (1 + 4*(IF/IKF + IR/IKR))^NKF) / 2.
            // This reproduces the square-root default (NKF=0.5) used by
            // SPICE when the exponent is omitted. The legacy M1 profile
            // remains on its original Ebers-Moll/Early-law path.
            const double inverseVaf = std::isfinite(advanced_.forwardEarlyVoltage)
                ? 1.0 / advanced_.forwardEarlyVoltage : 0.0;
            const double inverseVar = std::isfinite(advanced_.reverseEarlyVoltage)
                ? 1.0 / advanced_.reverseEarlyVoltage : 0.0;
            const double q1Denominator = 1.0 - vbc * inverseVaf - vbe * inverseVar;
            const bool q1Unclamped = q1Denominator > 0.05;
            const double q1 = 1.0 / std::max(0.05, q1Denominator);
            const double q1DerivativeBe = q1Unclamped
                ? q1 * q1 * inverseVar : 0.0;
            const double q1DerivativeBc = q1Unclamped
                ? q1 * q1 * inverseVaf : 0.0;

            const double inverseIkf =
                std::isfinite(advanced_.forwardBetaRollOffCurrentAmps) &&
                advanced_.forwardBetaRollOffCurrentAmps > 0.0
                    ? 1.0 / advanced_.forwardBetaRollOffCurrentAmps : 0.0;
            const double inverseIkr =
                std::isfinite(advanced_.reverseBetaRollOffCurrentAmps) &&
                advanced_.reverseBetaRollOffCurrentAmps > 0.0
                    ? 1.0 / advanced_.reverseBetaRollOffCurrentAmps : 0.0;
            const double q2 = std::max(
                0.0, forwardJunction * inverseIkf +
                     reverseJunction * inverseIkr);
            const double q2Argument = std::max(1.0e-12, 1.0 + 4.0 * q2);
            const double q2Power = std::pow(
                q2Argument, advanced_.betaRollOffExponent);
            const double q2PowerDerivative =
                advanced_.betaRollOffExponent *
                std::pow(q2Argument, advanced_.betaRollOffExponent - 1.0);
            const double q2DerivativeBe = inverseIkf * forwardDerivative;
            const double q2DerivativeBc = inverseIkr * reverseDerivative;

            const double baseCharge = q1 * (1.0 + q2Power) * 0.5;
            const double baseChargeDerivativeBe =
                0.5 * (q1DerivativeBe * (1.0 + q2Power) +
                       q1 * q2PowerDerivative * 4.0 * q2DerivativeBe);
            const double baseChargeDerivativeBc =
                0.5 * (q1DerivativeBc * (1.0 + q2Power) +
                       q1 * q2PowerDerivative * 4.0 * q2DerivativeBc);

            const double safeBaseCharge = std::max(1.0e-9, baseCharge);
            const double inverseBaseCharge = 1.0 / safeBaseCharge;
            collectorTransport = transport * inverseBaseCharge;
            const double inverseBaseChargeSquared =
                inverseBaseCharge * inverseBaseCharge;
            const double baseChargeDerivative[3] = {
                baseChargeDerivativeBe + baseChargeDerivativeBc,
                -baseChargeDerivativeBc,
                -baseChargeDerivativeBe
            };
            for (int column = 0; column < 3; ++column)
            {
                collectorDerivative[column] =
                    dTransport[column] * inverseBaseCharge -
                    transport * baseChargeDerivative[column] *
                        inverseBaseChargeSquared;
            }
        }

        // The BE leakage branch conducts from base to emitter. The BC
        // leakage branch conducts from base to collector.
        const double collector = collectorTransport - reverseLeakage;
        const double emitter = -collectorTransport -
                               baseWithoutCollectorLeakage;
        result.currents = {collector, base, emitter};

        const double forwardBaseSlope = ibForward *
            (std::isfinite(advanced_.forwardBetaRollOffCurrentAmps) &&
             advanced_.forwardBetaRollOffCurrentAmps > 0.0 &&
             forwardJunction > 0.0
                ? 1.0 + 2.0 * forwardJunction /
                    advanced_.forwardBetaRollOffCurrentAmps
                : 1.0);
        const double reverseBaseSlope = ibReverse *
            (std::isfinite(advanced_.reverseBetaRollOffCurrentAmps) &&
             advanced_.reverseBetaRollOffCurrentAmps > 0.0 &&
             reverseJunction > 0.0
                ? 1.0 + 2.0 * reverseJunction /
                    advanced_.reverseBetaRollOffCurrentAmps
                : 1.0);
        const double baseDerivativeBe =
            forwardDerivative *
                (advanced_.useGummelPoonBaseCharge ? ibForward : forwardBaseSlope) +
            forwardLeakageDerivative;
        const double baseDerivativeBc =
            reverseDerivative *
                (advanced_.useGummelPoonBaseCharge ? ibReverse : reverseBaseSlope) +
            reverseLeakageDerivative;

        // Rows/columns are ordered base, collector, emitter.
        result.conductance[0][0] = baseDerivativeBe + baseDerivativeBc;
        result.conductance[0][1] = -baseDerivativeBc;
        result.conductance[0][2] = -baseDerivativeBe;

        for (int column = 0; column < 3; ++column)
        {
            const double leakageBcSlope = column == 0
                ? reverseLeakageDerivative
                : (column == 1 ? -reverseLeakageDerivative : 0.0);
            result.conductance[1][column] =
                collectorDerivative[column] - leakageBcSlope;
            result.conductance[2][column] =
                -result.conductance[0][column] - result.conductance[1][column];
        }
        const auto qbe = depletionCharge(vbe,
            advanced_.baseEmitterCapacitanceFarads,
            advanced_.baseEmitterPotentialVolts,
            advanced_.baseEmitterGradingCoefficient);
        const auto qbc = depletionCharge(vbc,
            advanced_.baseCollectorCapacitanceFarads,
            advanced_.baseCollectorPotentialVolts,
            advanced_.baseCollectorGradingCoefficient);

        const double diffusionBe = std::max(0.0, forwardJunction);
        const double diffusionBc = std::max(0.0, reverseJunction);
        const double diffusionBeSlope = forwardJunction > 0.0 ? forwardDerivative : 0.0;
        const double diffusionBcSlope = reverseJunction > 0.0 ? reverseDerivative : 0.0;
        const double cbe = qbe.second +
                           advanced_.forwardTransitTimeSeconds * diffusionBeSlope;
        const double cbc = qbc.second +
                           advanced_.reverseTransitTimeSeconds * diffusionBcSlope;
        const double qBeTotal = qbe.first +
                                advanced_.forwardTransitTimeSeconds * diffusionBe;
        const double qBcTotal = qbc.first +
                                advanced_.reverseTransitTimeSeconds * diffusionBc;

        result.terminalChargeCoulombs[0] = qBeTotal + qBcTotal;
        result.terminalChargeCoulombs[1] = -qBcTotal;
        result.terminalChargeCoulombs[2] = -qBeTotal;

        result.capacitance[0][0] = cbe + cbc;
        result.capacitance[0][1] = -cbc;
        result.capacitance[0][2] = -cbe;
        result.capacitance[1][0] = -cbc;
        result.capacitance[1][1] = cbc;
        result.capacitance[2][0] = -cbe;
        result.capacitance[2][2] = cbe;

        return result;
    }

    double thermalVoltage() const noexcept { return thermalVoltage_; }
    double forwardBeta() const noexcept { return forwardBeta_; }
    double reverseBeta() const noexcept { return reverseBeta_; }
    double saturationCurrentAmps() const noexcept { return saturationCurrentAmps_; }

    BjtNoiseEvaluation evaluateNoiseSources(
        double temperatureKelvin,
        const BjtTerminalCurrents& biasCurrents) const noexcept
    {
        BjtNoiseEvaluation noise;
        if (!std::isfinite(temperatureKelvin) || temperatureKelvin <= 0.0 ||
            !std::isfinite(biasCurrents.baseAmps) ||
            !std::isfinite(biasCurrents.collectorAmps))
            return noise;

        constexpr double boltzmann = 1.380649e-23;
        constexpr double electronCharge = 1.602176634e-19;
        const double thermalScale = 4.0 * boltzmann * temperatureKelvin;
        const double shotScale = 2.0 * electronCharge;
        noise.baseResistanceVoltageNoiseVPerSqrtHz =
            std::sqrt(thermalScale * advanced_.baseResistanceOhms);
        noise.collectorResistanceVoltageNoiseVPerSqrtHz =
            std::sqrt(thermalScale * advanced_.collectorResistanceOhms);
        noise.emitterResistanceVoltageNoiseVPerSqrtHz =
            std::sqrt(thermalScale * advanced_.emitterResistanceOhms);
        noise.baseCurrentShotNoiseAPerSqrtHz =
            std::sqrt(shotScale * std::abs(biasCurrents.baseAmps));
        noise.collectorCurrentShotNoiseAPerSqrtHz =
            std::sqrt(shotScale * std::abs(biasCurrents.collectorAmps));
        return noise;
    }

private:
    static double nonNegative(double value) noexcept
    {
        return std::isfinite(value) && value > 0.0 ? value : 0.0;
    }

    static double positiveOr(double value, double fallback) noexcept
    {
        return std::isfinite(value) && value > 0.0 ? value : fallback;
    }

    static double positiveOrInfinity(double value) noexcept
    {
        if (std::isinf(value) && value > 0.0)
            return value;
        return std::isfinite(value) && value > 0.0
            ? value : std::numeric_limits<double>::infinity();
    }

    static std::pair<double, double> depletionCharge(double voltage, double c0,
                                                     double potential, double grading) noexcept
    {
        if (c0 <= 0.0)
            return {0.0, 0.0};

        const double m = std::clamp(grading, 0.0, 0.95);
        const double forwardCoefficient = 0.5;
        const double transitionVoltage = forwardCoefficient * potential;

        const auto chargeAndCapAt = [c0, potential, m](double v)
        {
            const double argument = std::max(1.0e-6, 1.0 - v / potential);
            const double capacitance = c0 * std::pow(argument, -m);
            double charge = 0.0;
            if (std::abs(1.0 - m) < 1.0e-9)
                charge = -c0 * potential * std::log(argument);
            else
                charge = c0 * potential / (1.0 - m) *
                         (1.0 - std::pow(argument, 1.0 - m));
            return std::pair<double, double>{charge, capacitance};
        };

        if (voltage <= transitionVoltage)
            return chargeAndCapAt(voltage);

        const auto atTransition = chargeAndCapAt(transitionVoltage);
        return {
            atTransition.first + atTransition.second * (voltage - transitionVoltage),
            atTransition.second
        };
    }

    BjtAdvancedParameters advanced_{};
    double nominalSaturationCurrentAmps_ = 1.0e-14;
    double nominalForwardBeta_ = 100.0;
    double nominalReverseBeta_ = 1.0;
    double saturationCurrentAmps_ = 1.0e-14;
    double forwardBeta_ = 100.0;
    double reverseBeta_ = 1.0;
    double temperatureKelvin_ = 300.15;
    double nominalTemperatureKelvin_ = 300.15;
    double bandGapEnergyElectronVolts_ = 1.11;
    double saturationCurrentExponent_ = 3.0;
    double betaTemperatureExponent_ = 0.0;
    double alphaForward_ = 100.0 / 101.0;
    double alphaReverse_ = 0.5;
    double thermalVoltage_ = 0.02586;
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit
