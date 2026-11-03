#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace PGAudio::Circuit {

// Discrete symmetric Preisach-style core model. Each magnetic domain is a
// relay hysteron with an upper and lower switching field; its state remembers
// the last direction of magnetization. This is a compact engineering model
// for hysteresis-loop experiments, not a calibrated material model by itself.
template <std::size_t MaxDomains = 256>
class PreisachCore
{
public:
    static constexpr double vacuumPermeability = 1.25663706212e-6;

    struct Sample
    {
        double fieldAmpsPerMeter = 0.0;
        double fluxDensityTesla = 0.0;
        double fluxWebers = 0.0;
        double magnetization = 0.0;
        double hysteresisWorkDensityJoulesPerM3 = 0.0;
        double eddyLossDensityWattsPerM3 = 0.0;
        double fluxDensityRateTeslaPerSecond = 0.0;
    };

    bool prepare(double saturationMagnetizationTesla,
                 double coreAreaSquareMeters,
                 double magneticPathLengthMeters,
                 double eddyLossCoefficient = 0.0) noexcept
    {
        if (!std::isfinite(saturationMagnetizationTesla) ||
            saturationMagnetizationTesla <= 0.0 ||
            !std::isfinite(coreAreaSquareMeters) || coreAreaSquareMeters <= 0.0 ||
            !std::isfinite(magneticPathLengthMeters) || magneticPathLengthMeters <= 0.0 ||
            !std::isfinite(eddyLossCoefficient) || eddyLossCoefficient < 0.0)
            return false;

        saturationFluxDensityTesla_ = saturationMagnetizationTesla;
        coreAreaSquareMeters_ = coreAreaSquareMeters;
        magneticPathLengthMeters_ = magneticPathLengthMeters;
        eddyLossCoefficient_ = eddyLossCoefficient;
        prepared_ = true;
        reset();
        return true;
    }

    // Configure a symmetric coercivity distribution. The first domain has
    // the lowest coercive field and the last has the highest. Using many
    // domains reduces flux quantization compared with a single relay.
    std::size_t configureSymmetricDomains(double minimumCoerciveField,
                                          double maximumCoerciveField,
                                          std::size_t count) noexcept
    {
        if (count == 0 || count > MaxDomains ||
            !std::isfinite(minimumCoerciveField) ||
            !std::isfinite(maximumCoerciveField) ||
            minimumCoerciveField <= 0.0 ||
            maximumCoerciveField < minimumCoerciveField)
            return 0;

        domainCount_ = count;
        weightSum_ = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double position = count == 1
                ? 0.0
                : static_cast<double>(i) / static_cast<double>(count - 1);
            const double coercivity = minimumCoerciveField +
                position * (maximumCoerciveField - minimumCoerciveField);
            domains_[i] = {-coercivity, coercivity, 1.0, 0};
            weightSum_ += 1.0;
        }
        reset();
        return domainCount_;
    }

    bool setDomain(std::size_t index, double lowerSwitchField,
                   double upperSwitchField, double weight = 1.0) noexcept
    {
        if (index >= domainCount_ ||
            !std::isfinite(lowerSwitchField) ||
            !std::isfinite(upperSwitchField) ||
            !std::isfinite(weight) || weight <= 0.0 ||
            lowerSwitchField >= upperSwitchField)
            return false;

        weightSum_ -= domains_[index].weight;
        domains_[index] = {lowerSwitchField, upperSwitchField, weight, 0};
        weightSum_ += weight;
        return true;
    }

    Sample advance(double fieldAmpsPerMeter, double timeStepSeconds) noexcept
    {
        if (!prepared_ || domainCount_ == 0 ||
            !std::isfinite(fieldAmpsPerMeter) ||
            !std::isfinite(timeStepSeconds) || timeStepSeconds <= 0.0)
            return lastSample_;

        for (std::size_t i = 0; i < domainCount_; ++i)
        {
            auto& domain = domains_[i];
            if (fieldAmpsPerMeter >= domain.upperSwitchField)
                domain.state = 1;
            else if (fieldAmpsPerMeter <= domain.lowerSwitchField)
                domain.state = -1;
        }

        double weightedState = 0.0;
        for (std::size_t i = 0; i < domainCount_; ++i)
            weightedState += domains_[i].weight * static_cast<double>(domains_[i].state);

        const double normalizedMagnetization = weightSum_ > 0.0
            ? weightedState / weightSum_ : 0.0;

        // B = mu0*H + domain magnetization. The relay ensemble approaches
        // +/- saturation while the vacuum term remains physically present.
        const double fluxDensity = vacuumPermeability * fieldAmpsPerMeter +
                                   saturationFluxDensityTesla_ * normalizedMagnetization;
        const double deltaFluxDensity = fluxDensity - previousFluxDensity_;
        const double rate = deltaFluxDensity / timeStepSeconds;

        const double incrementalWork =
            0.5 * (previousFieldAmpsPerMeter_ + fieldAmpsPerMeter) *
            deltaFluxDensity;
        hysteresisWorkDensity_ += incrementalWork;

        const double magnetization = saturationFluxDensityTesla_ *
                                     normalizedMagnetization / vacuumPermeability;
        lastSample_ = {
            fieldAmpsPerMeter,
            fluxDensity,
            fluxDensity * coreAreaSquareMeters_,
            magnetization,
            hysteresisWorkDensity_,
            eddyLossCoefficient_ * rate * rate,
            rate
        };

        previousFieldAmpsPerMeter_ = fieldAmpsPerMeter;
        previousFluxDensity_ = fluxDensity;
        return lastSample_;
    }

    void reset() noexcept
    {
        for (std::size_t i = 0; i < domainCount_; ++i)
            domains_[i].state = 0;
        previousFieldAmpsPerMeter_ = 0.0;
        previousFluxDensity_ = 0.0;
        hysteresisWorkDensity_ = 0.0;
        lastSample_ = {};
    }

    void resetLossAccumulator() noexcept
    {
        hysteresisWorkDensity_ = 0.0;
        lastSample_.hysteresisWorkDensityJoulesPerM3 = 0.0;
    }

    double coreAreaSquareMeters() const noexcept { return coreAreaSquareMeters_; }
    double magneticPathLengthMeters() const noexcept { return magneticPathLengthMeters_; }
    double saturationFluxDensityTesla() const noexcept { return saturationFluxDensityTesla_; }
    double hysteresisWorkDensity() const noexcept { return hysteresisWorkDensity_; }
    std::size_t domainCount() const noexcept { return domainCount_; }
    const Sample& lastSample() const noexcept { return lastSample_; }

private:
    struct Domain
    {
        double lowerSwitchField = -1.0;
        double upperSwitchField = 1.0;
        double weight = 1.0;
        std::int8_t state = 0;
    };

    std::array<Domain, MaxDomains> domains_{};
    std::size_t domainCount_ = 0;
    double weightSum_ = 0.0;
    double saturationFluxDensityTesla_ = 1.0;
    double coreAreaSquareMeters_ = 1.0e-4;
    double magneticPathLengthMeters_ = 0.1;
    double eddyLossCoefficient_ = 0.0;
    double previousFieldAmpsPerMeter_ = 0.0;
    double previousFluxDensity_ = 0.0;
    double hysteresisWorkDensity_ = 0.0;
    Sample lastSample_{};
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit