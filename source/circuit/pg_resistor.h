#pragma once

#include <cmath>
#include <cstdint>

namespace PGAudio::Circuit {

// Linear resistor with optional temperature coefficients and Johnson-Nyquist
// equivalent series noise. Noise is explicit: no hiss is added implicitly.
class Resistor
{
public:
    explicit Resistor(double resistanceOhms = 10000.0) noexcept
    {
        setResistanceOhms(resistanceOhms);
    }

    void setResistanceOhms(double value) noexcept
    {
        if (std::isfinite(value) && value > 0.0)
            resistanceOhms_ = value;
    }

    void setTemperatureCoefficients(double firstOrderPerKelvin,
                                    double secondOrderPerKelvinSquared = 0.0) noexcept
    {
        if (std::isfinite(firstOrderPerKelvin))
            tc1_ = firstOrderPerKelvin;
        if (std::isfinite(secondOrderPerKelvinSquared))
            tc2_ = secondOrderPerKelvinSquared;
    }

    void setNominalTemperatureKelvin(double value) noexcept
    {
        if (std::isfinite(value) && value > 0.0)
            nominalTemperatureKelvin_ = value;
    }

    double resistanceOhms() const noexcept { return resistanceOhms_; }
    double nominalTemperatureKelvin() const noexcept { return nominalTemperatureKelvin_; }
    double temperatureCoefficient1() const noexcept { return tc1_; }
    double temperatureCoefficient2() const noexcept { return tc2_; }

    // SPICE-style first/second-order resistance temperature coefficients.
    double resistanceAtTemperature(double temperatureKelvin) const noexcept
    {
        if (!std::isfinite(temperatureKelvin) || temperatureKelvin <= 0.0)
            return resistanceOhms_;

        const double delta = temperatureKelvin - nominalTemperatureKelvin_;
        const double factor = 1.0 + tc1_ * delta + tc2_ * delta * delta;
        const double result = resistanceOhms_ * factor;
        return std::isfinite(result) && result > 0.0 ? result : resistanceOhms_;
    }

    // Ohm's law: a resistor is algebraic and has no internal dynamic state.
    double currentForVoltage(double voltage,
                             double temperatureKelvin = 293.15) const noexcept
    {
        if (!std::isfinite(voltage))
            return 0.0;
        return voltage / resistanceAtTemperature(temperatureKelvin);
    }

    double voltageForCurrent(double current,
                             double temperatureKelvin = 293.15) const noexcept
    {
        if (!std::isfinite(current))
            return 0.0;
        return current * resistanceAtTemperature(temperatureKelvin);
    }

    double powerForVoltage(double voltage,
                           double temperatureKelvin = 293.15) const noexcept
    {
        if (!std::isfinite(voltage))
            return 0.0;
        return voltage * currentForVoltage(voltage, temperatureKelvin);
    }

    // Equivalent input-voltage noise density, in V/sqrt(Hz).
    double thermalNoiseDensity(double temperatureKelvin = 293.15) const noexcept
    {
        if (!std::isfinite(temperatureKelvin) || temperatureKelvin <= 0.0)
            return 0.0;
        constexpr double boltzmann = 1.380649e-23;
        return std::sqrt(4.0 * boltzmann * temperatureKelvin *
                         resistanceAtTemperature(temperatureKelvin));
    }

    // Integrated RMS noise over a one-sided noise bandwidth.
    double thermalNoiseRms(double bandwidthHz,
                           double temperatureKelvin = 293.15) const noexcept
    {
        if (!std::isfinite(bandwidthHz) || bandwidthHz <= 0.0)
            return 0.0;
        return thermalNoiseDensity(temperatureKelvin) * std::sqrt(bandwidthHz);
    }

private:
    double resistanceOhms_ = 10000.0;
    double nominalTemperatureKelvin_ = 293.15;
    double tc1_ = 0.0;
    double tc2_ = 0.0;
};

// Deterministic low-cost Gaussian approximation (sum of 12 uniform values).
// The source is opt-in and intended for component experiments, not enabled
// automatically inside the plugin signal path.
class ResistorThermalNoise
{
public:
    void prepare(double sampleRate, double resistanceOhms,
                 double temperatureKelvin = 293.15,
                 std::uint64_t seed = 0x5047417564696FULL) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
        {
            noiseRmsVolts_ = 0.0;
            return;
        }

        Resistor resistor(resistanceOhms);
        noiseRmsVolts_ = resistor.thermalNoiseRms(sampleRate * 0.5, temperatureKelvin);
        state_ = seed == 0 ? 0x9E3779B97F4A7C15ULL : seed;
    }

    float process() noexcept
    {
        double gaussianApproximation = 0.0;
        for (int i = 0; i < 12; ++i)
            gaussianApproximation += uniformMinusHalfToHalf();
        return static_cast<float>(gaussianApproximation * noiseRmsVolts_);
    }

    double noiseRmsVolts() const noexcept { return noiseRmsVolts_; }

private:
    double uniformMinusHalfToHalf() noexcept
    {
        // xorshift64*: compact deterministic generator with local state.
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        const std::uint64_t bits = state_ * 0x2545F4914F6CDD1DULL;
        const double unit = static_cast<double>(bits >> 11) *
                            (1.0 / 9007199254740992.0);
        return unit - 0.5;
    }

    std::uint64_t state_ = 0x9E3779B97F4A7C15ULL;
    double noiseRmsVolts_ = 0.0;
};

} // namespace PGAudio::Circuit