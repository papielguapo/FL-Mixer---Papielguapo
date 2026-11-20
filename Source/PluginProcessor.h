#pragma once

#include <JuceHeader.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include <atomic>
#include <cstdint>
#include <immintrin.h>

class FLResamplingQualityAudioProcessor : public juce::AudioProcessor
{
public:
    // Exact 9 modes from FLEngine_x64.dll @ VA 0x013EB80C (halfTaps: 1, 3, 8, 12, 16, 32, 64, 128, 256)
    enum QualityMode
    {
        Linear2Point = 0,
        Hermite6Point,
        Sinc16Point,
        Sinc24Point,
        Sinc32Point,
        Sinc64Point,
        Sinc128Point,
        Sinc256Point,
        Sinc512Point,
        NumQualityModes
    };

    FLResamplingQualityAudioProcessor();
    ~FLResamplingQualityAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const juce::StringArray& getQualityChoices();

private:
    static constexpr int kMaxSincTaps = 512;
    static constexpr int kPolyphaseResolution = 256;
    static constexpr int kRingBufferSize = 4096;     // Power-of-two ring buffer (>= 512 taps)
    static constexpr int kRingBufferMask = kRingBufferSize - 1;
    static constexpr int kMaxChannels = 2;

    // Exact TResampler Sinc angular constants from FLEngine_x64.dll (VA 0x0067D320 & VA 0x0067D9B0)
    struct FLSincKernelConfig
    {
        int halfTaps = 256;           // [rbx+0E4h] in FLEngine_x64.dll (1, 3, 8, 12, 16, 32, 64, 128, 256)
        double deltaW = 0.0;          // [rbx+120h] = PI / halfTaps
        double deltaS = 0.0;          // [rbx+128h] = (abs(step) > 1.0) ? (PI / abs(step)) : PI
        double twoCosDeltaS = -2.0;   // [rbx+130h] = (abs(step) > 1.0) ? (2.0 * cos(deltaS)) : -2.0
        double twoCosDeltaW = 0.0;    // [rbx+138h] = 2.0 * cos(deltaW)
    };

    struct SincTable
    {
        int numTaps = 0;
        std::vector<float> coeffs;
    };

    struct RationalResamplerState
    {
        std::array<float, kRingBufferSize> history {};
        int writePos = 0;
        int64_t stepIndex = 0;
        int64_t totalWritten = 0;

        void reset (int initialDelaySamples) noexcept
        {
            history.fill (0.0f);
            writePos = initialDelaySamples & kRingBufferMask;
            stepIndex = 0;
            totalWritten = initialDelaySamples;
        }

        inline void pushSample (float s) noexcept
        {
            history[static_cast<size_t> (writePos)] = s;
            writePos = (writePos + 1) & kRingBufferMask;
            ++totalWritten;
        }

        inline float readAt (int64_t absoluteIndex) const noexcept
        {
            return history[static_cast<size_t> (absoluteIndex & kRingBufferMask)];
        }
    };

    struct ChannelPipeline
    {
        RationalResamplerState downStage; // 8 -> 7 (step = 8.0 / 7.0)
        RationalResamplerState upStage;   // 7 -> 8 (step = 7.0 / 8.0)
    };

    void buildFLSincConfigsAndTables();
    static int getHalfTapsForMode (QualityMode mode) noexcept;
    static int getTapsForMode (QualityMode mode) noexcept;
    static int computeLatencyForMode (QualityMode mode) noexcept;
    void resetPipelinesForMode (QualityMode mode) noexcept;

    float evaluateSample (const RationalResamplerState& state,
                          int64_t baseIndex,
                          double frac,
                          QualityMode mode,
                          bool isDownStage) const noexcept;

    // 1:1 FLEngine_x64.dll @ VA 0x0067A940 (2-point linear)
    float evaluateLinear2 (const RationalResamplerState& state,
                           int64_t baseIndex,
                           double frac) const noexcept;

    // 1:1 FLEngine_x64.dll @ VA 0x0067B220 + table @ VA 0x013E9EB4..0x013E9EF8 (6-point 5th-order Hermite)
    float evaluateHermite6 (const RationalResamplerState& state,
                            int64_t baseIndex,
                            double frac) const noexcept;

    // 1:1 FLEngine_x64.dll @ VA 0x0067C510 + VA 0x0067C7E0 + VA 0x0067C600 (Chebyshev + SSE rcpps NR Sinc)
    float evaluateFLSincSSE (const RationalResamplerState& state,
                             int64_t baseIndex,
                             double frac,
                             const FLSincKernelConfig& cfg) const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* qualityParam = nullptr;

    std::array<FLSincKernelConfig, NumQualityModes> downSincConfigs; // step = 8.0 / 7.0 (> 1.0)
    std::array<FLSincKernelConfig, NumQualityModes> upSincConfigs;   // step = 7.0 / 8.0 (<= 1.0)
    std::array<SincTable, NumQualityModes> downSincTables;
    std::array<SincTable, NumQualityModes> upSincTables;

    std::array<ChannelPipeline, kMaxChannels> channels;
    QualityMode activeMode = Sinc512Point;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FLResamplingQualityAudioProcessor)
};
