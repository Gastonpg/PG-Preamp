#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "circuit/pg_fixed_common_emitter.h"

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
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API process(Vst::ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;

private:
    void updateParameters(Vst::ProcessData& data) noexcept;
    static double decibelsToLinear(double decibels) noexcept;

    std::array<PGAudio::Circuit::FixedCommonEmitterStage, 2> stages_{};
    double inputGainNormalized_ = 0.4;
    double outputGainNormalized_ = 0.5;
    bool bypass_ = false;
    bool stagesReady_ = false;
};

} // namespace Steinberg
