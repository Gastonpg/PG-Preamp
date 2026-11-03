#pragma once

#include <cmath>
#include <algorithm>

namespace PGAudio::Circuit {

// First validated physical building block: a passive series-R / shunt-C stage.
// The capacitor voltage is the output.  The exact discrete-time coefficient
// is derived from the analog RC time constant, rather than an arbitrary
// smoothing constant.
class RcStage
{
public:
    void prepare(double sampleRate, double resistanceOhms, double capacitanceFarads) noexcept
    {
        sampleRate_ = std::max(1.0, sampleRate);
        resistance_ = std::max(1.0e-3, resistanceOhms);
        capacitance_ = std::max(1.0e-15, capacitanceFarads);
        const double tau = resistance_ * capacitance_;
        alpha_ = 1.0 - std::exp(-1.0 / (sampleRate_ * tau));
        alpha_ = std::clamp(alpha_, 0.0, 1.0);
        prepared_ = true;
    }

    float process(float input) noexcept
    {
        if (!prepared_)
            return input;

        state_ += alpha_ * (static_cast<double>(input) - state_);
        return static_cast<float>(state_);
    }

    void reset(double initialVoltage = 0.0) noexcept { state_ = initialVoltage; }
    double voltage() const noexcept { return state_; }
    double timeConstantSeconds() const noexcept { return resistance_ * capacitance_; }
    double cutoffHz() const noexcept
    {
        return 1.0 / (2.0 * 3.14159265358979323846 * timeConstantSeconds());
    }

private:
    double sampleRate_ = 44100.0;
    double resistance_ = 10000.0;
    double capacitance_ = 1.0e-8;
    double alpha_ = 0.0;
    double state_ = 0.0;
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit
