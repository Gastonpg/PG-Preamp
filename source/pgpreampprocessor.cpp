#include "pgpreampprocessor.h"
#include "pgpreampcids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"

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

tresult PLUGIN_API PGPreampProcessor::setState(IBStream* state)
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

    // Optional fields preserve compatibility with sessions saved before the
    // A Stage / Iron switches existed.
    float aStageValue = 1.0f;
    float ironValue = 0.0f;
    streamer.readFloat(aStageValue);
    streamer.readFloat(ironValue);

    bypass_ = bypassValue >= 0.5f;
    inputGainNormalized_ = std::clamp(static_cast<double>(inputValue), 0.0, 1.0);
    outputGainNormalized_ = std::clamp(static_cast<double>(outputValue), 0.0, 1.0);
    aStageEnabled_ = aStageValue >= 0.5f;
    ironEnabled_ = ironValue >= 0.5f;
    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::getState(IBStream* state)
{
    if (state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    if (!streamer.writeFloat(bypass_ ? 1.0f : 0.0f) ||
        !streamer.writeFloat(static_cast<float>(inputGainNormalized_)) ||
        !streamer.writeFloat(static_cast<float>(outputGainNormalized_)) ||
        !streamer.writeFloat(aStageEnabled_ ? 1.0f : 0.0f) ||
        !streamer.writeFloat(ironEnabled_ ? 1.0f : 0.0f))
        return kResultFalse;

    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::setupProcessing(Vst::ProcessSetup& setup)
{
    const tresult result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk)
        return result;

    PGAudio::Circuit::EbersMollNpn transistor;
    transistor.prepare(1.0e-14, 120.0, 2.0, 300.15);

    // Provisional generic device parameters. They are intentionally not
    // described as a calibrated BC184C or an exact commercial preamp device.
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

    // Input-transformer prototype: 600-ohm source, 15-kohm secondary load,
    // explicit winding resistance/leakage, and a smooth hysteretic core.
    // Values are provisional engineering defaults pending a selected
    // transformer's datasheet and impedance/B-H measurements.
    PGAudio::Circuit::FixedMagneticTransformerStage::Parameters ironParameters;
    ironParameters.saturationFluxDensityTesla = 0.8;
    ironParameters.coreAreaSquareMeters = 1.0e-4;
    ironParameters.magneticPathLengthMeters = 0.08;
    ironParameters.relativePermeability = 4000.0;
    ironParameters.coerciveFieldAmpsPerMeter = 0.05;
    ironParameters.magneticRelaxationSeconds = 0.01;
    ironParameters.primaryTurns = 1200.0;
    ironParameters.secondaryTurns = 1200.0;
    ironParameters.sourceResistanceOhms = 600.0;
    ironParameters.primaryWindingResistanceOhms = 80.0;
    ironParameters.secondaryWindingResistanceOhms = 60.0;
    ironParameters.primaryLeakageInductanceHenries = 1.0e-3;
    ironParameters.secondaryLeakageInductanceHenries = 1.0e-3;
    ironParameters.loadResistanceOhms = 15000.0;

    transformersReady_ = true;
    for (auto& transformer : transformers_)
    {
        if (!transformer.prepare(setup.sampleRate, ironParameters))
        {
            transformersReady_ = false;
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
            case kPGPreampAStageId:
                aStageEnabled_ = value >= 0.5;
                break;
            case kPGPreampIronId:
                ironEnabled_ = value >= 0.5;
                break;
            default:
                break;
        }
    }
}

tresult PLUGIN_API PGPreampProcessor::process(Vst::ProcessData& data)
{
    // Hosts may flush automation/state without audio buffers. Consume parameter
    // queues before validating audio buses so controls stay in sync.
    updateParameters(data);

    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
        return kResultOk;

    auto& input = data.inputs[0];
    auto& output = data.outputs[0];

    if (data.symbolicSampleSize != Vst::kSample32 ||
        input.channelBuffers32 == nullptr ||
        output.channelBuffers32 == nullptr)
        return kResultOk;

    const int32 channels = std::min(input.numChannels, output.numChannels);
    if (bypass_)
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
        auto& transformer = transformers_[static_cast<std::size_t>(channel)];

        for (int32 sample = 0; sample < data.numSamples; ++sample)
        {
            float circuitVolts = source[sample] * inputVoltsPerFullScale;

            // The Iron block is the input coupling transformer. Its loaded
            // two-winding model runs before the transistor stage.
            if (ironEnabled_ && transformersReady_)
            {
                PGAudio::Circuit::FixedMagneticTransformerStage::ProcessResult
                    transformerResult;
                if (transformer.processSample(circuitVolts, transformerResult))
                    circuitVolts = transformerResult.outputVolts;
            }

            if (aStageEnabled_ && stagesReady_)
                circuitVolts = stage.processSample(circuitVolts);

            const float outputSample = circuitVolts * outputFullScalePerVolt;
            destination[sample] = std::isfinite(outputSample)
                ? outputSample : source[sample];
        }
    }

    output.silenceFlags = 0;
    return kResultOk;
}

} // namespace Steinberg
