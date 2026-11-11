#include "DrumDetector.h"
#include <juce_dsp/juce_dsp.h>

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

    /** Filters and measures in one pass, without keeping the filtered signal (saves a
        full-length copy per band when a whole song is analysed). */
    std::vector<float> bandEnergyDb (const std::vector<float>& x, std::initializer_list<juce::IIRCoefficients> stages, int hop)
    {
        std::vector<juce::IIRFilter> filters (stages.size());
        size_t k = 0;
        for (auto& c : stages) filters[k++].setCoefficients (c);
        const size_t n = x.size() / (size_t) hop;
        std::vector<float> e (n);
        for (size_t i = 0; i < n; ++i)
        {
            double s = 0;
            for (int j = 0; j < hop; ++j)
            {
                float v = x[i * (size_t) hop + (size_t) j];
                for (auto& f : filters) v = f.processSingleSampleRaw (v);
                s += (double) v * v;
            }
            e[i] = (float) (10.0 * std::log10 (s / hop + 1.0e-12));
        }
        return e;
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
        start = std::max ((juce::int64) 0, std::min ((juce::int64) src.getNumSamples() - 1, start));
        length = (int) std::min ((juce::int64) length, (juce::int64) src.getNumSamples() - start);
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

    /** Kick finder. A kick's pitch sweep puts new energy across the whole low end at once;
        a bass note (or the bass coming back after a kick on a loud master) adds it at one pitch.
        So: a short STFT of the low end (decimated to ~1 kHz, 64 ms window, 5 ms hop), an onset
        function that rewards broad rises, adaptive peak picking, then exact timing from a fast
        envelope (the steepest rise before the hit's peak). Returns sample positions. */
    struct KickHit { juce::int64 pos; float strength; float lowJumpDb; };

    std::vector<KickHit> findKicks (const std::vector<float>& mono, double sr, float sens)
    {
        const int dec = juce::jmax (1, (int) std::round (sr / 1000.0));
        const double fs = sr / dec;
        std::vector<float> lo;
        lo.reserve (mono.size() / (size_t) dec + 1);
        {
            juce::IIRFilter hpf, lp1, lp2;
            hpf.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 25.0));
            lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 220.0, 0.54));
            lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 220.0, 1.31));
            for (size_t i = 0; i < mono.size(); ++i)
            {
                const float v = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (hpf.processSingleSampleRaw (mono[i])));
                if (i % (size_t) dec == 0) lo.push_back (v);
            }
        }
        constexpr int order = 7, fftN = 1 << order, win = 64;
        // a window of silence in front, so a kick on the very first sample (clips usually start
        // on a bar line) is seen arriving like any other
        lo.insert (lo.begin(), (size_t) win, 0.0f);
        const int hopL = juce::jmax (1, (int) std::round (fs * 0.005));
        const int nFrames = ((int) lo.size() - win) / hopL;
        if (nFrames < 8) return {};
        juce::dsp::FFT fft (order);
        std::vector<float> w ((size_t) win), buf ((size_t) fftN * 2);
        for (int k = 0; k < win; ++k) w[(size_t) k] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) k / (float) (win - 1));
        const double binHz = fs / fftN;
        const int b0 = juce::jmax (1, (int) std::round (35.0 / binHz)), b1 = juce::jmin (fftN / 2 - 1, (int) std::round (180.0 / binHz));
        const int nb = b1 - b0 + 1;
        std::vector<float> p1 ((size_t) nb, -120.0f), p2 ((size_t) nb, -120.0f), odf ((size_t) nFrames, 0.0f), lvl ((size_t) nFrames, -120.0f);
        for (int f = 0; f < nFrames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int k = 0; k < win; ++k) buf[(size_t) k] = lo[(size_t) (f * hopL + k)] * w[(size_t) k];
            fft.performFrequencyOnlyForwardTransform (buf.data());
            float sum = 0.0f, energy = 0.0f;
            int spread = 0;
            for (int b = b0; b <= b1; ++b)
            {
                const float mag = buf[(size_t) b];
                energy += mag * mag;
                const float L = 20.0f * std::log10 (mag + 1.0e-9f);
                const float rise = L - juce::jmax (p1[(size_t) (b - b0)], p2[(size_t) (b - b0)]);
                if (f >= 2 && rise > 0.0f) { sum += rise; if (rise > 6.0f) ++spread; }
                p2[(size_t) (b - b0)] = p1[(size_t) (b - b0)];
                p1[(size_t) (b - b0)] = L;
            }
            odf[(size_t) f] = sum * (float) spread / (float) nb;   // broad rises count, narrow ones barely
            lvl[(size_t) f] = 10.0f * std::log10 (energy + 1.0e-12f);
        }
        float loudest = -120.0f;
        double odfMean = 0.0;
        for (int f = 0; f < nFrames; ++f) { loudest = juce::jmax (loudest, lvl[(size_t) f]); odfMean += odf[(size_t) f]; }
        odfMean /= nFrames;

        // fast envelope for exact timing (instant attack, 15 ms release)
        std::vector<float> env (lo.size());
        {
            const float rel = std::exp (-1.0f / (float) (0.015 * fs));
            float e = 0.0f;
            for (size_t i = 0; i < lo.size(); ++i) { const float a = std::abs (lo[i]); e = a > e ? a : e * rel; env[i] = e; }
        }

        const float k = juce::jmap (sens, 0.0f, 1.0f, 2.6f, 1.3f);           // more sensitivity = lower threshold
        const int gap = juce::jmax (1, (int) std::round (0.06 * fs / hopL));    // 60 ms between kicks at least
        const int wl = juce::jmax (4, (int) std::round (0.25 * fs / hopL));     // +-250 ms for the local level
        std::vector<KickHit> out;
        int last = -gap - 1;
        for (int f = 2; f < nFrames - 1; ++f)
        {
            const float o = odf[(size_t) f];
            if (o <= 0.0f || o < odf[(size_t) f - 1] || o < odf[(size_t) f + 1]) continue;
            double local = 0.0; int cnt = 0;
            for (int j = juce::jmax (0, f - wl); j < juce::jmin (nFrames, f + wl); ++j) { local += odf[(size_t) j]; ++cnt; }
            local /= juce::jmax (1, cnt);
            if (o < (float) (local * k + odfMean * 0.5)) continue;
            const int lf = juce::jmin (nFrames - 1, f + 3);
            if (lvl[(size_t) lf] < loudest - 30.0f) continue;     // too quiet to be a kick in this song
            // exact time: around the analysis window, find the hit's peak, then walk back to where it took off
            const int a = juce::jmax (0, f * hopL - (int) (0.03 * fs)), z = juce::jmin ((int) env.size() - 1, f * hopL + win + (int) (0.01 * fs));
            int pk = a;
            for (int i = a; i <= z; ++i) if (env[(size_t) i] > env[(size_t) pk]) pk = i;
            float base = env[(size_t) pk];
            for (int i = a; i <= pk; ++i) base = juce::jmin (base, env[(size_t) i]);
            const float thr = base + 0.2f * (env[(size_t) pk] - base);
            int on = pk;
            while (on > a && env[(size_t) on - 1] > thr) --on;
            const auto pos = (juce::int64) juce::jmax (0, on - win) * dec;   // minus the silence in front
            // how much the low end jumps: the level just after vs. just before the hit
            double before = 0.0; int nb4 = 0;
            for (int i = juce::jmax (0, on - (int) (0.04 * fs)); i < juce::jmax (0, on - (int) (0.005 * fs)); ++i) { before += env[(size_t) i]; ++nb4; }
            before = nb4 > 0 ? before / nb4 : 0.0;
            float after = 0.0f;
            for (int i = on; i < juce::jmin ((int) env.size(), on + (int) (0.04 * fs)); ++i) after = juce::jmax (after, env[(size_t) i]);
            const float jump = 20.0f * std::log10 ((after + 1.0e-6f) / (float) (before + 1.0e-6));
            // a kick is a big, broad jump in the low end: small or narrow ones are the bass
            // moving, or the mix breathing on a loud master
            if (jump < juce::jmap (sens, 0.0f, 1.0f, 8.0f, 4.0f) || o < juce::jmap (sens, 0.0f, 1.0f, 3.0f, 1.2f))
                continue;
            if (f - last < gap)
            {
                if (! out.empty() && o > out.back().strength) out.back() = { pos, o, jump };
                continue;
            }
            out.push_back ({ pos, o, jump });
            last = f;
        }
        // two detections can resolve to the same hit: keep one
        std::sort (out.begin(), out.end(), [] (const KickHit& x, const KickHit& y) { return x.pos < y.pos; });
        std::vector<KickHit> merged;
        for (auto& h : out)
        {
            if (! merged.empty() && h.pos - merged.back().pos < (juce::int64) (0.04 * sr))
            {
                if (h.strength > merged.back().strength) merged.back() = h;
                continue;
            }
            merged.push_back (h);
        }
        return merged;
    }

    float detectPitch (const std::vector<float>& lowBand, juce::int64 start, double sr)
    {
        const int from = (int) std::min ((juce::int64) lowBand.size() - 1, start + (juce::int64) (0.04 * sr));
        const int len = (int) std::min ((juce::int64) (0.16 * sr), (juce::int64) lowBand.size() - from);
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
    // the low band is kept (the 808 pitch is measured on it); the others are measured on the fly
    const auto eLow = hopEnergyDb (low, hop);
    const auto eBody = bandEnergyDb (mono, { juce::IIRCoefficients::makeBandPass (sr, 210.0, 1.2) }, hop);
    const auto eNoise = bandEnergyDb (mono, { juce::IIRCoefficients::makeHighPass (sr, 1800.0), juce::IIRCoefficients::makeLowPass (sr, 8000.0) }, hop);
    // capped at 16 kHz so detection doesn't depend on the sample rate (at 96 kHz "everything
    // above 7 kHz" would include ultrasonic content up to 48 kHz)
    const auto eHigh = bandEnergyDb (mono, { juce::IIRCoefficients::makeHighPass (sr, 7000.0), juce::IIRCoefficients::makeHighPass (sr, 7000.0),
                                             juce::IIRCoefficients::makeLowPass (sr, juce::jmin (16000.0, sr * 0.45)) }, hop);

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
        // kicks / 808s: broad low-end rises (see findKicks), placed exactly
        std::vector<Onset> onsets;
        std::vector<juce::int64> positions;
        for (auto& k : findKicks (mono, sr, sens808))
        {
            // a snare stirs up the low end too (its body, and a loud master's distortion):
            // where we found a snare, only a big low-end jump of its own counts as a kick
            bool onSnare = false;
            for (auto sn : result.snare.onsets)
                if (std::abs (sn - k.pos) < (juce::int64) (0.03 * sr)) { onSnare = true; break; }
            if (onSnare && k.lowJumpDb < 10.0f)
                continue;
            onsets.push_back ({ (int) (k.pos / hop), k.strength });
            positions.push_back (k.pos);
        }
        if (onsets.empty())   // very short clips: fall back to the low band's energy
            for (auto& o : pickOnsets (eLow, sens808, ms (140), maxOf (eLow) - 26.0f))
            {
                onsets.push_back (o);
                positions.push_back ((juce::int64) o.frame * hop);
            }
        float bestScore = -1.0e9f; int bestFrame = -1; int count = 0; float bestSustain = 0;
        const float lowMax = maxOf (eLow);
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const auto& o = onsets[i];
            const float peak = meanDb (eLow, o.frame, o.frame + ms (30));
            const float tail = meanDb (eLow, o.frame + ms (150), o.frame + ms (260));
            const float sustain = juce::jlimit (0.0f, 1.0f, 1.0f - (peak - tail) / 24.0f);
            const float score = juce::jmin (o.strength, 40.0f) + sustain * 10.0f + (peak - lowMax) * 0.3f;
            ++count;
            result.bass.onsets.push_back (positions[i]);
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
            const int len = (int) std::max ((juce::int64) (0.15 * sr), std::min ((juce::int64) samplesPerBeat, next - start));
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
