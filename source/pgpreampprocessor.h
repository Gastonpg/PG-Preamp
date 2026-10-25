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
};

} // namespace Steinberg
