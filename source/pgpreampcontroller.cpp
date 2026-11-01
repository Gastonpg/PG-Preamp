#include "pgpreampcontroller.h"
#include "pgpreampcids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <cstring>

namespace Steinberg {

tresult PLUGIN_API PGPreampController::initialize(FUnknown* context)
{
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(new Vst::RangeParameter(
        STR16("Drive"), kPGPreampDriveId, STR16("dB"), 0.0, 24.0, 0.0));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("Output"), kPGPreampOutputId, STR16("dB"), -24.0, 12.0, 0.0));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("Low"), kPGPreampLowId, STR16("dB"), -16.0, 16.0, 0.0));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("Mid"), kPGPreampMidId, STR16("dB"), -18.0, 18.0, 0.0));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("High"), kPGPreampHighId, STR16("dB"), -16.0, 16.0, 0.0));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("Class A Stage"), kPGPreampClassAId, nullptr, 0.0, 1.0, 0.0, 1));
    parameters.addParameter(new Vst::RangeParameter(
        STR16("Iron Transformers"), kPGPreampIronId, nullptr, 0.0, 1.0, 0.0, 1));

    return kResultOk;
}

tresult PLUGIN_API PGPreampController::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    float drive = 0.f;
    float output = 2.f / 3.f;
    if (!streamer.readFloat(drive) || !streamer.readFloat(output))
        return kResultFalse;

    float low = 0.5f;
    float mid = 0.5f;
    float high = 0.5f;
    if (!streamer.readFloat(low) || !streamer.readFloat(mid) || !streamer.readFloat(high))
        low = mid = high = 0.5f;

    float classA = 0.f;
    float iron = 0.f;
    if (!streamer.readFloat(classA) || !streamer.readFloat(iron))
        classA = iron = 0.f;

    if (auto* parameter = getParameterObject(kPGPreampDriveId))
        parameter->setNormalized(drive);
    if (auto* parameter = getParameterObject(kPGPreampOutputId))
        parameter->setNormalized(output);
    if (auto* parameter = getParameterObject(kPGPreampLowId))
        parameter->setNormalized(low);
    if (auto* parameter = getParameterObject(kPGPreampMidId))
        parameter->setNormalized(mid);
    if (auto* parameter = getParameterObject(kPGPreampHighId))
        parameter->setNormalized(high);
    if (auto* parameter = getParameterObject(kPGPreampClassAId))
        parameter->setNormalized(classA >= 0.5f ? 1.0 : 0.0);
    if (auto* parameter = getParameterObject(kPGPreampIronId))
        parameter->setNormalized(iron >= 0.5f ? 1.0 : 0.0);

    return kResultOk;
}

IPlugView* PLUGIN_API PGPreampController::createView(FIDString name)
{
    if (name && std::strcmp(name, Vst::ViewType::kEditor) == 0)
        return new VSTGUI::VST3Editor(this, "view", "pgpreamp.uidesc");
    return nullptr;
}

} // namespace Steinberg
