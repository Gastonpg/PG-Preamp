#include "pgpreampcontroller.h"
#include "pgpreampcids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <cstring>

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

    // Keep the normalized mappings used by the circuit processor:
    // Input 0.4 = 0 dB in the -12..+18 dB range.
    auto* input = new Vst::RangeParameter(
        STR16("Input"), kPGPreampInputGainId, STR16("dB"), -12.0, 18.0, 0.0);
    input->setPrecision(1);
    parameters.addParameter(input);

    // Output 0.5 = 0 dB in the -12..+12 dB range.
    auto* output = new Vst::RangeParameter(
        STR16("Output"), kPGPreampOutputGainId, STR16("dB"), -12.0, 12.0, 0.0);
    output->setPrecision(1);
    parameters.addParameter(output);

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

IPlugView* PLUGIN_API PGPreampController::createView(FIDString name)
{
    if (name != nullptr && std::strcmp(name, Vst::ViewType::kEditor) == 0)
        return new VSTGUI::VST3Editor(this, "view", "pgpreamp.uidesc");
    return nullptr;
}

} // namespace Steinberg
