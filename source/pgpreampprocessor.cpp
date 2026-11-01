#include "pgpreampprocessor.h"
#include "pgpreampcids.h"

#include "public.sdk/source/common/ibstreamer.h"

#include <algorithm>
#include <cmath>

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

tresult PLUGIN_API PGPreampProcessor::setupProcessing(Vst::ProcessSetup& setup)
{
    const tresult result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk)
        return result;

    if (setup.sampleRate > 0.0)
        mSmoothingCoefficient = 1.0 - std::exp(-1.0 / (0.02 * setup.sampleRate));
    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);
    return streamer.writeFloat(static_cast<float>(mDriveNormalized)) &&
           streamer.writeFloat(static_cast<float>(mOutputNormalized))
        ? kResultOk : kResultFalse;
}

tresult PLUGIN_API PGPreampProcessor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    float drive = 0.f;
    float output = 2.f / 3.f;
    if (!streamer.readFloat(drive) || !streamer.readFloat(output))
        return kResultFalse;

    mDriveNormalized = std::clamp(static_cast<double>(drive), 0.0, 1.0);
    mOutputNormalized = std::clamp(static_cast<double>(output), 0.0, 1.0);
    mTargetDriveGain = std::pow(10.0, (24.0 * mDriveNormalized) / 20.0);
    const double outputDb = -24.0 + 36.0 * mOutputNormalized;
    mTargetOutputGain = std::pow(10.0, outputDb / 20.0);
    mCurrentDriveGain = mTargetDriveGain;
    mCurrentOutputGain = mTargetOutputGain;
    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::process(Vst::ProcessData& data)
{
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
        return kResultOk;

    auto& input = data.inputs[0];
    auto& output = data.outputs[0];

    if (data.symbolicSampleSize != Vst::kSample32 ||
        input.channelBuffers32 == nullptr ||
        output.channelBuffers32 == nullptr)
        return kResultOk;

    if (data.inputParameterChanges)
    {
        const int32 queueCount = data.inputParameterChanges->getParameterCount();
        for (int32 index = 0; index < queueCount; ++index)
        {
            auto* queue = data.inputParameterChanges->getParameterData(index);
            if (!queue || queue->getPointCount() <= 0)
                continue;

            int32 sampleOffset = 0;
            Vst::ParamValue value = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != kResultOk)
                continue;

            const double normalized = std::clamp(value, 0.0, 1.0);
            if (queue->getParameterId() == kPGPreampDriveId)
            {
                mDriveNormalized = normalized;
                mTargetDriveGain = std::pow(10.0, (24.0 * normalized) / 20.0);
            }
            else if (queue->getParameterId() == kPGPreampOutputId)
            {
                mOutputNormalized = normalized;
                const double outputDb = -24.0 + 36.0 * normalized;
                mTargetOutputGain = std::pow(10.0, outputDb / 20.0);
            }
        }
    }

    const int32 channels = std::min(input.numChannels, output.numChannels);
    for (int32 sample = 0; sample < data.numSamples; ++sample)
    {
        mCurrentDriveGain += mSmoothingCoefficient * (mTargetDriveGain - mCurrentDriveGain);
        mCurrentOutputGain += mSmoothingCoefficient * (mTargetOutputGain - mCurrentOutputGain);

        const double bias = 0.18;
        const double biasOutput = std::tanh(bias);
        const double smallSignalSlope = 1.0 - biasOutput * biasOutput;
        for (int32 channel = 0; channel < channels; ++channel)
        {
            const float* source = input.channelBuffers32[channel];
            float* destination = output.channelBuffers32[channel];
            if (!source || !destination)
                continue;

            const double inputSample = source[sample];
            const double driven = inputSample * mCurrentDriveGain;
            const double shaped = (std::tanh(driven + bias) - biasOutput) /
                                  (smallSignalSlope * mCurrentDriveGain);
            destination[sample] = static_cast<float>(shaped * mCurrentOutputGain);
        }
    }

    output.silenceFlags = input.silenceFlags;
    return kResultOk;
}

} // namespace Steinberg

