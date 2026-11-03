#include "pgpreampcontroller.h"
#include "pgpreampcids.h"
#include "base/source/fstreamer.h"

#include <algorithm>

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

tresult PLUGIN_API PGPreampController::setComponentState(IBStream* state)
{
    if (state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    float bypassValue = 0.0f;
    float inputValue = 0.4f;
    float outputValue = 0.5f;
    if (!streamer.readFloat(bypassValue) ||
        !streamer.readFloat(inputValue) ||
        !streamer.readFloat(outputValue))
        return kResultFalse;

    setParamNormalized(kPGPreampBypassId, bypassValue >= 0.5f ? 1.0 : 0.0);
    setParamNormalized(kPGPreampInputGainId,
                       std::clamp(static_cast<double>(inputValue), 0.0, 1.0));
    setParamNormalized(kPGPreampOutputGainId,
                       std::clamp(static_cast<double>(outputValue), 0.0, 1.0));
    return kResultOk;
}

} // namespace Steinberg
