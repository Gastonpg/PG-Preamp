#include "pgpreampprocessor.h"
#include "pgpreampcids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Steinberg {

namespace {
constexpr double kAStageReferenceGain = 4.1316;
constexpr double kIronReferenceGain = 1.2545;
constexpr double kInputVoltsPerFullScale = 0.005;
constexpr double kOutputFullScalePerVolt = 40.0;
constexpr double kSwitchSmoothingSeconds = 0.005;
}

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

    // State restoration happens outside the sample loop; initialize the mixes
    // to their restored targets without an audible transition.
    aStageMix_ = aStageEnabled_ ? 1.0 : 0.0;
    ironMix_ = ironEnabled_ ? 1.0 : 0.0;
    bypassMix_ = bypass_ ? 1.0 : 0.0;
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

    mixSmoothingCoefficient_ = 1.0 -
        std::exp(-1.0 / (setup.sampleRate * kSwitchSmoothingSeconds));
    aStageMix_ = aStageEnabled_ ? 1.0 : 0.0;
    ironMix_ = ironEnabled_ ? 1.0 : 0.0;
    bypassMix_ = bypass_ ? 1.0 : 0.0;

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

    // Iron is anchored to the published 10468 microphone-input transformer
    // reference data and the Carnhill VTB9045 replacement configuration:
    // 1:2 turns ratio, 300-ohm primary / 1.2-kohm secondary, with parallel
    // primary and secondary coils. Their published coil DCR is 24+24 ohms
    // primary and 130+130 ohms secondary, which becomes 12 / 65 ohms in
    // parallel. The absolute turns and core geometry below are effective
    // macro-model parameters because the original Marinair internals are not
    // publicly specified. They remain provisional pending measurements.
    PGAudio::Circuit::FixedMagneticTransformerStage::Parameters ironParameters;
    ironParameters.saturationFluxDensityTesla = 0.8;
    ironParameters.coreAreaSquareMeters = 1.0e-4;
    ironParameters.magneticPathLengthMeters = 0.08;
    ironParameters.relativePermeability = 16000.0;
    ironParameters.coerciveFieldAmpsPerMeter = 1.0e-6;
    ironParameters.magneticRelaxationSeconds = 1.0e-6;
    ironParameters.branchMemoryRelaxationSeconds = 1.0e-3;
    ironParameters.branchTransitionFieldAmpsPerMeter = 1.0e-5;
    ironParameters.primaryTurns = 1000.0;
    ironParameters.secondaryTurns = 2000.0;
    ironParameters.sourceResistanceOhms = 150.0;
    ironParameters.primaryWindingResistanceOhms = 12.0;
    ironParameters.secondaryWindingResistanceOhms = 65.0;
    // Small effective leakage values maintain the published wide-band
    // response target; leakage remains a provisional parameter, not a
    // claimed measurement of the original 10468.
    ironParameters.primaryLeakageInductanceHenries = 1.0e-5;
    ironParameters.secondaryLeakageInductanceHenries = 1.0e-5;
    ironParameters.loadResistanceOhms = 1200.0;

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

void PGPreampProcessor::applyParameter(Vst::ParamID id, Vst::ParamValue value) noexcept
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, 0.0, 1.0);

    switch (id)
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
        if (queue->getPoint(pointCount - 1, sampleOffset, value) == kResultOk)
            applyParameter(queue->getParameterId(), value);
    }
}

