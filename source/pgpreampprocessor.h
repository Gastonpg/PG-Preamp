#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "circuit/pg_fixed_common_emitter.h"
#include "circuit/pg_fixed_magnetic_transformer.h"

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
    void applyParameter(Vst::ParamID id, Vst::ParamValue value) noexcept;
    void updateParameters(Vst::ProcessData& data) noexcept;
    static double decibelsToLinear(double decibels) noexcept;

    std::array<PGAudio::Circuit::FixedCommonEmitterStage, 2> stages_{};
    std::array<PGAudio::Circuit::FixedMagneticTransformerStage, 2> transformers_{};

    double inputGainNormalized_ = 0.4;
    double outputGainNormalized_ = 0.5;
    double inputGainTargetLinear_ = 1.0;
    double outputGainTargetLinear_ = 1.0;
    double inputGainCurrentLinear_ = 1.0;
    double outputGainCurrentLinear_ = 1.0;
    double mixSmoothingCoefficient_ = 1.0;
    double aStageMix_ = 1.0;
    double ironMix_ = 0.0;
    double bypassMix_ = 0.0;

    bool bypass_ = false;
    bool aStageEnabled_ = true;
    bool ironEnabled_ = false;
    bool stagesReady_ = false;
    bool transformersReady_ = false;
};

} // namespace Steinberg
