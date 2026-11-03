#pragma once

#include <cmath>
#include <limits>

namespace PGAudio::Circuit {

// Current-driven capacitor with series ESR and parallel leakage resistance.
// Current is held constant during each sample, allowing an exact exponential
// leakage update instead of an arbitrary smoothing coefficient.
class Capacitor
{
public:
    void prepare(double sampleRate, double capacitanceFarads,
                 double esrOhms = 0.0,
                 double leakageResistanceOhms = std::numeric_limits<double>::infinity()) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
        capacitance_ = std::isfinite(capacitanceFarads) && capacitanceFarads > 0.0
                           ? capacitanceFarads : 1.0e-12;
        esrOhms_ = std::isfinite(esrOhms) && esrOhms > 0.0 ? esrOhms : 0.0;
        leakageResistanceOhms_ =
            (std::isinf(leakageResistanceOhms) ||
             (std::isfinite(leakageResistanceOhms) && leakageResistanceOhms > 0.0))
                ? leakageResistanceOhms
                : std::numeric_limits<double>::infinity();

        leakageDecay_ = std::isfinite(leakageResistanceOhms_)
            ? std::exp(-1.0 / (sampleRate_ * leakageResistanceOhms_ * capacitance_))
            : 1.0;
        prepared_ = true;
    }

    // Returns terminal voltage = dielectric voltage + ESR * current.
    float processCurrent(float currentAmps) noexcept
    {
        if (!prepared_ || !std::isfinite(currentAmps))
            return static_cast<float>(terminalVoltage_);

        if (std::isfinite(leakageResistanceOhms_))
        {
            const double equilibriumVoltage = currentAmps * leakageResistanceOhms_;
            dielectricVoltage_ = equilibriumVoltage +
                                 (dielectricVoltage_ - equilibriumVoltage) * leakageDecay_;
        }
        else
        {
            dielectricVoltage_ += currentAmps / (sampleRate_ * capacitance_);
        }

        terminalVoltage_ = dielectricVoltage_ + currentAmps * esrOhms_;
        return static_cast<float>(terminalVoltage_);
    }

    void reset(double initialDielectricVoltage = 0.0) noexcept
    {
        dielectricVoltage_ = std::isfinite(initialDielectricVoltage) ? initialDielectricVoltage : 0.0;
        terminalVoltage_ = dielectricVoltage_;
    }

    double dielectricVoltage() const noexcept { return dielectricVoltage_; }
    double terminalVoltage() const noexcept { return terminalVoltage_; }
    double capacitanceFarads() const noexcept { return capacitance_; }
    double esrOhms() const noexcept { return esrOhms_; }
    double leakageResistanceOhms() const noexcept { return leakageResistanceOhms_; }
    double leakageTimeConstantSeconds() const noexcept
    {
        return leakageResistanceOhms_ * capacitance_;
    }

private:
    double sampleRate_ = 44100.0;
    double capacitance_ = 1.0e-8;
    double esrOhms_ = 0.0;
    double leakageResistanceOhms_ = std::numeric_limits<double>::infinity();
    double leakageDecay_ = 1.0;
    double dielectricVoltage_ = 0.0;
    double terminalVoltage_ = 0.0;
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit