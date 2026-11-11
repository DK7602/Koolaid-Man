#include "Mastering.h"
#include <juce_dsp/juce_dsp.h>
#include <deque>

namespace km
{

namespace
{
    /** Per base-rate sample: the largest |x| among the 4x oversampled points and channels. */
    std::vector<float> truePeakEnvelope (const juce::AudioBuffer<float>& b, int start, int n)
    {
        const int numCh = b.getNumChannels();
        juce::dsp::Oversampling<float> os ((size_t) numCh, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        const int block = 1024;
        os.initProcessing ((size_t) block);
        const int lat = (int) std::lround (os.getLatencyInSamples());

        juce::AudioBuffer<float> in (numCh, n + lat);
        in.clear();
        for (int ch = 0; ch < numCh; ++ch)
            in.copyFrom (ch, 0, b, ch, start, n);

        std::vector<float> env ((size_t) n, 0.0f);
        for (int s0 = 0; s0 < n + lat; s0 += block)
        {
            const int len = juce::jmin (block, n + lat - s0);
            juce::dsp::AudioBlock<float> blk (in.getArrayOfWritePointers(), (size_t) numCh, (size_t) s0, (size_t) len);
            auto up = os.processSamplesUp (blk);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* d = up.getChannelPointer ((size_t) ch);
                for (int i = 0; i < len; ++i)
                {
                    const int dst = s0 + i - lat;
                    if (dst < 0 || dst >= n) continue;
                    float m = std::abs (b.getSample (ch, start + dst));
                    for (int k = 0; k < 4; ++k) m = juce::jmax (m, std::abs (d[i * 4 + k]));
                    env[(size_t) dst] = juce::jmax (env[(size_t) dst], m);
                }
            }
        }
        return env;
    }
}

float measureTruePeak (const juce::AudioBuffer<float>& b, int start, int n, double)
{
    if (n <= 0) return 0.0f;
    const auto env = truePeakEnvelope (b, start, n);
    return *std::max_element (env.begin(), env.end());
}

std::vector<float> truePeakGain (const juce::AudioBuffer<float>& b, int start, int n, double sr, float ceilingGain,
                                 const juce::AudioBuffer<float>* reference, int refStart)
{
    if (n <= 0) return {};
    const float ceiling = ceilingGain * 0.995f;   // a hair of headroom for the reconstruction filter
    const auto env = truePeakEnvelope (b, start, n);
    // never limit below what the untouched audio already had at that moment, so a hot master
    // isn't squashed where nothing was changed
    std::vector<float> floorEnv;
    if (reference != nullptr)
        floorEnv = truePeakEnvelope (*reference, refStart, n);

    std::vector<float> req ((size_t) n);
    bool anyOver = false;
    for (int i = 0; i < n; ++i)
    {
        const float allowed = floorEnv.empty() ? ceiling : juce::jmax (ceiling, floorEnv[(size_t) i] * 1.0001f);
        req[(size_t) i] = env[(size_t) i] > allowed ? allowed / env[(size_t) i] : 1.0f;
        anyOver |= req[(size_t) i] < 1.0f;
    }
    if (! anyOver) return {};

    const int look = juce::jmax (1, (int) std::lround (0.0015 * sr));

    // hold: the smallest required gain within the next 'look' samples (sliding minimum)
    std::vector<float> hold ((size_t) n);
    std::deque<int> dq;
    for (int i = n - 1; i >= 0; --i)
    {
        while (! dq.empty() && req[(size_t) dq.back()] >= req[(size_t) i]) dq.pop_back();
        dq.push_back (i);
        while (dq.front() > i + look - 1) dq.pop_front();
        hold[(size_t) i] = req[(size_t) dq.front()];
    }

    // smooth release (60 ms), instant attack
    const float rel = 1.0f - std::exp (-1.0f / (float) (0.06 * sr));
    std::vector<float> g ((size_t) n);
    float prev = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        prev = juce::jmin (hold[(size_t) i], prev + (1.0f - prev) * rel);
        g[(size_t) i] = prev;
    }

    // boxcar over the lookahead window: smooth attack that still lands under every peak
    std::vector<float> out ((size_t) n);
    double acc = 0.0;
    // start the window at the first sample's gain: a peak right at the start of the
    // section (e.g. an Impact on the downbeat) can't be anticipated from outside it
    std::vector<float> win ((size_t) look, g[0]);
    acc = (double) g[0] * look;
    for (int i = 0; i < n; ++i)
    {
        acc += g[(size_t) i] - win[(size_t) (i % look)];
        win[(size_t) (i % look)] = g[(size_t) i];
        out[(size_t) i] = (float) (acc / look);
    }
    return out;
}

float applyGain (juce::AudioBuffer<float>& b, int start, const std::vector<float>& gain)
{
    float minGain = 1.0f;
    for (size_t i = 0; i < gain.size(); ++i)
    {
        minGain = juce::jmin (minGain, gain[i]);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.setSample (ch, start + (int) i, b.getSample (ch, start + (int) i) * gain[i]);
    }
    return juce::Decibels::gainToDecibels (minGain);
}

float truePeakLimit (juce::AudioBuffer<float>& b, int start, int n, double sr, float ceilingGain)
{
    return applyGain (b, start, truePeakGain (b, start, n, sr, ceilingGain));
}

void ditherTo16Bit (juce::AudioBuffer<float>& b, juce::uint32 seed)
{
    juce::Random rng ((juce::int64) seed);
    const float scale = 32767.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        float* d = b.getWritePointer (ch);
        float err = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float shaped = d[i] - err;                                   // first-order noise shaping
            const float tpdf = (rng.nextFloat() - rng.nextFloat()) / scale;    // +-1 LSB triangular
            float q = std::round ((shaped + tpdf) * scale) / scale;
            q = juce::jlimit (-1.0f, 1.0f, q);
            err = q - shaped;
            d[i] = q;
        }
    }
}

} // namespace km
