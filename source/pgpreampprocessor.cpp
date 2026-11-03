#include "pgpreampprocessor.h"
#include "pgpreampcids.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

tresult PLUGIN_API PGPreampProcessor::setupProcessing(Vst::ProcessSetup& setup)
{
    const tresult result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk)
        return result;

    PGAudio::Circuit::EbersMollNpn transistor;
    transistor.prepare(1.0e-14, 120.0, 2.0, 300.15);

    // Provisional generic device parameters for the first circuit audition.
    // These are not yet calibrated to a selected production transistor.
    PGAudio::Circuit::BjtAdvancedParameters deviceParameters;
    deviceParameters.forwardEarlyVoltage = 85.0;
    deviceParameters.reverseEarlyVoltage = 45.0;
    deviceParameters.baseResistanceOhms = 120.0;
    deviceParameters.collectorResistanceOhms = 4.0;
    deviceParameters.emitterResistanceOhms = 1.5;
    deviceParameters.baseEmitterCapacitanceFarads = 22.0e-12;
    deviceParameters.baseCollectorCapacitanceFarads = 7.0e-12;
    deviceParameters.forwardTransitTimeSeconds = 0.35e-9;
    deviceParameters.reverseTransitTimeSeconds = 2.0e-9;
    transistor.setAdvancedParameters(deviceParameters);

    PGAudio::Circuit::FixedCommonEmitterStage::Parameters stageParameters;
    stagesReady_ = true;
    for (auto& stage : stages_)
    {
        if (!stage.prepare(setup.sampleRate, stageParameters, transistor))
        {
            stagesReady_ = false;
            break;
        }
    }

    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

double PGPreampProcessor::decibelsToLinear(double decibels) noexcept
{
    return std::pow(10.0, decibels / 20.0);
}

void PGPreampProcessor::updateParameters(Vst::ProcessData& data) noexcept
{
    if (data.inputParameterChanges == nullptr)
        return;

    const int32 queueCount = data.inputParameterChanges->getParameterCount();
    for (int32 queueIndex = 0; queueIndex < queueCount; ++queueIndex)
    {
        auto* queue = data.inputParameterChanges->getParameterData(queueIndex);
        if (queue == nullptr)
            continue;

        const int32 pointCount = queue->getPointCount();
        if (pointCount <= 0)
            continue;

        int32 sampleOffset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint(pointCount - 1, sampleOffset, value) != kResultOk ||
            !std::isfinite(value))
            continue;

        value = std::clamp(value, 0.0, 1.0);
        switch (queue->getParameterId())
        {
            case kPGPreampBypassId:
                bypass_ = value >= 0.5;
                break;
            case kPGPreampInputGainId:
                inputGainNormalized_ = value;
                break;
            case kPGPreampOutputGainId:
                outputGainNormalized_ = value;
                break;
            default:
                break;
        }
    }
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

    updateParameters(data);

    const int32 channels = std::min(input.numChannels, output.numChannels);
    if (bypass_ || !stagesReady_)
    {
        for (int32 channel = 0; channel < channels; ++channel)
        {
            const float* source = input.channelBuffers32[channel];
            float* destination = output.channelBuffers32[channel];
            if (source != nullptr && destination != nullptr && source != destination)
                std::copy_n(source, data.numSamples, destination);
        }
        output.silenceFlags = input.silenceFlags;
        return kResultOk;
    }

    const double inputDb = -12.0 + 30.0 * inputGainNormalized_;
    const double outputDb = -12.0 + 24.0 * outputGainNormalized_;
    const float inputVoltsPerFullScale =
        static_cast<float>(0.005 * decibelsToLinear(inputDb));
    const float outputFullScalePerVolt =
        static_cast<float>(40.0 * decibelsToLinear(outputDb));

    for (int32 channel = 0; channel < channels && channel < 2; ++channel)
    {
        const float* source = input.channelBuffers32[channel];
        float* destination = output.channelBuffers32[channel];
        if (source == nullptr || destination == nullptr)
            continue;

        auto& stage = stages_[static_cast<std::size_t>(channel)];
        for (int32 sample = 0; sample < data.numSamples; ++sample)
        {
            const float drivenVolts = source[sample] * inputVoltsPerFullScale;
            const float processedVolts = stage.processSample(drivenVolts);
            const float outputSample = processedVolts * outputFullScalePerVolt;
            destination[sample] = std::isfinite(outputSample)
                ? outputSample : source[sample];
        }
    }

    output.silenceFlags = 0;
    return kResultOk;
}

} // namespace Steinberg
