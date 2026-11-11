#include "DrumDetector.h"

namespace km
{

namespace
{
    using Buffer = juce::AudioBuffer<float>;

    std::vector<float> toMono (const Buffer& b)
    {
        std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
        const int nc = juce::jmax (1, b.getNumChannels());
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* p = b.getReadPointer (c);
            for (size_t i = 0; i < m.size(); ++i)
                m[i] += p[i] / (float) nc;
        }
        return m;
    }

    std::vector<float> filtered (const std::vector<float>& in, std::initializer_list<juce::IIRCoefficients> stages)
    {
        std::vector<float> out (in);
        for (auto& c : stages)
        {
            juce::IIRFilter f;
            f.setCoefficients (c);
            f.processSamples (out.data(), (int) out.size());
        }
        return out;
    }

    /** Log energy per hop. */
    std::vector<float> hopEnergyDb (const std::vector<float>& x, int hop)
    {
        const size_t n = x.size() / (size_t) hop;
        std::vector<float> e (n);
        for (size_t i = 0; i < n; ++i)
        {
            double s = 0;
            for (int k = 0; k < hop; ++k)
            {
                const float v = x[i * (size_t) hop + (size_t) k];
                s += (double) v * v;
            }
            e[i] = (float) (10.0 * std::log10 (s / hop + 1.0e-12));
        }
        return e;
    }

    struct Onset { int frame; float strength; };

    std::vector<Onset> pickOnsets (const std::vector<float>& eDb, float sens, int minGapFrames, float floorDb)
    {
        const int n = (int) eDb.size();
        std::vector<float> flux ((size_t) n, 0.0f);
        for (int i = 2; i < n; ++i)
            flux[(size_t) i] = juce::jmax (0.0f, eDb[(size_t) i] - juce::jmax (eDb[(size_t) i - 1], eDb[(size_t) i - 2]) * 0.5f
                                                               - juce::jmin (eDb[(size_t) i - 1], eDb[(size_t) i - 2]) * 0.5f);

        // threshold = local mean of flux + a delta that shrinks as sensitivity rises
        const float delta = juce::jmap (sens, 0.0f, 1.0f, 7.5f, 1.5f);
        const int w = 40;
        std::vector<Onset> out;
        int last = -minGapFrames - 1;
        for (int i = 2; i < n - 1; ++i)
        {
            const float f = flux[(size_t) i];
            if (f <= 0.0f || f < flux[(size_t) i - 1] || f < flux[(size_t) i + 1])
                continue;
            double mean = 0; int cnt = 0;
            for (int k = juce::jmax (0, i - w); k < juce::jmin (n, i + w); ++k) { mean += flux[(size_t) k]; ++cnt; }
            mean /= juce::jmax (1, cnt);
            if (f < (float) mean + delta) continue;
            const int peakFrame = juce::jmin (n - 1, i + 2);
            if (eDb[(size_t) peakFrame] < floorDb) continue;
            if (i - last < minGapFrames)
            {
                if (! out.empty() && f > out.back().strength) out.back() = { i, f };
                continue;
            }
            out.push_back ({ i, f });
            last = i;
        }
        return out;
    }

    float meanDb (const std::vector<float>& eDb, int from, int to)
    {
        from = juce::jlimit (0, (int) eDb.size(), from);
        to = juce::jlimit (from, (int) eDb.size(), to);
        if (to <= from) return -120.0f;
        double s = 0;
        for (int i = from; i < to; ++i) s += std::pow (10.0, eDb[(size_t) i] / 10.0);
        return (float) (10.0 * std::log10 (s / (to - from) + 1.0e-12));
    }

    float maxOf (const std::vector<float>& v)
    {
        float m = -200.0f;
        for (auto x : v) m = juce::jmax (m, x);
        return m;
    }

