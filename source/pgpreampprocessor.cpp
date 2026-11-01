#include "pgpreampprocessor.h"
#include "pgpreampcids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

namespace Steinberg {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct BiquadCoefficients
{
    double b0 {1.0};
    double b1 {0.0};
    double b2 {0.0};
    double a1 {0.0};
    double a2 {0.0};
};

BiquadCoefficients makeLowShelf(double sampleRate, double frequency, double gainDb)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double sine = std::sin(w0);
    const double alpha = sine / (2.0 * 0.7071067811865476);
    const double rootA = std::sqrt(a);

    const double b0 = a * ((a + 1.0) - (a - 1.0) * cosine + 2.0 * rootA * alpha);
    const double b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine);
    const double b2 = a * ((a + 1.0) - (a - 1.0) * cosine - 2.0 * rootA * alpha);
    const double a0 = (a + 1.0) + (a - 1.0) * cosine + 2.0 * rootA * alpha;
    const double a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosine);
    const double a2 = (a + 1.0) + (a - 1.0) * cosine - 2.0 * rootA * alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoefficients makePeak(double sampleRate, double frequency, double gainDb)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * 0.85);

    const double b0 = 1.0 + alpha * a;
    const double b1 = -2.0 * cosine;
    const double b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a;
    const double a1 = -2.0 * cosine;
    const double a2 = 1.0 - alpha / a;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoefficients makeHighShelf(double sampleRate, double frequency, double gainDb)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double sine = std::sin(w0);
    const double alpha = sine / (2.0 * 0.7071067811865476);
    const double rootA = std::sqrt(a);

    const double b0 = a * ((a + 1.0) + (a - 1.0) * cosine + 2.0 * rootA * alpha);
    const double b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine);
    const double b2 = a * ((a + 1.0) + (a - 1.0) * cosine - 2.0 * rootA * alpha);
    const double a0 = (a + 1.0) - (a - 1.0) * cosine + 2.0 * rootA * alpha;
    const double a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosine);
    const double a2 = (a + 1.0) - (a - 1.0) * cosine - 2.0 * rootA * alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double logCosh(double value)
{
    const double magnitude = std::abs(value);
    return magnitude + std::log1p(std::exp(-2.0 * magnitude)) - std::log(2.0);
}

double processClassABjtStage(double input)
{
    // Normalized single-ended BJT stage: exponential junction current with
    // emitter degeneration, centered at its quiescent operating point.
    constexpr double effectiveThermalScale = 0.24;
    constexpr double emitterFeedback = 2.0;
    constexpr double outputHeadroom = 1.2;
    const double target = std::clamp(input, -2.0, 2.0) / effectiveThermalScale;
    double normalizedCurrent = 1.0;

    for (int iteration = 0; iteration < 8; ++iteration)
    {
        const double current = std::max(normalizedCurrent, 1.0e-6);
        const double residual = std::log(current) +
                                emitterFeedback * (current - 1.0) - target;
        const double slope = (1.0 / current) + emitterFeedback;
        normalizedCurrent = std::clamp(current - residual / slope, 1.0e-5, 8.0);
    }

    const double collectorSwing = (normalizedCurrent - 1.0) *
                                  effectiveThermalScale * (1.0 + emitterFeedback);
    return outputHeadroom * std::tanh(collectorSwing / outputHeadroom);
}

