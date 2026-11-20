#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>

#if defined (_MSC_VER)
 #pragma float_control(precise, on)
#endif

namespace FLEngineConstants
{
    inline float bitCastFloat (uint32_t bits) noexcept
    {
        float f = 0.0f;
        std::memcpy (&f, &bits, sizeof (float));
        return f;
    }

    inline double bitCastDouble (uint64_t bits) noexcept
    {
        double d = 0.0;
        std::memcpy (&d, &bits, sizeof (double));
        return d;
    }

    // Exact double constants from FLEngine_x64.dll @ VA 0x013E9F00..0x013E9F58 & VA 0x0067CB10
    static const double kPi          = bitCastDouble (0x400921FB54442D18ULL); // 3.1415926535897931
    static const double kHalfPi      = bitCastDouble (0x3FF921FB54442D18ULL); // 1.5707963267948966
    static const double kMinSincFrac = bitCastDouble (0x3ABEF2D0F5DA7DD9ULL); // 1.0e-25 (ippsThreshold_LT_64f_I)

    // Exact halfTaps lookup table from FLEngine_x64.dll @ VA 0x013EB80C ([0x015E20C0])
    static constexpr int kHalfTapsTable[9] = { 1, 3, 8, 12, 16, 32, 64, 128, 256 };

    // Exact 18 IEEE-754 float constants for 6-point Hermite from FLEngine_x64.dll @ VA 0x013E9EB4..0x013E9EF8
    static const float kHermite_13E9EB4 = bitCastFloat (0xbd2aaaabU); // -1/24
    static const float kHermite_13E9EB8 = bitCastFloat (0x3e555555U); // +5/24
    static const float kHermite_13E9EBC = bitCastFloat (0xbed55555U); // -5/12

    static const float kHermite_13E9EC0 = bitCastFloat (0x3caaaaabU); // +1/48
    static const float kHermite_13E9EC4 = bitCastFloat (0xbd800000U); // -1/16
    static const float kHermite_13E9EC8 = bitCastFloat (0x3d2aaaabU); // +1/24

    static const float kHermite_13E9ECC = bitCastFloat (0x3dd55555U); // +5/48
    static const float kHermite_13E9ED0 = bitCastFloat (0xbf300000U); // -11/16
    static const float kHermite_13E9ED4 = bitCastFloat (0x3fc55555U); // +37/24

    static const float kHermite_13E9ED8 = bitCastFloat (0xbd555555U); // -5/96
    static const float kHermite_13E9EDC = bitCastFloat (0x3ed00000U); // +13/32
    static const float kHermite_13E9EE0 = bitCastFloat (0xbeb55555U); // -17/48

    static const float kHermite_13E9EE4 = bitCastFloat (0xbcc00000U); // -3/128
    static const float kHermite_13E9EE8 = bitCastFloat (0x3e22aaabU); // +61/384
    static const float kHermite_13E9EEC = bitCastFloat (0xbfae0000U); // -87/64

    static const float kHermite_13E9EF0 = bitCastFloat (0x3c400000U); // +3/256
    static const float kHermite_13E9EF4 = bitCastFloat (0xbdc80000U); // -25/256
    static const float kHermite_13E9EF8 = bitCastFloat (0x3f160000U); // +75/128

    // Exact Sinc SSE constants from FLEngine_x64.dll @ VA 0x0067C9EC..0x0067CA50
    static const float kMinusTwoPiF = bitCastFloat (0xC0C90FDBU); // -6.2831855f
    static const float kPlusTwoPiF  = bitCastFloat (0x40C90FDBU); // +6.2831855f
    static const float kWinC0       = bitCastFloat (0x3EB851ECU); //  0.36f
    static const float kWinC2       = bitCastFloat (0x3F23D70AU); //  0.64f
    static const float kHalfF       = bitCastFloat (0x3F000000U); //  0.50f
}

const juce::StringArray& FLResamplingQualityAudioProcessor::getQualityChoices()
{
    static const juce::StringArray choices
    {
        "2-point linear",
        "6-point Hermite",
        "16-point sinc",
        "24-point sinc",
        "32-point sinc",
        "64-point sinc",
        "128-point sinc",
        "256-point sinc",
        "512-point sinc"
    };
    return choices;
}

juce::AudioProcessorValueTreeState::ParameterLayout FLResamplingQualityAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "quality", 1 },
        "Resampling quality",
        getQualityChoices(),
        static_cast<int> (Sinc512Point)));

    return { params.begin(), params.end() };
}

