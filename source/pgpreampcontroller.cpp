#include "pgpreampcontroller.h"
#include "pgpreampcids.h"

namespace Steinberg {

tresult PLUGIN_API PGPreampController::initialize(FUnknown* context)
{
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(
        STR16("Bypass"), STR16(""), 1, 0.0,
        Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass,
        kPGPreampBypassId);

    // Normalized 0.4 maps to 0 dB in the -12 .. +18 dB input range.
    parameters.addParameter(
        STR16("Input"), STR16("dB"), 0, 0.4,
        Vst::ParameterInfo::kCanAutomate,
        kPGPreampInputGainId);

    // Normalized 0.5 maps to 0 dB in the -12 .. +12 dB output range.
    parameters.addParameter(
        STR16("Output"), STR16("dB"), 0, 0.5,
        Vst::ParameterInfo::kCanAutomate,
        kPGPreampOutputGainId);

    return kResultOk;
}

} // namespace Steinberg
