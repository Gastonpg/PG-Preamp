#include "pgpreampcontroller.h"
#include "pgpreampcids.h"

#include "public.sdk/source/vst/vstparameters.h"

namespace Steinberg {

tresult PLUGIN_API PGPreampController::initialize(FUnknown* context)
{
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(new RangeParameter(
        STR16("Drive"), kPGPreampDriveId, STR16("dB"), 0.0, 24.0, 0.0));
    parameters.addParameter(new RangeParameter(
        STR16("Output"), kPGPreampOutputId, STR16("dB"), -24.0, 12.0, 0.0));

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

    if (auto* parameter = getParameterObject(kPGPreampDriveId))
        parameter->setNormalized(drive);
    if (auto* parameter = getParameterObject(kPGPreampOutputId))
        parameter->setNormalized(output);

    return kResultOk;
}

} // namespace Steinberg