FLResamplingQualityAudioProcessor::FLResamplingQualityAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    qualityParam = apvts.getRawParameterValue ("quality");
    buildFLSincConfigsAndTables();

    activeMode = Sinc512Point;
    resetPipelinesForMode (activeMode);
    setLatencySamples (computeLatencyForMode (activeMode));
}

FLResamplingQualityAudioProcessor::~FLResamplingQualityAudioProcessor() = default;

void FLResamplingQualityAudioProcessor::buildFLSincConfigsAndTables()
{
    // Exact TResampler.SetQuality (VA 0x0067D320) & TResampler.SetRate (VA 0x0067D9B0) from FLEngine_x64.dll
    constexpr double downStep = 8.0 / 7.0; // > 1.0 (bandlimited decimation)
    constexpr double upStep   = 7.0 / 8.0; // <= 1.0 (interpolation)

    const auto makeConfig = [] (int halfTaps, double step) -> FLSincKernelConfig
    {
        FLSincKernelConfig cfg;
        cfg.halfTaps = halfTaps;
        // VA 0x0067D341..0x0067D372:
        cfg.deltaW       = FLEngineConstants::kPi / static_cast<double> (halfTaps);
        cfg.twoCosDeltaW = 2.0 * std::cos (cfg.deltaW);

        // VA 0x0067DA1E..0x0067DA76:
        const double absStep = std::abs (step);
        if (absStep > 1.0)
        {
            cfg.deltaS       = FLEngineConstants::kPi / absStep;
            cfg.twoCosDeltaS = 2.0 * std::cos (cfg.deltaS);
        }
        else
        {
            cfg.deltaS       = FLEngineConstants::kPi;
            cfg.twoCosDeltaS = -2.0; // 0xC000000000000000
        }
        return cfg;
    };

    const auto buildPolyphaseFromFLKernel = [] (SincTable& table, const FLSincKernelConfig& cfg)
    {
        const int halfTaps = cfg.halfTaps;
        const int numTaps  = halfTaps * 2;
        table.numTaps = numTaps;
        table.coeffs.resize (static_cast<size_t> (kPolyphaseResolution + 1) * static_cast<size_t> (numTaps), 0.0f);

        for (int phaseIdx = 0; phaseIdx <= kPolyphaseResolution; ++phaseIdx)
        {
            double frac = static_cast<double> (phaseIdx) / static_cast<double> (kPolyphaseResolution);
            if (frac < FLEngineConstants::kMinSincFrac)
                frac = FLEngineConstants::kMinSincFrac;

            float* row = table.coeffs.data() + static_cast<size_t> (phaseIdx * numTaps);
            double sum = 0.0;

            for (int tap = 0; tap < numTaps; ++tap)
            {
                const int tapOffset = tap - (halfTaps - 1);
                const double x = static_cast<double> (tapOffset) - frac;
                const double piX = FLEngineConstants::kPi * x;
                const double sincVal = (std::abs (piX) > 1.0e-12)
                    ? (std::sin (cfg.deltaS * x) / piX)
                    : (cfg.deltaS / FLEngineConstants::kPi);

                const float u = static_cast<float> (std::cos (cfg.deltaW * x));
                const float w = FLEngineConstants::kHalfF
                    * ((FLEngineConstants::kWinC2 * u * u + u) + FLEngineConstants::kWinC0);

                const float c = static_cast<float> (sincVal) * w;
                row[tap] = c;
                sum += static_cast<double> (c);
            }

            if (std::abs (sum) > 1.0e-12)
            {
                const float invSum = static_cast<float> (1.0 / sum);
                for (int tap = 0; tap < numTaps; ++tap)
                    row[tap] *= invSum;
            }
        }
    };

    for (int m = 0; m < static_cast<int> (NumQualityModes); ++m)
    {
        const auto mode = static_cast<QualityMode> (m);
        const int halfTaps = getHalfTapsForMode (mode);
        downSincConfigs[static_cast<size_t> (m)] = makeConfig (halfTaps, downStep);
        upSincConfigs[static_cast<size_t> (m)]   = makeConfig (halfTaps, upStep);

        if (halfTaps >= 8)
        {
            buildPolyphaseFromFLKernel (downSincTables[static_cast<size_t> (m)], downSincConfigs[static_cast<size_t> (m)]);
            buildPolyphaseFromFLKernel (upSincTables[static_cast<size_t> (m)],   upSincConfigs[static_cast<size_t> (m)]);
        }
    }
}

