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

// Static Ebers-Moll NPN junction model. This is a nonlinear device primitive,
// not yet a complete audio transistor model: charge storage, junction
// capacitances, Early effect and ohmic parasitics are future extensions.
class EbersMollNpn
{
public:
    void prepare(double saturationCurrentAmps = 1.0e-14,
                 double forwardBeta = 100.0,
                 double reverseBeta = 1.0,
                 double temperatureKelvin = 300.15) noexcept
    {
        saturationCurrentAmps_ =
            std::isfinite(saturationCurrentAmps) && saturationCurrentAmps > 0.0
                ? saturationCurrentAmps : 1.0e-14;
        forwardBeta_ = std::isfinite(forwardBeta) && forwardBeta > 0.0
                           ? forwardBeta : 100.0;
        reverseBeta_ = std::isfinite(reverseBeta) && reverseBeta > 0.0
                           ? reverseBeta : 1.0;
        temperatureKelvin_ = std::isfinite(temperatureKelvin) && temperatureKelvin > 0.0
                                 ? temperatureKelvin : 300.15;

        alphaForward_ = forwardBeta_ / (forwardBeta_ + 1.0);
        alphaReverse_ = reverseBeta_ / (reverseBeta_ + 1.0);
        constexpr double boltzmann = 1.380649e-23;
        constexpr double electronCharge = 1.602176634e-19;
        thermalVoltage_ = boltzmann * temperatureKelvin_ / electronCharge;
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

    double saturationCurrentAmps_ = 1.0e-14;
    double forwardBeta_ = 100.0;
    double reverseBeta_ = 1.0;
    double temperatureKelvin_ = 300.15;
    double alphaForward_ = 100.0 / 101.0;
    double alphaReverse_ = 0.5;
    double thermalVoltage_ = 0.02586;
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit