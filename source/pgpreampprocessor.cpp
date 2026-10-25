#include "pgpreampprocessor.h"
#include "pgpreampcids.h"

#include <algorithm>

namespace Steinberg {

PGPreampProcessor::PGPreampProcessor()
{
    setControllerClass(kPGPreampControllerUID);
}

tresult PLUGIN_API PGPreampProcessor::initialize(FUnknown* context)
{
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addAudioInput(STR16("Stereo In"), Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Vst::SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API PGPreampProcessor::process(Vst::ProcessData& data)
{
    // Version 0.1 is intentionally transparent: copy input to output.
    // This verifies the plugin's audio path before any coloration is added.
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
        return kResultOk;

    auto& input = data.inputs[0];
    auto& output = data.outputs[0];

    if (data.symbolicSampleSize != Vst::kSample32 ||
        input.channelBuffers32 == nullptr ||
        output.channelBuffers32 == nullptr)
        return kResultOk;

    const int32 channels = std::min(input.numChannels, output.numChannels);
    for (int32 channel = 0; channel < channels; ++channel)
    {
        const float* source = input.channelBuffers32[channel];
        float* destination = output.channelBuffers32[channel];
        if (source && destination && source != destination)
            std::copy_n(source, data.numSamples, destination);
    }

    output.silenceFlags = input.silenceFlags;
    return kResultOk;
}

} // namespace Steinberg
