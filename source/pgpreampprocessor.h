#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>

namespace Steinberg {

class PGPreampProcessor : public Vst::AudioEffect
{
public:
    PGPreampProcessor();

    static FUnknown* createInstance(void*)
    {
        return static_cast<Vst::IAudioProcessor*>(new PGPreampProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API process(Vst::ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;

private:
    double mSmoothingCoefficient {0.001};
    double mSampleRate {44100.0};
    double mDriveNormalized {0.0};
    double mOutputNormalized {2.0 / 3.0};
    double mLowNormalized {0.5};
    double mMidNormalized {0.5};
    double mHighNormalized {0.5};
    double mClassAEnabled {0.0};
    double mIronEnabled {0.0};
    double mTargetDriveGain {1.0};
    double mTargetOutputGain {1.0};
    double mCurrentDriveGain {1.0};
    double mCurrentOutputGain {1.0};
    double mTargetLowDb {0.0};
    double mTargetMidDb {0.0};
    double mTargetHighDb {0.0};
    double mCurrentLowDb {0.0};
    double mCurrentMidDb {0.0};
    double mCurrentHighDb {0.0};
    double mTargetClassAAmount {0.0};
    double mTargetIronAmount {0.0};
    double mCurrentClassAAmount {0.0};
    double mCurrentIronAmount {0.0};

    struct FilterState
    {
        double x1 {0.0};
        double x2 {0.0};
        double y1 {0.0};
        double y2 {0.0};
    };
    std::array<FilterState, 2> mLowFilterState {};
    std::array<FilterState, 2> mMidFilterState {};
    std::array<FilterState, 2> mHighFilterState {};
    std::array<double, 2> mPreviousSaturationInput {};
    std::array<bool, 2> mSaturationInitialized {};
    std::array<double, 2> mInputTransformerFlux {};
    std::array<double, 2> mInputTransformerMagnetization {};
    std::array<double, 2> mOutputTransformerFlux {};
    std::array<double, 2> mOutputTransformerMagnetization {};
};

} // namespace Steinberg
