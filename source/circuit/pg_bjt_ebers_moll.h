#pragma once

#include <algorithm>
#include <cmath>

namespace PGAudio::Circuit {

struct BjtTerminalCurrents
{
    // Currents are positive into each transistor terminal; their sum is zero.
    double collectorAmps = 0.0;
    double baseAmps = 0.0;
    double emitterAmps = 0.0;
};

// Static Ebers-Moll NPN junction model with SPICE-style temperature scaling
// for transport saturation current and forward/reverse beta. Charge storage,
// junction capacitances, Early effect and ohmic parasitics remain future work.
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

    BjtTerminalCurrents evaluate(double baseVolts, double collectorVolts,
                                 double emitterVolts) const noexcept
    {
        if (!prepared_ || !std::isfinite(baseVolts) ||
            !std::isfinite(collectorVolts) || !std::isfinite(emitterVolts))
            return {};

        const double vbe = (baseVolts - emitterVolts) / thermalVoltage_;
        const double vbc = (baseVolts - collectorVolts) / thermalVoltage_;
        const double forwardJunction = saturationCurrentAmps_ * safeExpm1(vbe);
        const double reverseJunction = saturationCurrentAmps_ * safeExpm1(vbc);

        const double collector = forwardJunction - reverseJunction / alphaReverse_;
        const double base = forwardJunction * (1.0 / alphaForward_ - 1.0) +
                            reverseJunction * (1.0 / alphaReverse_ - 1.0);
        const double emitter = -collector - base;
        return {collector, base, emitter};
    }

    double thermalVoltage() const noexcept { return thermalVoltage_; }
    double forwardBeta() const noexcept { return forwardBeta_; }
    double reverseBeta() const noexcept { return reverseBeta_; }
    double saturationCurrentAmps() const noexcept { return saturationCurrentAmps_; }

private:
    static double safeExpm1(double value) noexcept
    {
        return std::expm1(std::clamp(value, -80.0, 40.0));
    }

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