#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Steinberg {

class PGPreampController : public Vst::EditControllerEx1
{
public:
    static FUnknown* createInstance(void*)
    {
        return static_cast<Vst::IEditController*>(new PGPreampController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
};

} // namespace Steinberg