    Buffer extract (const Buffer& src, juce::int64 start, int length, std::initializer_list<juce::IIRCoefficients> stages, int fadeOut)
    {
        start = juce::jlimit<juce::int64> (0, src.getNumSamples() - 1, start);
        length = (int) juce::jmin<juce::int64> (length, src.getNumSamples() - start);
        Buffer out (2, juce::jmax (1, length));
        out.clear();
        for (int c = 0; c < 2; ++c)
        {
            out.copyFrom (c, 0, src, juce::jmin (c, src.getNumChannels() - 1), (int) start, length);
            for (auto& co : stages)
            {
                juce::IIRFilter f; f.setCoefficients (co);
                f.processSamples (out.getWritePointer (c), length);
            }
        }
        const int fi = juce::jmin (length / 4, 24);
        if (fi > 0) out.applyGainRamp (0, fi, 0.0f, 1.0f);
        fadeOut = juce::jmin (fadeOut, length / 2);
        if (fadeOut > 0) out.applyGainRamp (length - fadeOut, fadeOut, 1.0f, 0.0f);
        const float mag = out.getMagnitude (0, length);
        if (mag > 1.0e-5f) out.applyGain (0.9f / mag);
        return out;
    }

    float detectPitch (const std::vector<float>& lowBand, juce::int64 start, double sr)
    {
        const int from = (int) juce::jmin<juce::int64> ((juce::int64) lowBand.size() - 1, start + (juce::int64) (0.04 * sr));
        const int len = (int) juce::jmin<juce::int64> ((juce::int64) (0.16 * sr), (juce::int64) lowBand.size() - from);
        const int minLag = (int) (sr / 160.0), maxLag = (int) (sr / 28.0);
        if (len < maxLag * 2) return 0.0f;
        double best = 0; int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double s = 0, e1 = 0, e2 = 0;
            for (int i = 0; i + lag < len; i += 2)
            {
                const double a = lowBand[(size_t) (from + i)], b = lowBand[(size_t) (from + i + lag)];
                s += a * b; e1 += a * a; e2 += b * b;
            }
            const double r = s / std::sqrt (e1 * e2 + 1.0e-20);
            if (r > best) { best = r; bestLag = lag; }
        }
        return best > 0.6 && bestLag > 0 ? (float) (sr / bestLag) : 0.0f;
    }
}

DrumAnalysis analyseDrums (const Buffer& audio, double sr, double samplesPerBeat, double beatsPerBar,
                           float sensSnare, float sens808, float sensHats)
{
    DrumAnalysis result;
    if (audio.getNumSamples() < (int) (sr * 0.25) || sr <= 0)
        return result;

    const int hop = juce::jmax (64, (int) std::round (sr / 200.0)); // 5 ms hops
    const auto mono = toMono (audio);

    const auto low = filtered (mono, { juce::IIRCoefficients::makeHighPass (sr, 28.0), juce::IIRCoefficients::makeLowPass (sr, 110.0),
                                       juce::IIRCoefficients::makeLowPass (sr, 110.0) });
    const auto body = filtered (mono, { juce::IIRCoefficients::makeBandPass (sr, 210.0, 1.2) });
    const auto noise = filtered (mono, { juce::IIRCoefficients::makeHighPass (sr, 1800.0), juce::IIRCoefficients::makeLowPass (sr, 8000.0) });
    // capped at 16 kHz so detection doesn't depend on the sample rate (at 96 kHz "everything
    // above 7 kHz" would include ultrasonic content up to 48 kHz)
    const auto high = filtered (mono, { juce::IIRCoefficients::makeHighPass (sr, 7000.0), juce::IIRCoefficients::makeHighPass (sr, 7000.0),
                                        juce::IIRCoefficients::makeLowPass (sr, juce::jmin (16000.0, sr * 0.45)) });

    const auto eLow = hopEnergyDb (low, hop);
    const auto eBody = hopEnergyDb (body, hop);
    const auto eNoise = hopEnergyDb (noise, hop);
    const auto eHigh = hopEnergyDb (high, hop);

    const auto ms = [&] (double t) { return juce::jmax (1, (int) std::round (t * sr / 1000.0 / hop)); };
    const auto beatPhase = [&] (int frame)
    {
        const double beats = (double) frame * hop / samplesPerBeat;
        const double inBar = std::fmod (beats, beatsPerBar);
        return inBar;
    };

    //------------------------------------------------------------------ snare
    {
        // the 1.8-8 kHz "crack" is the snare's signature; the body band is too easily
        // masked by pads and bass, so it is only used as a feature below
        const auto onsets = pickOnsets (eNoise, sensSnare, ms (90), maxOf (eNoise) - 32.0f);
        struct Cand { int frame; float score; };
        std::vector<Cand> cands;
        float loudest = -200.0f;
        for (auto& o : onsets)
            loudest = juce::jmax (loudest, meanDb (eNoise, o.frame, o.frame + ms (70)));

        // how strict the spectral checks are (more sensitivity = looser)
        const float slack = juce::jmap (sensSnare, 0.0f, 1.0f, -3.0f, 5.0f);
        for (auto& o : onsets)
        {
            const float n = meanDb (eNoise, o.frame, o.frame + ms (70));   // 1.8-8 kHz crack
            const float h = meanDb (eHigh, o.frame, o.frame + ms (70));    // > 7 kHz (hats live here)
            const float b = meanDb (eBody, o.frame, o.frame + ms (60));    // ~200 Hz body
            const float l = meanDb (eLow, o.frame, o.frame + ms (60));     // kick / 808

            const bool loudEnough = n > loudest - 10.0f - slack;
            const bool notAHat = (n - h) > -6.0f - slack * 0.5f;
            const bool notAKick = (n - l) > -12.0f - slack;
            if (! (loudEnough && notAHat && notAKick))
                continue;

            const double phase = beatPhase (o.frame);
            const double nearest = std::round (phase);
            const bool onBeat = std::abs (phase - nearest) < 0.12;
            const int beatIdx = ((int) nearest) % juce::jmax (1, (int) std::round (beatsPerBar));
            const bool backbeat = onBeat && (beatIdx % 2 == 1);
            float score = (n - loudest) + 0.3f * (b - l) + 0.3f * (n - h);
            if (backbeat) score += 6.0f;
            else if (! onBeat) score -= 4.0f;
            cands.push_back ({ o.frame, score });
        }
        float bestScore = -1.0e9f; int bestFrame = -1; int count = 0;
        for (auto& c : cands)
            if (c.score > bestScore) { bestScore = c.score; bestFrame = c.frame; }
        for (auto& c : cands)
        {
            if (c.score > bestScore - 10.0f)
            {
                ++count;
                result.snare.onsets.push_back ((juce::int64) c.frame * hop);
            }
        }
        const double bars = audio.getNumSamples() / (samplesPerBeat * beatsPerBar);
        const double expected = juce::jmax (1.0, bars * 2.0);
        result.snare.found = count >= 2 && bestFrame >= 0;
        result.snare.confidence = result.snare.found ? (float) juce::jlimit (0.15, 1.0, count / expected) : 0.0f;
        if (result.snare.found)
            result.snare.oneShot = extract (audio, (juce::int64) bestFrame * hop - hop, (int) (0.24 * sr),
                                            { juce::IIRCoefficients::makeHighPass (sr, 140.0) }, (int) (0.08 * sr));
    }

    //------------------------------------------------------------------ 808 / kick
    {
        const auto onsets = pickOnsets (eLow, sens808, ms (140), maxOf (eLow) - 26.0f);
        float bestScore = -1.0e9f; int bestFrame = -1; int count = 0; float bestSustain = 0;
        for (auto& o : onsets)
        {
            const float peak = meanDb (eLow, o.frame, o.frame + ms (30));
            const float tail = meanDb (eLow, o.frame + ms (150), o.frame + ms (260));
            const float sustain = juce::jlimit (0.0f, 1.0f, 1.0f - (peak - tail) / 24.0f);
            const float score = o.strength + sustain * 10.0f + (peak - maxOf (eLow)) * 0.3f;
            ++count;
            result.bass.onsets.push_back ((juce::int64) o.frame * hop);
            if (score > bestScore) { bestScore = score; bestFrame = o.frame; bestSustain = sustain; }
        }
        result.bass.found = count >= 1 && bestFrame >= 0;
        if (result.bass.found)
        {
            const double bars = audio.getNumSamples() / (samplesPerBeat * beatsPerBar);
            result.bass.confidence = (float) juce::jlimit (0.15, 1.0, 0.4 * bestSustain + 0.6 * juce::jmin (1.0, count / juce::jmax (1.0, bars * 1.5)));
            // one-shot length: until the next low onset (max 1 beat), at least 150 ms
            juce::int64 start = (juce::int64) bestFrame * hop - hop;
            juce::int64 next = audio.getNumSamples();
            for (auto s : result.bass.onsets)
                if (s > start + hop * 4) { next = s; break; }
            const int len = (int) juce::jlimit<juce::int64> ((juce::int64) (0.15 * sr), (juce::int64) samplesPerBeat, next - start);
            result.bass.oneShot = extract (audio, start, len, { juce::IIRCoefficients::makeLowPass (sr, 180.0),
                                                                juce::IIRCoefficients::makeLowPass (sr, 180.0) }, (int) (0.03 * sr));
            result.bass.pitchHz = detectPitch (low, start, sr);
        }
    }

    //------------------------------------------------------------------ hats
    {
        const auto onsets = pickOnsets (eHigh, sensHats, ms (45), maxOf (eHigh) - 30.0f);
        std::vector<std::pair<float, int>> shortHits;
        for (auto& o : onsets)
        {
            const float peak = meanDb (eHigh, o.frame, o.frame + ms (15));
            const float tail = meanDb (eHigh, o.frame + ms (70), o.frame + ms (120));
            result.hats.onsets.push_back ((juce::int64) o.frame * hop);
            if (peak - tail > 8.0f)
                shortHits.push_back ({ peak, o.frame });
        }
        const int count = (int) result.hats.onsets.size();
        const double bars = audio.getNumSamples() / (samplesPerBeat * beatsPerBar);
        result.hats.found = count >= 4 && ! shortHits.empty();
        if (result.hats.found)
        {
            result.hats.confidence = (float) juce::jlimit (0.15, 1.0, count / juce::jmax (1.0, bars * beatsPerBar * 2.0));
            std::sort (shortHits.begin(), shortHits.end());
            const int frame = shortHits[shortHits.size() * 3 / 4].second;   // a loud-ish, short hat
            result.hats.oneShot = extract (audio, (juce::int64) frame * hop - hop / 2, (int) (0.09 * sr),
                                           { juce::IIRCoefficients::makeHighPass (sr, 5500.0),
                                             juce::IIRCoefficients::makeHighPass (sr, 5500.0) }, (int) (0.04 * sr));
        }
    }

    return result;
}

//==============================================================================
juce::AudioBuffer<float> synthSnare (double sr)
{
    const int n = (int) (0.22 * sr);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (1234);
    juce::IIRFilter hp; hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 900.0));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const float tone = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * (190.0 - 40.0 * t) * t) * std::exp (-t * 28.0));
        const float nz = hp.processSingleSampleRaw (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * 16.0);
        const float v = 0.55f * tone + 0.6f * nz;
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    b.applyGain (0.9f / juce::jmax (1.0e-5f, b.getMagnitude (0, n)));
    return b;
}

juce::AudioBuffer<float> synthHat (double sr, bool open)
{
    const int n = (int) ((open ? 0.35 : 0.07) * sr);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (99);
    juce::IIRFilter hp1, hp2;
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7500.0));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7500.0));
    const double decay = open ? 9.0 : 55.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const float v = hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (rng.nextFloat() * 2.0f - 1.0f)) * (float) std::exp (-t * decay);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    b.applyGain (0.8f / juce::jmax (1.0e-5f, b.getMagnitude (0, n)));
    return b;
}

} // namespace km