int FLResamplingQualityAudioProcessor::getHalfTapsForMode (QualityMode mode) noexcept
{
    const int idx = juce::jlimit (0, 8, static_cast<int> (mode));
    return FLEngineConstants::kHalfTapsTable[idx];
}

int FLResamplingQualityAudioProcessor::getTapsForMode (QualityMode mode) noexcept
{
    return getHalfTapsForMode (mode) * 2;
}

int FLResamplingQualityAudioProcessor::computeLatencyForMode (QualityMode /*mode*/) noexcept
{
    constexpr int maxHalfTaps     = 256;
    constexpr int downInitialLead = maxHalfTaps + 1;           // 257
    constexpr int upInitialLead   = ((maxHalfTaps + 8) / 7) * 7; // 259 (exact multiple of 7)
    return downInitialLead + (upInitialLead * 8) / 7;          // 257 + 296 = 553 samples exact
}

void FLResamplingQualityAudioProcessor::resetPipelinesForMode (QualityMode /*mode*/) noexcept
{
    // Both stages are primed once with the maximum lookahead (maxHalfTaps = 256):
    // Stage 1 primed with 257 samples; Stage 2 primed with 259 samples (37 * 7).
    // This gives an exact integer total delay of 553 samples across all 9 quality modes
    // and allows seamless, click-free live switching without ever resetting pointers.
    constexpr int maxHalfTaps     = 256;
    constexpr int downInitialLead = maxHalfTaps + 1;
    constexpr int upInitialLead   = ((maxHalfTaps + 8) / 7) * 7;

    for (auto& ch : channels)
    {
        ch.downStage.reset (downInitialLead);
        ch.upStage.reset (upInitialLead);
    }
}

const juce::String FLResamplingQualityAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FLResamplingQualityAudioProcessor::acceptsMidi() const { return false; }
bool FLResamplingQualityAudioProcessor::producesMidi() const { return false; }
bool FLResamplingQualityAudioProcessor::isMidiEffect() const { return false; }
double FLResamplingQualityAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int FLResamplingQualityAudioProcessor::getNumPrograms() { return 1; }
int FLResamplingQualityAudioProcessor::getCurrentProgram() { return 0; }
void FLResamplingQualityAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String FLResamplingQualityAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return "Default";
}
void FLResamplingQualityAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

void FLResamplingQualityAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    const int paramIndex = (qualityParam != nullptr)
        ? juce::jlimit (0, static_cast<int> (NumQualityModes) - 1, static_cast<int> (std::lround (qualityParam->load())))
        : static_cast<int> (Sinc512Point);

    activeMode = static_cast<QualityMode> (paramIndex);
    resetPipelinesForMode (activeMode);
    setLatencySamples (computeLatencyForMode (activeMode));
}

void FLResamplingQualityAudioProcessor::releaseResources() {}

bool FLResamplingQualityAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

float FLResamplingQualityAudioProcessor::evaluateLinear2 (const RationalResamplerState& state,
                                                          int64_t baseIndex,
                                                          double frac) const noexcept
{
    // Exact 1:1 with FLEngine_x64.dll @ VA 0x0067A964:
    // cvtss2sd xmm1, [rax]; movsd xmm2, 1.0; subsd xmm2, xmm0; mulsd xmm1, xmm2;
    // cvtss2sd xmm2, [rax+4]; mulsd xmm2, xmm0; addsd xmm1, xmm2; cvtsd2ss xmm0, xmm1
    const double y0 = static_cast<double> (state.readAt (baseIndex));
    const double y1 = static_cast<double> (state.readAt (baseIndex + 1));
    return static_cast<float> (y0 * (1.0 - frac) + y1 * frac);
}

