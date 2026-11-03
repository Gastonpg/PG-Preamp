#pragma once

#include <cmath>

namespace PGAudio::Circuit {

// Linear inductor with series winding resistance. Magnetic saturation,
// hysteresis and frequency-dependent core loss are separate future models;
// this class represents L and copper winding loss only.
class Inductor
{
public:
    void prepare(double sampleRate, double inductanceHenries,
                 double windingResistanceOhms = 0.0) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
        inductanceHenries_ = std::isfinite(inductanceHenries) && inductanceHenries > 0.0
                                 ? inductanceHenries : 1.0e-9;
        windingResistanceOhms_ =
            std::isfinite(windingResistanceOhms) && windingResistanceOhms > 0.0
                ? windingResistanceOhms : 0.0;

        const double exponent = windingResistanceOhms_ /
                                (sampleRate_ * inductanceHenries_);
        decay_ = windingResistanceOhms_ > 0.0 ? std::exp(-exponent) : 1.0;
        oneMinusDecay_ = windingResistanceOhms_ > 0.0 ? -std::expm1(-exponent) : 0.0;
        prepared_ = true;
    }

    // Voltage is held constant during one sample; returns updated winding current.
    float processVoltage(float voltageVolts) noexcept
    {
        if (!prepared_ || !std::isfinite(voltageVolts))
            return static_cast<float>(currentAmps_);

        if (windingResistanceOhms_ > 0.0)
        {
            currentAmps_ = currentAmps_ * decay_ +
                           (voltageVolts / windingResistanceOhms_) * oneMinusDecay_;
        }
        else
        {
            currentAmps_ += voltageVolts / (sampleRate_ * inductanceHenries_);
        }
        return static_cast<float>(currentAmps_);
    }

    void reset(double initialCurrentAmps = 0.0) noexcept
    {
        currentAmps_ = std::isfinite(initialCurrentAmps) ? initialCurrentAmps : 0.0;
    }

    double currentAmps() const noexcept { return currentAmps_; }
    double inductanceHenries() const noexcept { return inductanceHenries_; }
    double windingResistanceOhms() const noexcept { return windingResistanceOhms_; }
    double electricalTimeConstantSeconds() const noexcept
    {
        return windingResistanceOhms_ > 0.0
            ? inductanceHenries_ / windingResistanceOhms_
            : INFINITY;
    }

private:
    double sampleRate_ = 44100.0;
    double inductanceHenries_ = 1.0e-3;
    double windingResistanceOhms_ = 0.0;
    double decay_ = 1.0;
    double oneMinusDecay_ = 0.0;
    double currentAmps_ = 0.0;
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit