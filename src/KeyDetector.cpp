#include "KeyDetector.h"
#include <juce_dsp/juce_dsp.h>

namespace km
{

KeyInfo detectKey (const juce::AudioBuffer<float>& audio, double sr)
{
    KeyInfo key;
    const int n = audio.getNumSamples();
    if (n < (int) (sr * 1.0) || sr <= 0)
        return key;

    // ~5 Hz resolution regardless of the sample rate
    const int order = juce::jlimit (12, 15, (int) std::ceil (std::log2 (sr / 5.5)));
    const int size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) size), frame ((size_t) size * 2);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));

    // up to ~240 frames spread over the clip
    const int hop = juce::jmax (size / 2, (n - size) / 240);
    const int nc = audio.getNumChannels();
    double chroma[12] {};
    const int lo = (int) std::ceil (55.0 * size / sr), hi = juce::jmin (size / 2 - 2, (int) (2000.0 * size / sr));

    for (int start = 0; start + size <= n; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < size; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < nc; ++ch) v += audio.getSample (ch, start + i);
            frame[(size_t) i] = v * window[(size_t) i] / (float) juce::jmax (1, nc);
        }
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        double frameChroma[12] {};
        double total = 0.0;
        for (int b = lo; b <= hi; ++b)
        {
            const float m = frame[(size_t) b];
            // spectral peaks only: tones count, the broadband wash of drums mostly doesn't
            if (m <= frame[(size_t) b - 1] || m < frame[(size_t) b + 1]) continue;
            // parabolic interpolation for the true peak frequency
            const float a = frame[(size_t) b - 1], c = frame[(size_t) b + 1];
            const float denom = a - 2.0f * m + c;
            const double delta = std::abs (denom) > 1.0e-12f ? 0.5 * (a - c) / denom : 0.0;
            const double freq = (b + delta) * sr / size;
            const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            const double w = std::sqrt ((double) m);
            frameChroma[pc] += w;
            total += w;
        }
        if (total <= 0.0) continue;
        for (int k = 0; k < 12; ++k)
            chroma[k] += frameChroma[k] / total;   // every frame counts equally
    }

    // Krumhansl-Kessler key profiles
    static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto correlate = [&] (const double* profile, int tonic)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(i + tonic) % 12]; my += profile[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(i + tonic) % 12] - mx, y = profile[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2.0, second = -2.0;
    int bestTonic = 0;
    bool bestMinor = false;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const double r = correlate (m == 1 ? minor : major, t);
            if (r > best) { second = best; best = r; bestTonic = t; bestMinor = m == 1; }
            else if (r > second) second = r;
        }

    key.found = best > 0.45;
    key.tonic = bestTonic;
    key.minor = bestMinor;
    // how clearly the best key beats the runner-up (0.3 apart is very clear)
    key.confidence = (float) juce::jlimit (0.0, 1.0, (best - second) / 0.3);
    return key;
}

} // namespace km