float FLResamplingQualityAudioProcessor::evaluateHermite6 (const RationalResamplerState& state,
                                                           int64_t baseIndex,
                                                           double frac) const noexcept
{
    // Exact 1:1 with FLEngine_x64.dll @ VA 0x0067B220 + constants @ VA 0x013E9EB4..0x013E9EF8:
    // Centered fractional offset: xmm0 = frac - 0.5 (VA 0x0067B478 = 0x3FE0000000000000)
    const double xmm0 = frac - 0.5;

    const float ym2 = state.readAt (baseIndex - 2); // [rax+00h]
    const float ym1 = state.readAt (baseIndex - 1); // [rax+04h]
    const float y0  = state.readAt (baseIndex);     // [rax+08h]
    const float y1  = state.readAt (baseIndex + 1); // [rax+0Ch]
    const float y2  = state.readAt (baseIndex + 2); // [rax+10h]
    const float y3  = state.readAt (baseIndex + 3); // [rax+14h]

    // Symmetric / anti-symmetric pairs around t = 0.5 (VA 0x0067B280..0x0067B2B5)
    const float xmm1 = ym2 + y3;
    const float xmm2 = ym2 - y3;
    const float xmm3 = ym1 + y2;
    const float xmm4 = ym1 - y2;
    const float xmm5 = y0  + y1;
    const float xmm6 = y0  - y1;

    using namespace FLEngineConstants;

    // Degree 5 -> 4 (VA 0x0067B2BA..0x0067B2F5)
    float xmm7 = (kHermite_13E9EB4 * xmm2 + kHermite_13E9EB8 * xmm4) + kHermite_13E9EBC * xmm6;
    xmm7 = static_cast<float> (static_cast<double> (xmm7) * xmm0);

    // Degree 4 -> 3 (VA 0x0067B2FA..0x0067B33C)
    float xmm8 = ((kHermite_13E9EC0 * xmm1 + kHermite_13E9EC4 * xmm3) + kHermite_13E9EC8 * xmm5) + xmm7;
    xmm7 = static_cast<float> (static_cast<double> (xmm8) * xmm0);

    // Degree 3 -> 2 (VA 0x0067B341..0x0067B383)
    xmm8 = ((kHermite_13E9ECC * xmm2 + kHermite_13E9ED0 * xmm4) + kHermite_13E9ED4 * xmm6) + xmm7;
    xmm7 = static_cast<float> (static_cast<double> (xmm8) * xmm0);

    // Degree 2 -> 1 (VA 0x0067B388..0x0067B3CA)
    xmm8 = ((kHermite_13E9ED8 * xmm1 + kHermite_13E9EDC * xmm3) + kHermite_13E9EE0 * xmm5) + xmm7;
    xmm7 = static_cast<float> (static_cast<double> (xmm8) * xmm0);

    // Degree 1 -> 0 (VA 0x0067B3CF..0x0067B40D)
    xmm8 = ((kHermite_13E9EE4 * xmm2 + kHermite_13E9EE8 * xmm4) + kHermite_13E9EEC * xmm6) + xmm7;
    const float tail = static_cast<float> (static_cast<double> (xmm8) * xmm0);

    // Degree 0 constant term (VA 0x0067B412..0x0067B442)
    return ((kHermite_13E9EF0 * xmm1 + kHermite_13E9EF4 * xmm3) + kHermite_13E9EF8 * xmm5) + tail;
}