tresult PLUGIN_API PGPreampProcessor::process(Vst::ProcessData& data)
{
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
    {
        updateParameters(data);
        return kResultOk;
    }

    auto& input = data.inputs[0];
    auto& output = data.outputs[0];

    if (data.symbolicSampleSize != Vst::kSample32 ||
        input.channelBuffers32 == nullptr ||
        output.channelBuffers32 == nullptr)
    {
        updateParameters(data);
        return kResultOk;
    }

    const int32 channels = std::min(input.numChannels, output.numChannels);
    if (channels <= 0)
    {
        updateParameters(data);
        return kResultOk;
    }

    // Keep parameter event handling allocation-free and sample-offset accurate.
    // The plug-in exposes five parameters, so five fixed cursors are enough.
    using QueuePointer = decltype(data.inputParameterChanges->getParameterData(0));
    struct ParameterCursor
    {
        QueuePointer queue = nullptr;
        Vst::ParamID id = 0;
        int32 pointCount = 0;
        int32 nextPoint = 0;
        int32 nextOffset = 0;
        Vst::ParamValue nextValue = 0.0;
        bool hasPoint = false;
    };
    std::array<ParameterCursor, 5> cursors{};
    std::size_t cursorCount = 0;

    if (data.inputParameterChanges != nullptr)
    {
        const int32 queueCount = data.inputParameterChanges->getParameterCount();
        for (int32 queueIndex = 0;
             queueIndex < queueCount && cursorCount < cursors.size();
             ++queueIndex)
        {
            auto* queue = data.inputParameterChanges->getParameterData(queueIndex);
            if (queue == nullptr)
                continue;

            const Vst::ParamID id = queue->getParameterId();
            if (id != kPGPreampBypassId &&
                id != kPGPreampInputGainId &&
                id != kPGPreampOutputGainId &&
                id != kPGPreampAStageId &&
                id != kPGPreampIronId)
                continue;

            ParameterCursor& cursor = cursors[cursorCount++];
            cursor.queue = queue;
            cursor.id = id;
            cursor.pointCount = queue->getPointCount();
            cursor.nextPoint = 0;
            cursor.hasPoint = false;

            while (cursor.nextPoint < cursor.pointCount)
            {
                if (queue->getPoint(cursor.nextPoint,
                                    cursor.nextOffset,
                                    cursor.nextValue) == kResultOk &&
                    std::isfinite(cursor.nextValue))
                {
                    cursor.hasPoint = true;
                    break;
                }
                ++cursor.nextPoint;
            }
        }
    }

    double inputGainLinear =
        decibelsToLinear(-12.0 + 30.0 * inputGainNormalized_);
    double outputGainLinear =
        decibelsToLinear(-12.0 + 24.0 * outputGainNormalized_);

    for (int32 sample = 0; sample < data.numSamples; ++sample)
    {
        // Apply all parameter points scheduled at this sample offset.
        for (std::size_t cursorIndex = 0; cursorIndex < cursorCount; ++cursorIndex)
        {
            ParameterCursor& cursor = cursors[cursorIndex];
            while (cursor.hasPoint && cursor.nextOffset <= sample)
            {
                applyParameter(cursor.id, cursor.nextValue);
                if (cursor.id == kPGPreampInputGainId)
                    inputGainLinear =
                        decibelsToLinear(-12.0 + 30.0 * inputGainNormalized_);
                else if (cursor.id == kPGPreampOutputGainId)
                    outputGainLinear =
                        decibelsToLinear(-12.0 + 24.0 * outputGainNormalized_);

                ++cursor.nextPoint;
                cursor.hasPoint = false;
                while (cursor.nextPoint < cursor.pointCount)
                {
                    if (cursor.queue->getPoint(cursor.nextPoint,
                                                cursor.nextOffset,
                                                cursor.nextValue) == kResultOk &&
                        std::isfinite(cursor.nextValue))
                    {
                        cursor.hasPoint = true;
                        break;
                    }
                    ++cursor.nextPoint;
                }
            }
        }

        const double targetIronMix = ironEnabled_ && transformersReady_ ? 1.0 : 0.0;
        const double targetAStageMix = aStageEnabled_ && stagesReady_ ? 1.0 : 0.0;
        const double targetBypassMix = bypass_ ? 1.0 : 0.0;
        ironMix_ += mixSmoothingCoefficient_ * (targetIronMix - ironMix_);
        aStageMix_ += mixSmoothingCoefficient_ * (targetAStageMix - aStageMix_);
        bypassMix_ += mixSmoothingCoefficient_ * (targetBypassMix - bypassMix_);

        for (int32 channel = 0; channel < channels && channel < 2; ++channel)
        {
            const float* source = input.channelBuffers32[channel];
            float* destination = output.channelBuffers32[channel];
            if (source == nullptr || destination == nullptr)
                continue;

            auto& stage = stages_[static_cast<std::size_t>(channel)];
            auto& transformer = transformers_[static_cast<std::size_t>(channel)];
            const float sourceSample = source[sample];

            // The 10468-style transformer sits before the preamp gain control.
            // It runs continuously so engaging Iron cannot start from a cold
            // magnetic state. The off path is level-matched to the measured
            // small-signal transformer ratio.
            const float transformerInput =
                sourceSample * static_cast<float>(kInputVoltsPerFullScale);
            float transformerWet = transformerInput;
            if (transformersReady_)
            {
                PGAudio::Circuit::FixedMagneticTransformerStage::ProcessResult
                    transformerResult;
                if (transformer.processSample(transformerInput, transformerResult))
                    transformerWet = static_cast<float>(
                        transformerResult.outputVolts / kIronReferenceGain);
            }
            const float ironOutput = transformerInput +
                (transformerWet - transformerInput) * static_cast<float>(ironMix_);

            // Input gain follows the input transformer, matching the basic
            // 1073 gain-staging order more closely than driving the transformer
            // with the gain knob itself.
            const float stageInput = ironOutput * static_cast<float>(inputGainLinear);

            // Keep A Stage warm while bypassed. Its dry path uses the measured
            // small-signal gain as makeup so toggling the colour block does not
            // cause the 12.3 dB level drop observed in the first audition.
            float stageWet = stageInput;
            if (stagesReady_)
            {
                PGAudio::Circuit::FixedCommonEmitterStage::ProcessResult stageResult;
                if (stage.processSample(stageInput, stageResult))
                    stageWet = stageResult.output;
            }
            const float stageDry = stageInput *
                static_cast<float>(stagesReady_ ? kAStageReferenceGain : 1.0);
            const float stageOutput = stageDry +
                (stageWet - stageDry) * static_cast<float>(aStageMix_);

            const float processedSample = stageOutput *
                static_cast<float>(kOutputFullScalePerVolt * outputGainLinear);
            const float finalSample = processedSample +
                (sourceSample - processedSample) * static_cast<float>(bypassMix_);
            destination[sample] = std::isfinite(finalSample)
                ? finalSample : sourceSample;
        }
    }

    output.silenceFlags = 0;
    return kResultOk;
}

} // namespace Steinberg
