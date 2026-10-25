#include "pgpreampcontroller.h"
#include "pgpreampcids.h"

namespace Steinberg {

tresult PLUGIN_API PGPreampController::initialize(FUnknown* context)
{
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(
        STR16("Bypass"),
        nullptr,
        1,
        0,
        Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass,
        kPGPreampBypassId
    );

    return kResultOk;
}

} // namespace Steinberg