float FLResamplingQualityAudioProcessor::evaluateFLSincSSE (const RationalResamplerState& state,
                                                            int64_t baseIndex,
                                                            double frac,
                                                            const FLSincKernelConfig& cfg) const noexcept
{
    using namespace FLEngineConstants;

    // 1. ippsThreshold_LT_64f_I clamp @ VA 0x0067CA78 (0x3ABEF2D0F5DA7DD9 = 1.0e-25)
    if (frac < kMinSincFrac)
        frac = kMinSincFrac;

    // 2. Angle preparation @ VA 0x0067C53E..0x0067C5CE + ippsSin_64f_A50 @ VA 0x0067CAC4
    const double a0 = kHalfPi - frac * cfg.deltaW;
    const double a1 = a0 - cfg.deltaW;
    const double a2 = frac * cfg.deltaS;
    const double a3 = a2 + cfg.deltaS;

    const double r0 = std::sin (a0);
    const double r1 = std::sin (a1);
    const double r2 = std::sin (a2);
    const double r3 = std::sin (a3);

    // 3. Two-step Chebyshev recurrence seed @ VA 0x0067C804..0x0067C89E
    const double w_p1_d = cfg.twoCosDeltaW * r0 - r1;
    const double w_p2_d = cfg.twoCosDeltaW * w_p1_d - r0;
    const double s_p1_d = cfg.twoCosDeltaS * r2 - r3;
    const double s_p2_d = cfg.twoCosDeltaS * s_p1_d - r2;

    // [rbx+00h..0Ch] = xmm5, [rbx+10h..1Ch] = xmm6
    __m128 xmm5 = _mm_set_ps (static_cast<float> (r1),
                              static_cast<float> (w_p2_d),
                              static_cast<float> (r3),
                              static_cast<float> (s_p2_d));

    __m128 xmm6 = _mm_set_ps (static_cast<float> (r0),
                              static_cast<float> (w_p1_d),
                              static_cast<float> (r2),
                              static_cast<float> (s_p1_d));

    // [rbx+20h..2Ch] = xmm8 = [2*cos(deltaS), 2*cos(deltaS), 2*cos(deltaW), 2*cos(deltaW)]
    const float twoCosSF = static_cast<float> (cfg.twoCosDeltaS);
    const float twoCosWF = static_cast<float> (cfg.twoCosDeltaW);
    const __m128 xmm8 = _mm_set_ps (twoCosWF, twoCosWF, twoCosSF, twoCosSF);

    // [rbx+30h..3Ch] = xmm4 = [frac*PI - PI, frac*PI, frac*PI - 2*PI, frac*PI + PI] @ VA 0x0067C8A2..0x0067C8ED
    const double fp = frac * kPi;
    __m128 xmm4 = _mm_set_ps (static_cast<float> (fp + kPi),
                              static_cast<float> (fp - 2.0 * kPi),
                              static_cast<float> (fp),
                              static_cast<float> (fp - kPi));

    // Constant SSE registers @ VA 0x0067C9EC..0x0067CA50
    const __m128 xmm9  = _mm_set_ps (0.0f, 0.0f, kWinC0, kWinC0);
    const __m128 xmm10 = _mm_set_ps (1.0f, 1.0f, kWinC2, kWinC2);
    const __m128 xmm11 = _mm_set_ps (kPlusTwoPiF, kMinusTwoPiF, kPlusTwoPiF, kMinusTwoPiF);
    __m128 xmm3 = _mm_setzero_ps();

    // 4. Inner SSE loop @ VA 0x0067C6C0..0x0067C780 (processes 4 taps per iteration: 2 right, 2 left)
    const int numLoops = cfg.halfTaps >> 1;
    for (int k = 0; k < numLoops; ++k)
    {
        __m128 xmm7 = _mm_rcp_ps (xmm4);
        __m128 xmm1 = _mm_mul_ps (xmm4, xmm7);
        xmm1 = _mm_mul_ps (xmm1, xmm7);
        __m128 xmm0 = xmm6;
        xmm6 = xmm5;
        xmm5 = _mm_sub_ps (_mm_mul_ps (xmm5, xmm8), xmm0);
        xmm7 = _mm_sub_ps (_mm_add_ps (xmm7, xmm7), xmm1);

        __m128 xmm13 = _mm_movehl_ps (xmm0, xmm0);
        __m128 xmm12 = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (xmm10, xmm13), xmm13), xmm13), xmm9);
        xmm4 = _mm_add_ps (xmm4, xmm11);
        xmm0 = _mm_mul_ps (_mm_mul_ps (xmm0, xmm7), xmm12);
        xmm7 = _mm_movehl_ps (xmm7, xmm7);

        const int64_t twoK = static_cast<int64_t> (k * 2);
        const float sPos1 = state.readAt (baseIndex + twoK + 1);
        const float sPos2 = state.readAt (baseIndex + twoK + 2);
        const float sNeg1 = state.readAt (baseIndex - twoK - 1);
        const float sNeg0 = state.readAt (baseIndex - twoK);

        xmm1 = _mm_set_ps (sNeg0, sNeg0, sPos1, sPos1);
        const __m128 xmm14 = _mm_set_ps (sNeg1, sNeg1, sPos2, sPos2);

        xmm0 = _mm_shuffle_ps (xmm0, xmm0, 0x50);
        xmm3 = _mm_add_ps (xmm3, _mm_mul_ps (xmm0, xmm1));

        xmm0 = xmm6;
        xmm13 = _mm_movehl_ps (xmm6, xmm6);
        xmm12 = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (xmm10, xmm13), xmm13), xmm13), xmm9);
        xmm6 = xmm5;
        xmm5 = _mm_sub_ps (_mm_mul_ps (xmm5, xmm8), xmm0);
        xmm0 = _mm_mul_ps (_mm_mul_ps (xmm0, xmm7), xmm12);
        xmm0 = _mm_shuffle_ps (xmm0, xmm0, 0x50);
        xmm3 = _mm_add_ps (xmm3, _mm_mul_ps (xmm0, xmm14));
    }

    // Horizontal reduction + * 0.5f @ VA 0x0067C774..0x0067C780
    __m128 xmm0 = _mm_add_ps (_mm_movehl_ps (xmm3, xmm3), xmm3);
    return _mm_cvtss_f32 (xmm0) * kHalfF;
}

