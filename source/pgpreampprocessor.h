#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

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
    double mDriveNormalized {0.0};
    double mOutputNormalized {2.0 / 3.0};
    double mTargetDriveGain {1.0};
    double mTargetOutputGain {1.0};
    double mCurrentDriveGain {1.0};
    double mCurrentOutputGain {1.0};
};

} // namespace Steinberg