double processTransformerCore(double input, double& flux, double& magnetization,
                              double fluxLimit, double sampleRate)
{
    // Flux is the leaky integral of winding voltage. The saturating B-H curve
    // gives a low-frequency, level-dependent first approximation of core color.
    constexpr double lowFrequencyCorner = 8.0;
    const double leak = std::exp(-2.0 * kPi * lowFrequencyCorner / sampleRate);
    flux = leak * flux + input / sampleRate;
    const double currentMagnetization = fluxLimit * std::tanh(flux / fluxLimit);
    const double output = (currentMagnetization - magnetization) * sampleRate;
    magnetization = currentMagnetization;
    return output;
}

} // namespace

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
    {
        mSampleRate = setup.sampleRate;
        mSmoothingCoefficient = 1.0 - std::exp(-1.0 / (0.02 * mSampleRate));
    }
    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    const bool written = streamer.writeFloat(static_cast<float>(mDriveNormalized)) &&
                         streamer.writeFloat(static_cast<float>(mOutputNormalized)) &&
                         streamer.writeFloat(static_cast<float>(mLowNormalized)) &&
                         streamer.writeFloat(static_cast<float>(mMidNormalized)) &&
                         streamer.writeFloat(static_cast<float>(mHighNormalized)) &&
                         streamer.writeFloat(static_cast<float>(mClassAEnabled)) &&
                         streamer.writeFloat(static_cast<float>(mIronEnabled));
    return written ? kResultOk : kResultFalse;
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

    float low = 0.5f;
    float mid = 0.5f;
    float high = 0.5f;
    if (!streamer.readFloat(low) || !streamer.readFloat(mid) || !streamer.readFloat(high))
        low = mid = high = 0.5f;

    float classA = 0.f;
    float iron = 0.f;
    if (!streamer.readFloat(classA) || !streamer.readFloat(iron))
        classA = iron = 0.f;

    mDriveNormalized = std::clamp(static_cast<double>(drive), 0.0, 1.0);
    mOutputNormalized = std::clamp(static_cast<double>(output), 0.0, 1.0);
    mLowNormalized = std::clamp(static_cast<double>(low), 0.0, 1.0);
    mMidNormalized = std::clamp(static_cast<double>(mid), 0.0, 1.0);
    mHighNormalized = std::clamp(static_cast<double>(high), 0.0, 1.0);
    mClassAEnabled = classA >= 0.5f ? 1.0 : 0.0;
    mIronEnabled = iron >= 0.5f ? 1.0 : 0.0;

    mTargetDriveGain = std::pow(10.0, (24.0 * mDriveNormalized) / 20.0);
    mTargetOutputGain = std::pow(10.0, (-24.0 + 36.0 * mOutputNormalized) / 20.0);
    mTargetLowDb = -16.0 + 32.0 * mLowNormalized;
    mTargetMidDb = -18.0 + 36.0 * mMidNormalized;
    mTargetHighDb = -16.0 + 32.0 * mHighNormalized;
    mCurrentDriveGain = mTargetDriveGain;
    mCurrentOutputGain = mTargetOutputGain;
    mCurrentLowDb = mTargetLowDb;
    mCurrentMidDb = mTargetMidDb;
    mCurrentHighDb = mTargetHighDb;
    mTargetClassAAmount = mCurrentClassAAmount = mClassAEnabled;
    mTargetIronAmount = mCurrentIronAmount = mIronEnabled;
    mLowFilterState = {};
    mMidFilterState = {};
    mHighFilterState = {};
    mPreviousSaturationInput = {};
    mSaturationInitialized = {};
    mInputTransformerFlux = {};
    mInputTransformerMagnetization = {};
    mOutputTransformerFlux = {};
    mOutputTransformerMagnetization = {};
    return kResultOk;
}

