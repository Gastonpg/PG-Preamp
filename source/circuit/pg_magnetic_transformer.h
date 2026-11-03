#pragma once

#include "pg_preisach_core.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace PGAudio::Circuit {

// Current-driven nonlinear transformer constitutive model. Winding currents
// are the input variables; induced voltages are calculated from Faraday's law
// and the history-dependent core flux. This is a component-level two-port
// model for offline validation and later fixed-topology circuit integration.
template <std::size_t MaxDomains = 256>
class MagneticTransformer
{
public:
    struct Sample
    {
        double primaryVoltage = 0.0;
        double secondaryVoltage = 0.0;
        double primaryFluxLinkageWebers = 0.0;
        double secondaryFluxLinkageWebers = 0.0;
        double magnetizingFieldAmpsPerMeter = 0.0;
        double coreFluxDensityTesla = 0.0;
        double coreFluxWebers = 0.0;
        double coreHysteresisWorkDensityJoulesPerM3 = 0.0;
        double coreEddyLossDensityWattsPerM3 = 0.0;
        double coreLossPowerEstimateWatts = 0.0;
    };

    bool prepare(double saturationFluxDensityTesla,
                 double coreAreaSquareMeters,
                 double magneticPathLengthMeters,
                 double primaryTurns,
                 double secondaryTurns,
                 double primaryWindingResistanceOhms,
                 double secondaryWindingResistanceOhms,
                 double primaryLeakageInductanceHenries,
                 double secondaryLeakageInductanceHenries,
                 double eddyLossCoefficient = 0.0) noexcept
    {
        if (!std::isfinite(primaryTurns) || primaryTurns <= 0.0 ||
            !std::isfinite(secondaryTurns) || secondaryTurns <= 0.0 ||
            !validNonNegative(primaryWindingResistanceOhms) ||
            !validNonNegative(secondaryWindingResistanceOhms) ||
            !validNonNegative(primaryLeakageInductanceHenries) ||
            !validNonNegative(secondaryLeakageInductanceHenries) ||
            !core_.prepare(saturationFluxDensityTesla, coreAreaSquareMeters,
                           magneticPathLengthMeters, eddyLossCoefficient))
            return false;

        primaryTurns_ = primaryTurns;
        secondaryTurns_ = secondaryTurns;
        primaryWindingResistanceOhms_ = primaryWindingResistanceOhms;
        secondaryWindingResistanceOhms_ = secondaryWindingResistanceOhms;
        primaryLeakageInductanceHenries_ = primaryLeakageInductanceHenries;
        secondaryLeakageInductanceHenries_ = secondaryLeakageInductanceHenries;
        coreVolumeCubicMeters_ = coreAreaSquareMeters * magneticPathLengthMeters;
        prepared_ = true;
        reset();
        return true;
    }

    std::size_t configureCoreDomains(double minimumCoerciveField,
                                     double maximumCoerciveField,
                                     std::size_t count) noexcept
    {
        return core_.configureSymmetricDomains(
            minimumCoerciveField, maximumCoerciveField, count);
    }

    Sample processCurrents(double primaryCurrentAmps,
                           double secondaryCurrentAmps,
                           double timeStepSeconds) noexcept
    {
        if (!prepared_ || !std::isfinite(primaryCurrentAmps) ||
            !std::isfinite(secondaryCurrentAmps) ||
            !std::isfinite(timeStepSeconds) || timeStepSeconds <= 0.0)
            return lastSample_;

        const double previousHysteresisWork = core_.hysteresisWorkDensity();
        const double field = (primaryTurns_ * primaryCurrentAmps +
                              secondaryTurns_ * secondaryCurrentAmps) /
                             core_.magneticPathLengthMeters();

        const auto coreSample = core_.advance(field, timeStepSeconds);
        const double fluxRate = coreSample.fluxDensityRateTeslaPerSecond;
        const double primaryCurrentRate =
            (primaryCurrentAmps - previousPrimaryCurrentAmps_) / timeStepSeconds;
        const double secondaryCurrentRate =
            (secondaryCurrentAmps - previousSecondaryCurrentAmps_) / timeStepSeconds;

        const double coreFlux = coreSample.fluxWebers;
        const double primaryFluxLinkage = primaryTurns_ * coreFlux;
        const double secondaryFluxLinkage = secondaryTurns_ * coreFlux;

        const double primaryVoltage =
            primaryWindingResistanceOhms_ * primaryCurrentAmps +
            primaryLeakageInductanceHenries_ * primaryCurrentRate +
            primaryTurns_ * core_.coreAreaSquareMeters() * fluxRate;

        const double secondaryVoltage =
            secondaryWindingResistanceOhms_ * secondaryCurrentAmps +
            secondaryLeakageInductanceHenries_ * secondaryCurrentRate +
            secondaryTurns_ * core_.coreAreaSquareMeters() * fluxRate;

        const double hysteresisPowerDensity =
            std::max(0.0, (core_.hysteresisWorkDensity() -
                           previousHysteresisWork) / timeStepSeconds);
        const double eddyPowerDensity = coreSample.eddyLossDensityWattsPerM3;

        lastSample_ = {
            primaryVoltage,
            secondaryVoltage,
            primaryFluxLinkage,
            secondaryFluxLinkage,
            field,
            coreSample.fluxDensityTesla,
            coreFlux,
            core_.hysteresisWorkDensity(),
            eddyPowerDensity,
            (hysteresisPowerDensity + eddyPowerDensity) * coreVolumeCubicMeters_
        };

        previousPrimaryCurrentAmps_ = primaryCurrentAmps;
        previousSecondaryCurrentAmps_ = secondaryCurrentAmps;
        return lastSample_;
    }

    void reset() noexcept
    {
        core_.reset();
        previousPrimaryCurrentAmps_ = 0.0;
        previousSecondaryCurrentAmps_ = 0.0;
        lastSample_ = {};
    }

    double primaryTurns() const noexcept { return primaryTurns_; }
    double secondaryTurns() const noexcept { return secondaryTurns_; }
    double turnsRatio() const noexcept { return secondaryTurns_ / primaryTurns_; }
    const typename PreisachCore<MaxDomains>::Sample& coreState() const noexcept
    {
        return core_.lastSample();
    }
    const Sample& lastSample() const noexcept { return lastSample_; }

private:
    static bool validNonNegative(double value) noexcept
    {
        return std::isfinite(value) && value >= 0.0;
    }

    PreisachCore<MaxDomains> core_;
    double primaryTurns_ = 1.0;
    double secondaryTurns_ = 1.0;
    double primaryWindingResistanceOhms_ = 0.0;
    double secondaryWindingResistanceOhms_ = 0.0;
    double primaryLeakageInductanceHenries_ = 0.0;
    double secondaryLeakageInductanceHenries_ = 0.0;
    double coreVolumeCubicMeters_ = 0.0;
    double previousPrimaryCurrentAmps_ = 0.0;
    double previousSecondaryCurrentAmps_ = 0.0;
    Sample lastSample_{};
    bool prepared_ = false;
};

} // namespace PGAudio::Circuit