float FLResamplingQualityAudioProcessor::evaluateSample (const RationalResamplerState& state,
                                                         int64_t baseIndex,
                                                         double frac,
                                                         QualityMode mode,
                                                         bool isDownStage) const noexcept
{
    switch (mode)
    {
        case Linear2Point:
            return evaluateLinear2 (state, baseIndex, frac);

        case Hermite6Point:
            return evaluateHermite6 (state, baseIndex, frac);

        case Sinc16Point:
        case Sinc24Point:
        case Sinc32Point:
        case Sinc64Point:
        case Sinc128Point:
        case Sinc256Point:
        case Sinc512Point:
        default:
        {
            const auto& cfg = isDownStage
                ? downSincConfigs[static_cast<size_t> (mode)]
                : upSincConfigs[static_cast<size_t> (mode)];
            return evaluateFLSincSSE (state, baseIndex, frac, cfg);
        }
    }
}

void FLResamplingQualityAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                      juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples <= 0)
        return;

    const int requestedIdx = (qualityParam != nullptr)
        ? juce::jlimit (0, static_cast<int> (NumQualityModes) - 1, static_cast<int> (std::lround (qualityParam->load())))
        : static_cast<int> (Sinc512Point);

    const auto requestedMode = static_cast<QualityMode> (requestedIdx);
    if (requestedMode != activeMode)
        activeMode = requestedMode;

    constexpr int64_t kFixedLookaheadHalfTaps = 256;

    // Exact rational round-trip integer ratios:
    // Stage 1 (8 -> 7 downsampling): position(m) = (m * 8) / 7 + ((m * 8) % 7) / 7.0
    // Stage 2 (7 -> 8 upsampling):   position(n) = (n * 7) / 8 + ((n * 7) % 8) / 8.0
    constexpr double invSeven = 1.0 / 7.0;
    constexpr double invEight = 1.0 / 8.0;

    const int numCh = juce::jmin (totalNumInputChannels, kMaxChannels);

    for (int chIdx = 0; chIdx < numCh; ++chIdx)
    {
        float* channelData = buffer.getWritePointer (chIdx);
        auto& pipe = channels[static_cast<size_t> (chIdx)];

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. Push incoming sample into Stage 1 (8 -> 7) circular buffer
            const float inSample = std::isfinite (channelData[i]) ? channelData[i] : 0.0f;
            pipe.downStage.pushSample (inSample);

            // 2. Produce intermediate downsampled samples using FLEngine_x64.dll kernel (step = 8/7 > 1.0)
            while ((pipe.downStage.stepIndex * 8) / 7 + kFixedLookaheadHalfTaps < pipe.downStage.totalWritten)
            {
                const int64_t downNum   = pipe.downStage.stepIndex * 8;
                const int64_t downBase  = downNum / 7;
                const double  downFrac  = static_cast<double> (downNum % 7) * invSeven;

                const float midSample = evaluateSample (pipe.downStage, downBase, downFrac, activeMode, true);
                ++pipe.downStage.stepIndex;
                pipe.upStage.pushSample (std::isfinite (midSample) ? midSample : 0.0f);
            }

            // 3. Produce 1 output sample from Stage 2 (7 -> 8) using FLEngine_x64.dll kernel (step = 7/8 <= 1.0)
            const int64_t upNum  = pipe.upStage.stepIndex * 7;
            const int64_t upBase = upNum / 8;
            const double  upFrac = static_cast<double> (upNum % 8) * invEight;

            const float outSample = evaluateSample (pipe.upStage, upBase, upFrac, activeMode, false);
            ++pipe.upStage.stepIndex;
            channelData[i] = std::isfinite (outSample) ? outSample : 0.0f;
        }
    }
}

bool FLResamplingQualityAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FLResamplingQualityAudioProcessor::createEditor()
{
    return new FLResamplingQualityAudioProcessorEditor (*this);
}

void FLResamplingQualityAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FLResamplingQualityAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLResamplingQualityAudioProcessor();
}