tresult PLUGIN_API PGPreampProcessor::process(Vst::ProcessData& data)
{
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
        return kResultOk;

    auto& input = data.inputs[0];
    auto& output = data.outputs[0];
    if (data.symbolicSampleSize != Vst::kSample32 ||
        input.channelBuffers32 == nullptr || output.channelBuffers32 == nullptr)
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
            switch (queue->getParameterId())
            {
                case kPGPreampDriveId:
                    mDriveNormalized = normalized;
                    mTargetDriveGain = std::pow(10.0, (24.0 * normalized) / 20.0);
                    break;
                case kPGPreampOutputId:
                    mOutputNormalized = normalized;
                    mTargetOutputGain = std::pow(10.0, (-24.0 + 36.0 * normalized) / 20.0);
                    break;
                case kPGPreampLowId:
                    mLowNormalized = normalized;
                    mTargetLowDb = -16.0 + 32.0 * normalized;
                    break;
                case kPGPreampMidId:
                    mMidNormalized = normalized;
                    mTargetMidDb = -18.0 + 36.0 * normalized;
                    break;
                case kPGPreampHighId:
                    mHighNormalized = normalized;
                    mTargetHighDb = -16.0 + 32.0 * normalized;
                    break;
                case kPGPreampClassAId:
                    mClassAEnabled = normalized >= 0.5 ? 1.0 : 0.0;
                    mTargetClassAAmount = mClassAEnabled;
                    break;
                case kPGPreampIronId:
                    mIronEnabled = normalized >= 0.5 ? 1.0 : 0.0;
                    mTargetIronAmount = mIronEnabled;
                    break;
                default:
                    break;
            }
        }
    }

    const double blockSmoothing = 1.0 - std::pow(1.0 - mSmoothingCoefficient, data.numSamples);
    mCurrentLowDb += blockSmoothing * (mTargetLowDb - mCurrentLowDb);
    mCurrentMidDb += blockSmoothing * (mTargetMidDb - mCurrentMidDb);
    mCurrentHighDb += blockSmoothing * (mTargetHighDb - mCurrentHighDb);

    const auto lowCoefficients = makeLowShelf(mSampleRate, 110.0, mCurrentLowDb);
    const auto midCoefficients = makePeak(mSampleRate, 1600.0, mCurrentMidDb);
    const auto highCoefficients = makeHighShelf(mSampleRate, 12000.0, mCurrentHighDb);
    const int32 channels = std::min<int32>({input.numChannels, output.numChannels, 2});

    const double bias = 0.18;
    const double biasOutput = std::tanh(bias);
    const double smallSignalSlope = 1.0 - biasOutput * biasOutput;
    for (int32 sample = 0; sample < data.numSamples; ++sample)
    {
        mCurrentDriveGain += mSmoothingCoefficient * (mTargetDriveGain - mCurrentDriveGain);
        mCurrentOutputGain += mSmoothingCoefficient * (mTargetOutputGain - mCurrentOutputGain);
        mCurrentClassAAmount += mSmoothingCoefficient *
                                (mTargetClassAAmount - mCurrentClassAAmount);
        mCurrentIronAmount += mSmoothingCoefficient *
                              (mTargetIronAmount - mCurrentIronAmount);

        for (int32 channel = 0; channel < channels; ++channel)
        {
            const float* source = input.channelBuffers32[channel];
            float* destination = output.channelBuffers32[channel];
            if (!source || !destination)
                continue;

            const double inputSample = static_cast<double>(source[sample]);
            const double transformerInput = processTransformerCore(
                inputSample, mInputTransformerFlux[channel],
                mInputTransformerMagnetization[channel], 0.0030, mSampleRate);
            const double modelledInput = inputSample + mCurrentIronAmount *
                                         (transformerInput - inputSample);
            const double driven = modelledInput * mCurrentDriveGain;
            const double directSaturation = (std::tanh(driven + bias) - biasOutput) /
                                            (smallSignalSlope * mCurrentDriveGain);
            double processed = directSaturation;
            if (mSaturationInitialized[channel])
            {
                const double previous = mPreviousSaturationInput[channel];
                const double delta = driven - previous;
                if (std::abs(delta) > 1.0e-5)
                {
                    const double antiderivative = (logCosh(driven + bias) - logCosh(bias) -
                                                   driven * biasOutput) / smallSignalSlope;
                    const double previousAntiderivative =
                        (logCosh(previous + bias) - logCosh(bias) - previous * biasOutput) /
                        smallSignalSlope;
                    processed = (antiderivative - previousAntiderivative) /
                                (delta * mCurrentDriveGain);
                }
            }
            mPreviousSaturationInput[channel] = driven;
            mSaturationInitialized[channel] = true;

            if (mCurrentClassAAmount > 1.0e-6)
            {
                const double classAOutput = processClassABjtStage(processed);
                processed += mCurrentClassAAmount * (classAOutput - processed);
            }

            auto applyFilter = [](double value, const BiquadCoefficients& coefficients,
                                  FilterState& state)
            {
                const double result = coefficients.b0 * value + coefficients.b1 * state.x1 +
                                      coefficients.b2 * state.x2 - coefficients.a1 * state.y1 -
                                      coefficients.a2 * state.y2;
                state.x2 = state.x1;
                state.x1 = value;
                state.y2 = state.y1;
                state.y1 = result;
                return result;
            };

            processed = applyFilter(processed, lowCoefficients, mLowFilterState[channel]);
            processed = applyFilter(processed, midCoefficients, mMidFilterState[channel]);
            processed = applyFilter(processed, highCoefficients, mHighFilterState[channel]);

            const double transformerOutput = processTransformerCore(
                processed, mOutputTransformerFlux[channel],
                mOutputTransformerMagnetization[channel], 0.0012, mSampleRate);
            if (mCurrentIronAmount > 1.0e-6)
                processed += mCurrentIronAmount * (transformerOutput - processed);
            destination[sample] = static_cast<float>(processed * mCurrentOutputGain);
        }
    }

    output.silenceFlags = input.silenceFlags;
    return kResultOk;
}

} // namespace Steinberg
