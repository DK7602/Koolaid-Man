#include "MoveProcessors.h"
#include "DrumDetector.h"
#include <juce_dsp/juce_dsp.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace km
{

using Buffer = juce::AudioBuffer<float>;
static constexpr double twoPi = juce::MathConstants<double>::twoPi;

double stutterSpanBeats (float knob, double bpb) { return bpb * std::pow (2.0, -2.0 + 4.0 * (double) knob); }
double filterCutoff (float knob)                 { return 20.0 * std::pow (1000.0, (double) knob); }

namespace
{
    //==========================================================================
    // helpers
    inline float readInterp (const float* d, int n, double pos)
    {
        if (pos <= 0.0) return d[0] * (float) juce::jmax (0.0, 1.0 + pos);
        const int i = (int) pos;
        if (i >= n - 1) return i == n - 1 ? d[n - 1] : 0.0f;
        const float f = (float) (pos - i);
        // 4-point Hermite
        const float xm1 = d[juce::jmax (0, i - 1)], x0 = d[i], x1 = d[i + 1], x2 = d[juce::jmin (n - 1, i + 2)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int toSamples (double beats, const MoveContext& c) { return (int) std::llround (beats * c.samplesPerBeat); }

    /** A move's own length (from its settings), unless the timeline gave it a slice to fill. */
    double regionLen (double natural, const MoveContext& c) { return c.lengthOverride >= 0.0 ? c.lengthOverride : natural; }

    /** Linear crossfade from 'before' to 'after' in [start, start+len). */
    void crossfadeInto (Buffer& target, const Buffer& before, int start, int len)
    {
        len = juce::jmin (len, target.getNumSamples() - start);
        for (int c = 0; c < target.getNumChannels(); ++c)
        {
            auto* t = target.getWritePointer (c);
            auto* b = before.getReadPointer (c);
            for (int i = 0; i < len; ++i)
            {
                const float a = (float) i / (float) len;
                t[start + i] = b[start + i] * (1.0f - a) + t[start + i] * a;
            }
        }
    }

    /** Adds a one-shot into dest at 'pos', with a playback rate and gain, cut to maxLen with a short fade. */
    void addHit (Buffer& dest, const Buffer& hit, int pos, float gain, double rate, int maxLen, float pan = 0.0f)
    {
        if (pos >= dest.getNumSamples() || hit.getNumSamples() == 0) return;
        const int hitLen = (int) (hit.getNumSamples() / rate);
        int len = juce::jmin (hitLen, maxLen, dest.getNumSamples() - pos);
        if (len <= 0) return;
        const int fade = juce::jmin (len / 3, 96);
        const float gl = gain * juce::jmin (1.0f, 1.0f - pan), gr = gain * juce::jmin (1.0f, 1.0f + pan);
        for (int c = 0; c < dest.getNumChannels(); ++c)
        {
            auto* d = dest.getWritePointer (c);
            auto* s = hit.getReadPointer (juce::jmin (c, hit.getNumChannels() - 1));
            const float g = c == 0 ? gl : gr;
            for (int i = 0; i < len; ++i)
            {
                if (pos + i < 0) continue;
                float v = readInterp (s, hit.getNumSamples(), i * rate) * g;
                if (i >= len - fade) v *= (float) (len - i) / (float) fade;
                d[pos + i] += v;
            }
        }
    }

    double curve (double t, double p) { return std::pow (juce::jlimit (0.0, 1.0, t), p); }

    //==========================================================================
    /** Classic two-tap delay-line pitch shifter (time preserving). */
    struct PitchShifter
    {
        void prepare (double sr, double windowSeconds = 0.05)
        {
            window = juce::jmax (256, (int) (sr * windowSeconds));
            size = window * 2 + 8;
            buf.assign ((size_t) size, 0.0f);
            write = 0;
            phase = 0.5;
        }
        int latency() const { return window / 2; }

        float process (float x, double ratio)
        {
            buf[(size_t) write] = x;
            phase += (1.0 - ratio) / window;
            phase -= std::floor (phase);
            const double p2 = std::fmod (phase + 0.5, 1.0);
            const float w1 = (float) std::pow (std::sin (juce::MathConstants<double>::pi * phase), 2.0);
            const float w2 = 1.0f - w1;
            const float y = tap (phase * window) * w1 + tap (p2 * window) * w2;
            write = (write + 1) % size;
            return y;
        }

    private:
        float tap (double delay) const
        {
            double rp = write - delay;
            while (rp < 0) rp += size;
            const int i0 = (int) rp;
            const float f = (float) (rp - i0);
            const float a = buf[(size_t) (i0 % size)], b = buf[(size_t) ((i0 + 1) % size)];
            return a + (b - a) * f;
        }
        std::vector<float> buf;
        int window = 2048, size = 4104, write = 0;
        double phase = 0.5;
    };

    //==========================================================================
    /** Studio-quality pitch shift (Signalsmith Stretch, spectral), following a per-sample
        semitone curve. Latency is compensated, so the output stays sample-aligned. */
    template <typename SemiFn>
    void pitchShiftHQ (Buffer& b, double sr, SemiFn semisAt)
    {
        const int n = b.getNumSamples(), numCh = b.getNumChannels();
        if (n < 64) return;
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (numCh, (float) sr);
        const int inLat = st.inputLatency(), outLat = st.outputLatency();

        Buffer in (numCh, n + inLat), out (numCh, n + outLat);
        in.clear();
        out.clear();
        for (int ch = 0; ch < numCh; ++ch)
            in.copyFrom (ch, 0, b, ch, 0, n);

        std::vector<float*> ip ((size_t) numCh), op ((size_t) numCh);
        auto point = [numCh] (Buffer& buf, int offset, std::vector<float*>& v)
        {
            for (int ch = 0; ch < numCh; ++ch) v[(size_t) ch] = buf.getWritePointer (ch) + offset;
        };

        // prime so that the processing time starts at the first input sample
        st.setTransposeSemitones ((float) semisAt (0));
        point (in, 0, ip);
        st.seek (ip.data(), inLat, 1.0);

        const int block = 256;
        int inPos = inLat, outPos = 0;
        while (outPos < n)
        {
            const int len = juce::jmin (block, n - outPos);
            st.setTransposeSemitones ((float) semisAt (juce::jlimit (0, n - 1, outPos)));
            point (in, inPos, ip);
            point (out, outPos, op);
            st.process (ip.data(), len, op.data(), len);
            inPos += len;
            outPos += len;
        }
        point (out, outPos, op);
        st.flush (op.data(), outLat);

        // output has outputLatency() samples of pre-roll
        for (int ch = 0; ch < numCh; ++ch)
            b.copyFrom (ch, 0, out, ch, outLat, n);
    }

    //==========================================================================
    /** 8-line feedback delay network reverb used by Atmos (and its shimmer). */
    class FdnReverb
    {
    public:
        struct Settings
        {
            double rt60 = 4.0, scale = 1.0, damping = 0.3, predelayMs = 20.0, modDepth = 0.3, shimmer = 0.0, diffusion = 0.65;
        };

        void prepare (double sampleRate, const Settings& st)
        {
            sr = sampleRate; s = st;
            static const double baseMs[8] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
            for (int i = 0; i < 8; ++i)
            {
                len[i] = juce::jmax (16, (int) (baseMs[i] * s.scale * sr / 1000.0));
                lines[i].assign ((size_t) (len[i] + 64), 0.0f);
                pos[i] = 0;
                gain[i] = (float) std::pow (10.0, -3.0 * len[i] / (s.rt60 * sr));
                lp[i] = 0.0f;
                lfoPhase[i] = i * 0.71;
            }
            static const double apMs[4] = { 4.7, 3.6, 12.7, 9.3 };
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 4; ++k)
                {
                    apLen[ch][k] = juce::jmax (8, (int) (apMs[k] * (1.0 + 0.07 * ch) * sr / 1000.0));
                    ap[ch][k].assign ((size_t) apLen[ch][k], 0.0f);
                    apPos[ch][k] = 0;
                }
            preLen = juce::jmax (1, (int) (s.predelayMs * sr / 1000.0));
            pre[0].assign ((size_t) preLen, 0.0f); pre[1].assign ((size_t) preLen, 0.0f); prePos = 0;
            shifter.prepare (sr, 0.08);
            shimmerState = 0.0f;
            hp[0] = hp[1] = 0.0f;
        }

        void process (float inL, float inR, float& outL, float& outR)
        {
            // pre-delay
            const float pl = pre[0][(size_t) prePos], pr = pre[1][(size_t) prePos];
            pre[0][(size_t) prePos] = inL; pre[1][(size_t) prePos] = inR;
            prePos = (prePos + 1) % preLen;

            float x[2] = { pl, pr };
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 4; ++k)
                {
                    auto& b = ap[ch][k];
                    const float d = b[(size_t) apPos[ch][k]];
                    const float g = (float) s.diffusion;
                    const float v = x[ch] + g * d;
                    b[(size_t) apPos[ch][k]] = v;
                    x[ch] = d - g * v;
                    apPos[ch][k] = (apPos[ch][k] + 1) % apLen[ch][k];
                }

            float o[8];
            for (int i = 0; i < 8; ++i)
            {
                lfoPhase[i] += (0.13 + 0.05 * i) / sr;
                if (lfoPhase[i] > 1.0) lfoPhase[i] -= 1.0;
                const double mod = s.modDepth * 12.0 * (1.0 + std::sin (twoPi * lfoPhase[i]));
                double rp = pos[i] - len[i] - mod;
                const int sz = (int) lines[i].size();
                while (rp < 0) rp += sz;
                const int i0 = (int) rp;
                const float f = (float) (rp - i0);
                const float a = lines[i][(size_t) (i0 % sz)], b = lines[i][(size_t) ((i0 + 1) % sz)];
                float v = a + (b - a) * f;
                lp[i] = v * (1.0f - (float) s.damping) + lp[i] * (float) s.damping;
                o[i] = lp[i] * gain[i];
            }

            // fast Hadamard
            for (int h = 1; h < 8; h <<= 1)
                for (int i = 0; i < 8; i += h * 2)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = o[j], b = o[j + h];
                        o[j] = a + b; o[j + h] = a - b;
                    }
            const float norm = 0.35355339f;

            float shimmerIn = 0.0f;
            if (s.shimmer > 0.0)
            {
                shimmerState = shifter.process ((o[0] + o[3]) * norm, 2.0);
                shimmerIn = shimmerState * (float) s.shimmer;
            }

            for (int i = 0; i < 8; ++i)
            {
                const float in = (i & 1) ? x[1] : x[0];
                const int sz = (int) lines[i].size();
                lines[i][(size_t) pos[i]] = o[i] * norm + in * 0.5f + shimmerIn * ((i & 2) ? 0.6f : -0.6f);
                pos[i] = (pos[i] + 1) % sz;
            }

            float l = 0, r = 0;
            for (int i = 0; i < 8; ++i)
            {
                l += o[i] * ((i % 2) ? -1.0f : 1.0f);
                r += o[i] * ((i / 2) % 2 ? -1.0f : 1.0f);
            }
            // gentle high-pass to keep the wash out of the sub range
            const float hpCoef = 0.995f;
            const float yl = l - hp[0]; hp[0] = l * (1.0f - hpCoef) + hp[0] * hpCoef;
            const float yr = r - hp[1]; hp[1] = r * (1.0f - hpCoef) + hp[1] * hpCoef;
            outL = yl * 0.25f; outR = yr * 0.25f;
        }

    private:
        double sr = 48000.0;
        Settings s;
        std::vector<float> lines[8];
        int len[8] {}, pos[8] {};
        float gain[8] {}, lp[8] {};
        double lfoPhase[8] {};
        std::vector<float> ap[2][4];
        int apLen[2][4] {}, apPos[2][4] {};
        std::vector<float> pre[2];
        int preLen = 1, prePos = 0;
        PitchShifter shifter;
        float shimmerState = 0.0f;
        float hp[2] {};
    };

    FdnReverb::Settings atmosSettings (int type, float size)
    {
        FdnReverb::Settings s;
        const double z = size;
        switch (type)
        {
            case 0: s.rt60 = 2.0 + 4.0 * z;  s.scale = 1.0;  s.damping = 0.35; s.predelayMs = 22; s.modDepth = 0.3; break;            // Hall
            case 1: s.rt60 = 4.0 + 8.0 * z;  s.scale = 1.6;  s.damping = 0.25; s.predelayMs = 45; s.modDepth = 0.2; break;            // Cathedral
            case 2: s.rt60 = 4.0 + 6.0 * z;  s.scale = 1.3;  s.damping = 0.30; s.predelayMs = 30; s.modDepth = 0.4; s.shimmer = 0.3; break;  // Shimmer
            case 3: s.rt60 = 3.0 + 6.0 * z;  s.scale = 1.2;  s.damping = 0.50; s.predelayMs = 8;  s.modDepth = 1.0; s.diffusion = 0.75; break; // Cloud
            case 4: s.rt60 = 1.2 + 3.0 * z;  s.scale = 0.6;  s.damping = 0.12; s.predelayMs = 5;  s.modDepth = 0.15; s.diffusion = 0.72; break; // Plate
            case 5: s.rt60 = 8.0 + 12.0 * z; s.scale = 2.0;  s.damping = 0.45; s.predelayMs = 80; s.modDepth = 0.6; break;            // Deep Space
            case 6: s.rt60 = 2.5 + 4.5 * z;  s.scale = 1.1;  s.damping = 0.30; s.predelayMs = 0;  s.modDepth = 0.3; break;            // Reverse Swell
            default: break;
        }
        return s;
    }

    //==========================================================================
    /** Simple TPT state-variable filter. */
    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (double sr, double hz, double kk)
        {
            hz = juce::jlimit (16.0, sr * 0.45, hz);
            g = (float) std::tan (juce::MathConstants<double>::pi * hz / sr);
            k = (float) kk;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void process (float x, float& lpOut, float& bpOut, float& hpOut)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            lpOut = v2; bpOut = v1; hpOut = x - k * v1 - v2;
        }
    };

    //==========================================================================
    void doReverse (const Move& m, const MoveContext& c, Buffer& b, bool isFx)
    {
        const int n = b.getNumSamples();
        int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        if (m.mode == 2) L = n;
        if (L < 64) return;
        const int start = n - L;
        const float mix = m.knob;

        Buffer rev (b.getNumChannels(), L);
        bool usedDownbeat = false;
        if (m.mode == 1 && c.clip != nullptr && ! isFx)
        {
            const juce::int64 dropPos = c.zoneStartInClip + n;
            if (dropPos + L <= c.clip->getNumSamples())
            {
                for (int ch = 0; ch < rev.getNumChannels(); ++ch)
                    rev.copyFrom (ch, 0, *c.clip, juce::jmin (ch, c.clip->getNumChannels() - 1), (int) dropPos, L);
                usedDownbeat = true;
            }
        }
        if (m.mode == 1 && isFx)
        {
            rev.clear();   // the pre-verse replaces the build: duck the fx layer under it
            usedDownbeat = true;
        }
        if (! usedDownbeat)
            for (int ch = 0; ch < rev.getNumChannels(); ++ch)
                rev.copyFrom (ch, 0, b, ch, start, L);

        for (int ch = 0; ch < rev.getNumChannels(); ++ch)
            std::reverse (rev.getWritePointer (ch), rev.getWritePointer (ch) + L);

        const int fade = juce::jmin (L / 4, (int) (0.008 * c.sampleRate));
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            auto* r = rev.getReadPointer (ch);
            for (int i = 0; i < L; ++i)
            {
                float w = mix;
                if (i < fade) w *= (float) i / (float) fade;
                d[start + i] = d[start + i] * (1.0f - w) + r[i] * w;
            }
        }
    }

    /** Which pattern step a zone sample falls on: steps of 'step' samples counted on the song's
        grid (the clip starts on a bar line), so a 16-step pattern of 1/16s lines up with each bar. */
    inline int patternStep (const MoveContext& c, int i, int step, int& posInStep)
    {
        const juce::int64 abs = c.zoneStartInClip + i;
        posInStep = (int) (abs % step);
        return (int) ((abs / step) % patternSteps);
    }
    inline bool stepOn (const Move& m, int s) { return ((m.pattern >> ((s % patternSteps + patternSteps) % patternSteps)) & 1u) != 0; }

    void doChops (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const int step = juce::jmax (32, toSamples (divisionToBeats (m.division, c.beatsPerBar), c));
        const float depth = m.knob;
        const int ramp = juce::jmin (step / 4, (int) (0.002 * c.sampleRate) + 1);

        if (m.mode == 1) // shuffle
        {
            Buffer src (b);
            juce::Random rng ((juce::int64) m.seed * 7919 + 17);
            const int slicesPerBar = juce::jmax (1, (int) std::round (c.beatsPerBar * c.samplesPerBeat / step));
            const int numSlices = (n + step - 1) / step;
            for (int s = 0; s < numSlices; ++s)
            {
                int from = s;
                int pos = 0;
                const bool shuffleThis = m.patternOn ? stepOn (m, patternStep (c, s * step, step, pos)) : rng.nextFloat() < depth * 0.8f;
                if (m.patternOn) rng.nextFloat();   // keep the random sequence the same either way
                if (shuffleThis)
                {
                    const int barStart = (s / slicesPerBar) * slicesPerBar;
                    from = juce::jmin (numSlices - 1, barStart + rng.nextInt (slicesPerBar));
                    from = juce::jmin (from, s);   // never borrow from the future (keeps the drop clean)
                }
                const int len = juce::jmin (step, n - s * step, n - from * step);
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    auto* sp = src.getReadPointer (ch);
                    for (int i = 0; i < len; ++i)
                    {
                        float g = 1.0f;
                        if (i < ramp) g = (float) i / ramp;
                        else if (i > len - ramp) g = (float) (len - i) / ramp;
                        d[s * step + i] = from == s ? sp[s * step + i] : sp[from * step + i] * g;
                    }
                }
            }
            return;
        }

        if (m.patternOn)
        {
            // step pattern: on steps sound for the whole step (consecutive steps stay open),
            // off steps are cut by Depth. Tremolo swells each on step instead.
            for (int i = 0; i < n; ++i)
            {
                int ph = 0;
                const int st = patternStep (c, i, step, ph);
                const bool on = stepOn (m, st), prevOn = stepOn (m, st - 1), nextOn = stepOn (m, st + 1);
                float open;
                if (m.mode == 2)
                    open = on ? (float) (0.5 - 0.5 * std::cos (twoPi * ph / step)) : 0.0f;
                else if (! on)
                    open = 0.0f;
                else if (! prevOn && ph < ramp)
                    open = (float) ph / ramp;
                else if (! nextOn && ph > step - ramp)
                    open = (float) (step - ph) / ramp;
                else
                    open = 1.0f;
                const float g = 1.0f - depth * (1.0f - open);
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    b.getWritePointer (ch)[i] *= g;
            }
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            const int ph = i % step;
            float g;
            if (m.mode == 2) // tremolo
            {
                g = 1.0f - depth * (float) (0.5 - 0.5 * std::cos (twoPi * ph / step));
            }
            else
            {
                const int half = step / 2;
                float open;
                if (ph < ramp) open = (float) ph / ramp;
                else if (ph < half) open = 1.0f;
                else if (ph < half + ramp) open = 1.0f - (float) (ph - half) / ramp;
                else open = 0.0f;
                g = 1.0f - depth * (1.0f - open);
            }
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.getWritePointer (ch)[i] *= g;
        }
    }

    void tileSlice (Buffer& b, const Buffer& src, int sliceStart, int sliceLen, int from, int to, int fade)
    {
        sliceLen = juce::jmax (16, sliceLen);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            auto* s = src.getReadPointer (ch);
            for (int i = from; i < to; ++i)
            {
                const int k = (i - from) % sliceLen;
                float g = 1.0f;
                if (k < fade) g = (float) k / fade;
                else if (k > sliceLen - fade) g = (float) (sliceLen - k) / fade;
                const int si = juce::jlimit (0, src.getNumSamples() - 1, sliceStart + k);
                d[i] = s[si] * g;
            }
        }
    }

    void doStutter (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const int slice = juce::jmax (32, toSamples (divisionToBeats (m.division, c.beatsPerBar), c));
        const int span = juce::jmin (n, toSamples (regionLen (stutterSpanBeats (m.knob, c.beatsPerBar), c), c));
        if (span < slice) return;
        const int start = n - span;
        const Buffer src (b);
        const int fade = juce::jmin (slice / 4, (int) (0.0015 * c.sampleRate) + 1);

        if (m.mode == 0)
        {
            tileSlice (b, src, start, slice, start, n, fade);
        }
        else if (m.mode == 1) // accelerate: 2x, 1x, 1/2, 1/4 slice sizes across four parts
        {
            for (int part = 0; part < 4; ++part)
            {
                const int p0 = start + span * part / 4, p1 = start + span * (part + 1) / 4;
                const int sl = juce::jmax (32, (int) (slice * std::pow (2.0, 1 - part)));
                tileSlice (b, src, start, sl, p0, p1, juce::jmin (fade, sl / 4));
            }
        }
        else // beat repeat
        {
            const int beat = juce::jmax (slice, toSamples (1.0, c));
            for (int p0 = start; p0 < n; p0 += beat)
                tileSlice (b, src, p0, slice, p0, juce::jmin (n, p0 + beat), fade);
        }
        // step pattern: only the on steps stutter, the off steps play the track as it was
        if (m.patternOn)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                auto* o = src.getReadPointer (ch);
                for (int i = start; i < n; ++i)
                {
                    int ph = 0;
                    const int st = patternStep (c, i, slice, ph);
                    const float on = stepOn (m, st) ? 1.0f : 0.0f, was = stepOn (m, st - 1) ? 1.0f : 0.0f;
                    const float w = ph < fade && i - start >= fade ? was + (on - was) * (float) ph / fade : on;
                    d[i] = o[i] + (d[i] - o[i]) * w;
                }
            }
        }
        // de-click the entry
        Buffer before (src);
        crossfadeInto (b, before, start, juce::jmin (fade * 2, span));
    }

    void doPitch (const Move& m, const MoveContext& c, Buffer& b)
    {
        if (m.semitones == 0) return;
        const int n = b.getNumSamples();
        const double p = juce::jmap ((double) m.knob, 0.35, 3.2);
        const double target = m.semitones;
        const bool ramp = m.mode == 0;
        pitchShiftHQ (b, c.sampleRate, [&] (int i) { return ramp ? target * (m.curve == 0 ? curve ((double) i / n, p) : shapeCurve (m, (double) i / n, 1.0)) : target; });
    }

    void doTapeStop (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        if (L < 128) return;
        const int start = n - L;
        const double p = juce::jmap ((double) m.knob, 0.55, 2.6);
        const Buffer src (b);

        // pre-compute the read positions
        std::vector<double> rate ((size_t) L);
        double total = 0;
        for (int i = 0; i < L; ++i)
        {
            const double t = (double) i / L;
            rate[(size_t) i] = m.mode == 0 ? std::pow (1.0 - t, p) : std::pow (t, p);
            total += rate[(size_t) i];
        }
        const double startPos = m.mode == 0 ? (double) start : (double) n - total;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            auto* s = src.getReadPointer (ch);
            double pos = startPos;
            float lpState = 0.0f;
            for (int i = 0; i < L; ++i)
            {
                const double r = rate[(size_t) i];
                float v = readInterp (s, n, pos);
                // darken as the tape slows
                const float coef = (float) juce::jlimit (0.02, 1.0, r * 1.2);
                lpState += (v - lpState) * coef;
                v = lpState * (float) std::pow (juce::jmax (0.0, r), 0.25);
                const int tail = juce::jmin (L / 10, (int) (0.02 * c.sampleRate));
                if (m.mode == 0 && i > L - tail) v *= (float) (L - i) / tail;
                if (m.mode == 1 && i < tail) v *= (float) i / tail;
                d[start + i] = v;
                pos += r;
            }
        }
        crossfadeInto (b, src, start, juce::jmin (L / 8, (int) (0.004 * c.sampleRate) + 1));
    }

    void doFilter (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const double fc = filterCutoff (m.knob);
        const double k = 2.0 - 1.9 * m.knob2;
        const Buffer dry (b);
        const int numCh = b.getNumChannels();

        // run the filter at 2x the sample rate (linear-phase half-band FIR), so resonant
        // sweeps stay smooth and correctly tuned right up to the top of the spectrum
        juce::dsp::Oversampling<float> os ((size_t) numCh, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        const int block = 512;
        os.initProcessing ((size_t) block);
        const int lat = (int) std::lround (os.getLatencyInSamples());
        Buffer in (numCh, n + lat), out (numCh, n + lat);
        in.clear();
        out.clear();
        for (int ch = 0; ch < numCh; ++ch)
            in.copyFrom (ch, 0, b, ch, 0, n);

        std::vector<Svf> filters ((size_t) numCh);
        const double osRate = c.sampleRate * 2.0;
        for (int s0 = 0; s0 < n + lat; s0 += block)
        {
            const int len = juce::jmin (block, n + lat - s0);
            juce::dsp::AudioBlock<float> inBlock (in.getArrayOfWritePointers(), (size_t) numCh, (size_t) s0, (size_t) len);
            auto up = os.processSamplesUp (inBlock);
            const int upLen = (int) up.getNumSamples();
            for (int i = 0; i < upLen; ++i)
            {
                if ((i & 31) == 0)
                {
                    const double t = shapeCurve (m, (s0 + i * 0.5 - lat) / (double) n, 1.35);
                    double hz = fc;
                    switch (m.mode)
                    {
                        case 0: hz = std::exp (std::log (20000.0) + (std::log (fc) - std::log (20000.0)) * t); break;
                        case 1: hz = std::exp (std::log (20.0) + (std::log (fc) - std::log (20.0)) * t); break;
                        case 2: hz = std::exp (std::log (fc) + std::log (8.0) * t); break;
                        default: break;
                    }
                    for (auto& f : filters) f.set (osRate, hz, k);
                }
                for (int ch = 0; ch < numCh; ++ch)
                {
                    float* d = up.getChannelPointer ((size_t) ch);
                    float lp, bp, hp;
                    filters[(size_t) ch].process (d[i], lp, bp, hp);
                    d[i] = (m.mode == 0 || m.mode == 3) ? lp : (m.mode == 2 ? bp * (float) k : hp);
                }
            }
            juce::dsp::AudioBlock<float> outBlock (out.getArrayOfWritePointers(), (size_t) numCh, (size_t) s0, (size_t) len);
            os.processSamplesDown (outBlock);
        }
        for (int ch = 0; ch < numCh; ++ch)
            b.copyFrom (ch, 0, out, ch, lat, n);
        if (m.mode >= 2)
            crossfadeInto (b, dry, 0, juce::jmin (n, (int) (0.015 * c.sampleRate)));
    }

    const Buffer& snareHit (const MoveContext& c, Buffer& fallback)
    {
        if (c.drums != nullptr && c.drums->snare.found && c.drums->snare.oneShot.getNumSamples() > 0)
            return c.drums->snare.oneShot;
        fallback = synthSnare (c.sampleRate);
        return fallback;
    }

    const Buffer& hatHit (const MoveContext& c, Buffer& fallback, bool open)
    {
        if (! open && c.drums != nullptr && c.drums->hats.found && c.drums->hats.oneShot.getNumSamples() > 0)
            return c.drums->hats.oneShot;
        fallback = synthHat (c.sampleRate, open);
        return fallback;
    }

    void doDrumMoves (const Move& m, const MoveContext& c, Buffer& main, Buffer& fx)
    {
        const int n = main.getNumSamples();
        const float level = (0.45f + 0.55f * m.knob) * c.refPeak * 0.95f;
        Buffer fb;
        const Buffer& hit = snareHit (c, fb);

        if (m.mode == 1) // gate on the detected hits
        {
            std::vector<juce::int64> hits;
            if (c.drums != nullptr)
                for (auto* set : { &c.drums->snare.onsets, &c.drums->bass.onsets, &c.drums->hats.onsets })
                    for (auto s : *set)
                        if (s >= c.zoneStartInClip && s < c.zoneStartInClip + n)
                            hits.push_back (s - c.zoneStartInClip);
            if (hits.empty())
                for (double bt = 0; bt * c.samplesPerBeat < n; bt += 0.5)
                    hits.push_back ((juce::int64) (bt * c.samplesPerBeat));
            std::sort (hits.begin(), hits.end());

            const double tau = 0.125 * c.samplesPerBeat;
            const int atk = (int) (0.001 * c.sampleRate) + 1;
            std::vector<float> env ((size_t) n, 0.04f);
            for (auto h : hits)
                for (int i = juce::jmax<int> (0, (int) h - atk); i < n; ++i)
                {
                    const double dt = i - (double) h;
                    const float v = dt < 0 ? (float) (1.0 + dt / atk) : (float) std::exp (-dt / tau);
                    if (v < 0.01f) break;
                    env[(size_t) i] = juce::jmax (env[(size_t) i], v);
                }
            const float depth = 0.5f + 0.5f * m.knob;
            for (auto* buf : { &main, &fx })
                for (int ch = 0; ch < buf->getNumChannels(); ++ch)
                {
                    auto* d = buf->getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                        d[i] *= 1.0f - depth * (1.0f - env[(size_t) i]);
                }
            return;
        }

        const double bpb = c.beatsPerBar;
        if (m.mode == 0) // fill: 1/4 -> 1/8 -> 1/16 -> 1/32 across the last (up to) 4 bars
        {
            const double regionBeats = juce::jmin (n / c.samplesPerBeat, regionLen (4.0 * bpb, c));
            const double startBeat = n / c.samplesPerBeat - regionBeats;
            const double seg = regionBeats / 4.0;
            const double base = regionBeats >= 2.0 * bpb ? 1.0 : (regionBeats >= bpb ? 0.5 : 0.25);
            for (int part = 0; part < 4; ++part)
            {
                const double stepBeats = base / std::pow (2.0, part);
                for (double bt = startBeat + part * seg; bt < startBeat + (part + 1) * seg - 1.0e-6; bt += stepBeats)
                {
                    const double t = (bt - startBeat) / regionBeats;
                    const int pos = (int) std::llround (bt * c.samplesPerBeat);
                    const float vel = (float) (0.5 + 0.5 * shapeCurve (m, t, 1.0));
                    const double rate = std::pow (2.0, (4.0 * t) / 12.0);
                    addHit (fx, hit, pos, level * vel, rate, (int) (stepBeats * c.samplesPerBeat));
                }
            }
        }
        else // roll: steady 1/16s over the last 2 bars, crescendo, last beat 1/32
        {
            const double regionBeats = juce::jmin (n / c.samplesPerBeat, regionLen (2.0 * bpb, c));
            const double endBeat = n / c.samplesPerBeat;
            const double startBeat = endBeat - regionBeats;
            for (double bt = startBeat; bt < endBeat - 1.0e-6;)
            {
                const double t = (bt - startBeat) / regionBeats;
                const double step = (endBeat - bt) <= 1.0 + 1.0e-6 ? 0.125 : 0.25;
                const int pos = (int) std::llround (bt * c.samplesPerBeat);
                addHit (fx, hit, pos, level * (float) (0.25 + 0.75 * shapeCurve (m, t, 2.0)), std::pow (2.0, 7.0 * t / 12.0),
                        (int) (step * c.samplesPerBeat));
                bt += step;
            }
        }
    }

    void do808 (const Move& m, const MoveContext& c, Buffer& fx)
    {
        const int n = fx.getNumSamples();
        double f0 = c.rootHz (32.0, 49.0);   // the song's root if no 808 was found
        const Buffer* transient = nullptr;
        if (c.drums != nullptr && c.drums->bass.found)
        {
            if (c.drums->bass.pitchHz > 20.0f) f0 = c.drums->bass.pitchHz;
            transient = &c.drums->bass.oneShot;
        }
        while (f0 > 80.0) f0 *= 0.5;
        while (f0 < 32.0) f0 *= 2.0;

        const double bpb = c.beatsPerBar;
        const double lenBeats = regionLen (m.mode == 0 ? bpb * 0.5 : bpb, c);
        const int L = juce::jmin (n, (int) (lenBeats * c.samplesPerBeat));
        const int start = n - L;
        const float level = 0.5f + 0.5f * m.knob;
        std::vector<float> sub ((size_t) L, 0.0f);
        double phase = 0.0;
        const int atk = (int) (0.003 * c.sampleRate) + 1, rel = (int) (0.03 * c.sampleRate) + 1;
        for (int i = 0; i < L; ++i)
        {
            const double t = (double) i / L;
            double freq = f0;
            float amp = 1.0f;
            if (m.mode == 0)      freq = f0 * std::pow (2.0, -12.0 * shapeCurve (m, t, 1.6) / 12.0);
            else if (m.mode == 1) freq = f0 * std::pow (2.0, (-12.0 + 12.0 * shapeCurve (m, t, 0.8)) / 12.0);
            else                  amp = (float) shapeCurve (m, t, 2.2);
            if (i < atk) amp *= (float) i / atk;
            if (i > L - rel) amp *= (float) (L - i) / rel;
            phase += freq / c.sampleRate;
            sub[(size_t) i] = (float) std::tanh (1.8 * std::sin (twoPi * phase)) * amp;
        }
        // level-match: about as strong as the track's own low end (a touch above it), never
        // louder than the whole track, scaled by the move's level
        double ss = 0.0;
        for (auto v : sub) ss += (double) v * v;
        const double subRms = std::sqrt (ss / juce::jmax (1, L)) + 1.0e-9;
        const double target = juce::jlimit ((double) c.refRms * 0.35, (double) c.refRms * 0.9, (double) c.lowRms * 1.6) * level;
        const float g = (float) (target / subRms);
        for (int i = 0; i < L; ++i)
        {
            fx.addSample (0, start + i, sub[(size_t) i] * g);
            fx.addSample (1, start + i, sub[(size_t) i] * g);
        }
        if (transient != nullptr && m.mode != 2)
        {
            const int tl = juce::jmin (transient->getNumSamples(), (int) (0.03 * c.sampleRate));
            Buffer t (2, tl);
            for (int ch = 0; ch < 2; ++ch) t.copyFrom (ch, 0, *transient, juce::jmin (ch, transient->getNumChannels() - 1), 0, tl);
            addHit (fx, t, start, level * c.refPeak * 0.6f, 1.0, tl);
        }
    }

    void doHats (const Move& m, const MoveContext& c, Buffer& fx)
    {
        const int n = fx.getNumSamples();
        const float level = (0.3f + 0.5f * m.knob) * c.refPeak;
        Buffer fb;
        const Buffer& hit = hatHit (c, fb, m.mode == 2);
        const double totalBeats = n / c.samplesPerBeat;
        const double regionBeats = juce::jmin (totalBeats, regionLen (4.0 * c.beatsPerBar, c));
        const double startBeat = totalBeats - regionBeats;

        int idx = 0;
        if (m.mode == 2)
        {
            for (double bt = std::ceil (startBeat) + 0.5; bt < totalBeats; bt += 1.0)
            {
                const double t = (bt - startBeat) / regionBeats;
                addHit (fx, hit, (int) std::llround (bt * c.samplesPerBeat), level * (float) (0.5 + 0.5 * shapeCurve (m, t, 1.0)), 1.0,
                        (int) (0.5 * c.samplesPerBeat));
                if (totalBeats - bt <= c.beatsPerBar)
                    addHit (fx, hit, (int) std::llround ((bt + 0.25) * c.samplesPerBeat), level * 0.6f, 1.0,
                            (int) (0.25 * c.samplesPerBeat));
            }
            return;
        }

        for (double bt = startBeat; bt < totalBeats - 1.0e-6; ++idx)
        {
            const double t = (bt - startBeat) / regionBeats;
            double step;
            if (m.mode == 1) step = (totalBeats - bt) <= 1.0 + 1.0e-6 ? 1.0 / 12.0 : 1.0 / 6.0;
            else             step = t < 0.5 ? 0.25 : (t < 0.75 ? 1.0 / 6.0 : 0.125);
            const double inBeat = std::fmod (bt, 1.0);
            const float accent = inBeat < 0.01 ? 1.0f : 0.68f;
            const float pan = (idx % 2 == 0) ? -0.2f : 0.2f;
            addHit (fx, hit, (int) std::llround (bt * c.samplesPerBeat), level * accent * (float) (0.55 + 0.45 * shapeCurve (m, t, 1.0)),
                    1.0, (int) (step * c.samplesPerBeat), pan);
            bt += step;
        }
    }

    void doAtmos (const Move& m, const MoveContext& c, Buffer& main, Buffer& fx)
    {
        const int n = main.getNumSamples();
        const int type = juce::jlimit (0, 6, m.mode);
        Buffer in (2, n);
        for (int ch = 0; ch < 2; ++ch)
        {
            in.copyFrom (ch, 0, main, juce::jmin (ch, main.getNumChannels() - 1), 0, n);
            in.addFrom (ch, 0, fx, juce::jmin (ch, fx.getNumChannels() - 1), 0, n);
        }
        const bool reversed = type == 6;
        if (reversed)
            for (int ch = 0; ch < 2; ++ch)
                std::reverse (in.getWritePointer (ch), in.getWritePointer (ch) + n);

        FdnReverb rv;
        rv.prepare (c.sampleRate, atmosSettings (type, m.knob));
        Buffer wet (2, n);
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            rv.process (in.getSample (0, i), in.getSample (1, i), l, r);
            wet.setSample (0, i, l);
            wet.setSample (1, i, r);
        }
        if (reversed)
            for (int ch = 0; ch < 2; ++ch)
                std::reverse (wet.getWritePointer (ch), wet.getWritePointer (ch) + n);

        // match the wash to the track: at full ramp its RMS is (mix x 1.1) of the dry zone
        double ws = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = n / 2; i < n; ++i) ws += (double) wet.getSample (ch, i) * wet.getSample (ch, i);
        const double wetRms = std::sqrt (ws / juce::jmax (1, n)) + 1.0e-9;
        const float mix = (float) (m.knob2 * 1.1 * c.refRms / wetRms);
        for (int i = 0; i < n; ++i)
        {
            const float ramp = (float) shapeCurve (m, (double) i / n, 1.4);
            fx.addSample (0, i, wet.getSample (0, i) * mix * ramp);
            fx.addSample (1, i, wet.getSample (1, i) * mix * ramp);
        }
        // let the dry thin out a little as the wash takes over
        const float duck = 0.35f * m.knob2;
        for (int ch = 0; ch < main.getNumChannels(); ++ch)
        {
            auto* d = main.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] *= 1.0f - duck * (float) shapeCurve (m, (double) i / n, 1.4);
        }
    }


    //==========================================================================
    // Classic build & drop tools
    //==========================================================================
    float rmsOf (const Buffer& b, int s0, int len)
    {
        s0 = juce::jlimit (0, b.getNumSamples(), s0);
        len = juce::jlimit (0, b.getNumSamples() - s0, len);
        if (len <= 0) return 0.0f;
        double acc = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = s0; i < s0 + len; ++i) acc += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return (float) std::sqrt (acc / (len * b.getNumChannels()));
    }

    /** Scales 'layer' so the RMS of [s0, s0+len) hits 'target', then adds it into 'dest' at 'offset'. */
    void addMatched (Buffer& dest, Buffer& layer, int offset, int s0, int len, float target)
    {
        const float r = rmsOf (layer, s0, len);
        if (r < 1.0e-9f) return;
        layer.applyGain (target / r);
        for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        {
            const int n = juce::jmin (layer.getNumSamples(), dest.getNumSamples() - offset);
            if (n > 0) dest.addFrom (ch, offset, layer, juce::jmin (ch, layer.getNumChannels() - 1), 0, n);
        }
    }

    /** PolyBLEP sawtooth (alias-suppressed). */
    struct BlepSaw
    {
        double phase = 0.0;
        static double blep (double t, double dt)
        {
            if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        }
        float next (double freq, double sr)
        {
            const double dt = juce::jlimit (1.0e-6, 0.45, freq / sr);
            const double v = 2.0 * phase - 1.0 - blep (phase, dt);
            phase += dt;
            if (phase >= 1.0) phase -= 1.0;
            return (float) v;
        }
    };

    /** A synthetic crash cymbal: bright noise through a bank of metallic resonances. */
    Buffer synthCrash (double sr, int len, double decaySeconds, double brightness, juce::uint32 seed)
    {
        Buffer out (2, juce::jmax (1, len));
        const double partials[] = { 3150.0, 4730.0, 5940.0, 7320.0, 8610.0, 10230.0 };
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Random rng ((juce::int64) seed * 31 + ch * 977 + 5);
            juce::IIRFilter hp1, hp2, lp;
            hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 2800.0));
            hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 2800.0));
            lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, juce::jlimit (4000.0, sr * 0.45, 7000.0 + 9000.0 * brightness)));
            Svf res[6];
            for (int k = 0; k < 6; ++k) res[k].set (sr, partials[k] * (ch == 0 ? 1.0 : 1.013), 0.08);
            auto* d = out.getWritePointer (ch);
            for (int i = 0; i < len; ++i)
            {
                const double t = i / sr;
                const float nz = rng.nextFloat() * 2.0f - 1.0f;
                float metal = 0.0f;
                for (auto& r : res) { float l, b, h; r.process (nz, l, b, h); metal += b; }
                float v = hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (nz * 0.6f + metal * 0.12f));
                v = lp.processSingleSampleRaw (v);
                const float env = (float) (std::exp (-t / decaySeconds) * (1.0 - std::exp (-t / 0.002)));
                d[i] = v * env;
            }
        }
        return out;
    }

    /** The user's sample as stereo, without the near-silence at its start (and end, if asked). */
    Buffer trimmedSample (const ClipData& s, bool trimEnd)
    {
        const auto& a = s.audio;
        const int n = a.getNumSamples();
        Buffer out;
        if (n == 0) return out;
        const float peak = a.getMagnitude (0, n);
        if (peak <= 0.0f) return out;
        const float thr = peak * 0.001f;   // -60 dB
        auto loud = [&] (int i)
        {
            for (int ch = 0; ch < a.getNumChannels(); ++ch) if (std::abs (a.getSample (ch, i)) > thr) return true;
            return false;
        };
        int first = 0, last = n - 1;
        while (first < n - 1 && ! loud (first)) ++first;
        if (trimEnd) while (last > first && ! loud (last)) --last;
        first = juce::jmax (0, first - 16);   // keep the attack's very start
        const int len = last - first + 1;
        out.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, a, juce::jmin (ch, a.getNumChannels() - 1), first, len);
        return out;
    }

    /** Riser / Reverse Crash with your own sample: it ends right on the drop (a crash is
        reversed, so its hit lands on the drop). Length limits how much of it plays. */
    void doSampleIntoDrop (const Move& m, const MoveContext& c, Buffer& fx, bool reverse)
    {
        const int n = fx.getNumSamples();
        const int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        Buffer smp = trimmedSample (*c.userSample, true);
        if (L < 256 || smp.getNumSamples() < 64) return;
        if (reverse)
            for (int ch = 0; ch < 2; ++ch)
                std::reverse (smp.getWritePointer (ch), smp.getWritePointer (ch) + smp.getNumSamples());
        const int len = smp.getNumSamples();
        const int take = juce::jmin (len, L), skip = len - take;
        Buffer layer (2, take);
        for (int ch = 0; ch < 2; ++ch)
            layer.copyFrom (ch, 0, smp, ch, skip, take);
        if (skip > 0) layer.applyGainRamp (0, juce::jmin (take / 4, (int) (0.05 * c.sampleRate)), 0.0f, 1.0f);   // cut in gently
        for (int i = 0; i < take; ++i)
        {
            const float e = (float) shapeCurve (m, (double) i / take, 0.0);   // Auto = the sample as it is
            layer.setSample (0, i, layer.getSample (0, i) * e);
            layer.setSample (1, i, layer.getSample (1, i) * e);
        }
        const int tail = juce::jmin (take / 8, (int) (0.002 * c.sampleRate) + 1);
        layer.applyGainRamp (take - tail, tail, 1.0f, 0.0f);
        const float target = c.refRms * (reverse ? 0.4f + 0.8f * m.knob : 0.35f + 0.75f * m.knob);
        addMatched (fx, layer, n - take, take - juce::jmax (1, take / 4), juce::jmax (1, take / 4), target);
    }

    void doRiser (const Move& m, const MoveContext& c, Buffer& fx)
    {
        if (c.userSample != nullptr) { doSampleIntoDrop (m, c, fx, false); return; }
        const int n = fx.getNumSamples();
        const int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        if (L < 256) return;
        const double sr = c.sampleRate;
        Buffer layer (2, L);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Random rng ((juce::int64) m.seed * 13 + ch * 101 + 1);
            Svf bp;
            BlepSaw saws[3];
            juce::IIRFilter hp;
            hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 150.0));
            Svf toneLp;
            auto* d = layer.getWritePointer (ch);
            const double sixteenth = 0.25 * c.samplesPerBeat;
            for (int i = 0; i < L; ++i)
            {
                const double t = (double) i / L;
                const float nz = rng.nextFloat() * 2.0f - 1.0f;
                float v = 0.0f;
                if ((i & 15) == 0)
                    bp.set (sr, 250.0 * std::pow (44.0, std::pow (t, 1.3)), m.mode == 2 ? 0.9 : 1.2);
                float l, b, h;
                bp.process (nz, l, b, h);
                if (m.mode == 0)
                {
                    v = b * 2.2f;
                }
                else if (m.mode == 1)   // uplifter: noise + three detuned saws gliding up
                {
                    // in key: start on the root and rise exactly three octaves to land on it again
                    const double f = c.tonic >= 0 ? c.rootHz (110.0, 180.0) * std::pow (8.0, std::pow (t, 1.4))
                                                  : 180.0 * std::pow (10.0, std::pow (t, 1.4));
                    float tone = 0.0f;
                    const double det[3] = { 1.0, 1.0072, 0.9931 };
                    for (int k = 0; k < 3; ++k) tone += saws[k].next (f * det[k] * (ch == 0 ? 1.0 : 1.002), sr);
                    if ((i & 15) == 0) toneLp.set (sr, juce::jmin (sr * 0.45, f * 4.0), 1.0);
                    float tl, tb, th;
                    toneLp.process (tone * 0.33f, tl, tb, th);
                    v = b * 1.4f + tl;
                }
                else                    // snare wash: 1/16 noise bursts that blur into a wall of noise
                {
                    const double ph = std::fmod ((c.zoneStartInClip + (n - L) + i) / sixteenth, 1.0);
                    const double tau = 0.08 + 0.9 * std::pow (t, 2.0);
                    v = b * 2.0f * (float) std::exp (-ph / tau);
                }
                v = hp.processSingleSampleRaw (v);
                const float env = (float) shapeCurve (m, t, 1.6);
                const int tail = juce::jmin (L / 8, (int) (0.003 * sr) + 1);
                d[i] = v * env * (i > L - tail ? (float) (L - i) / tail : 1.0f);
            }
        }
        addMatched (fx, layer, n - L, L - L / 4, L / 4, c.refRms * (0.35f + 0.75f * m.knob));
    }

    void doRevCrash (const Move& m, const MoveContext& c, Buffer& fx)
    {
        if (c.userSample != nullptr) { doSampleIntoDrop (m, c, fx, true); return; }
        const int n = fx.getNumSamples();
        int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        if (m.mode == 1) L = juce::jmin (L, toSamples (2.0, c));
        if (L < 256) return;
        const double decay = (L / c.sampleRate) * (m.mode == 2 ? 0.55 : 0.35);
        Buffer crash = synthCrash (c.sampleRate, L, decay, m.mode == 1 ? 1.0 : (m.mode == 2 ? 0.0 : 0.6), m.seed);
        for (int ch = 0; ch < 2; ++ch)
            std::reverse (crash.getWritePointer (ch), crash.getWritePointer (ch) + L);   // swells into the drop
        const int tail = (int) (0.002 * c.sampleRate);
        crash.applyGainRamp (L - tail, tail, 1.0f, 0.0f);
        addMatched (fx, crash, n - L, L - L / 6, L / 6, c.refRms * (0.4f + 0.8f * m.knob));
    }

    void doPump (const Move& m, const MoveContext& c, Buffer& main)
    {
        const int n = main.getNumSamples();
        const double stepBeats = juce::jmax (0.125, divisionToBeats (m.division, c.beatsPerBar));
        const float depth = 0.92f * m.knob;
        const float smooth = 1.0f - std::exp (-1.0f / (float) (0.0015 * c.sampleRate));

        // Follow Kick: duck from the kicks / 808s found in the audio (falls back to the grid)
        std::vector<juce::int64> kicks;
        if (m.mode == 3 && c.drums != nullptr)
            for (auto s : c.drums->bass.onsets)
                if (s >= c.zoneStartInClip - (juce::int64) (stepBeats * c.samplesPerBeat) && s < c.zoneStartInClip + n)
                    kicks.push_back (s - c.zoneStartInClip);
        const bool followKick = ! kicks.empty();
        size_t nextKick = 0;
        double sinceKick = 1.0e9;

        float g = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            // phase on the song's beat grid, so the pump locks to the kick
            const double beatPos = (c.zoneStartInClip + i) / c.samplesPerBeat;
            double ph = std::fmod (beatPos / stepBeats, 1.0);
            if (followKick)
            {
                while (nextKick < kicks.size() && kicks[nextKick] <= i)
                    sinceKick = (double) (i - kicks[nextKick++]);
                sinceKick += 1.0;
                ph = juce::jmin (1.0, sinceKick / (stepBeats * c.samplesPerBeat));   // Rate = recovery time
            }
            double duck;
            if (m.mode == 1) duck = ph < 0.3 ? 1.0 : std::exp (-(ph - 0.3) * 16.0);
            else             duck = std::exp (-ph * 5.0);
            float d = depth;
            if (m.mode == 2) d *= (float) shapeCurve (m, (double) i / n, 1.2);
            const float target = 1.0f - d * (float) duck;
            g += (target - g) * smooth;
            for (int ch = 0; ch < main.getNumChannels(); ++ch)
                main.getWritePointer (ch)[i] *= g;
        }
    }

    void doGap (const Move& m, const MoveContext& c, Buffer& main)
    {
        const int n = main.getNumSamples();
        const int L = juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
        if (L < 64) return;
        const int start = n - L;
        const int fade = juce::jmin (L / 3, (int) (0.006 * c.sampleRate) + 1);
        Buffer wet (main.getNumChannels(), L);
        for (int ch = 0; ch < main.getNumChannels(); ++ch)
        {
            if (m.mode == 0) { wet.clear (ch, 0, L); continue; }
            Svf f;
            f.set (c.sampleRate, m.mode == 1 ? 260.0 : 2200.0, 1.2);
            auto* src = main.getReadPointer (ch, start);
            auto* d = wet.getWritePointer (ch);
            for (int i = 0; i < L; ++i)
            {
                float l, b, h;
                f.process (src[i], l, b, h);
                d[i] = m.mode == 1 ? l : h;
            }
        }
        for (int ch = 0; ch < main.getNumChannels(); ++ch)
        {
            auto* d = main.getWritePointer (ch, start);
            auto* w = wet.getReadPointer (ch);
            for (int i = 0; i < L; ++i)
            {
                const float amt = m.knob * (i < fade ? (float) i / fade : 1.0f);
                d[i] = d[i] * (1.0f - amt) + w[i] * amt;
            }
        }
    }

    //==========================================================================
    // FX moves (page 3)
    //==========================================================================
    /** Circular delay line with fractional reads (read before push: delay 1 = the last sample pushed). */
    struct DelayLine
    {
        std::vector<float> d;
        int w = 0;
        void prepare (int maxDelay) { d.assign ((size_t) juce::jmax (8, maxDelay + 4), 0.0f); w = 0; }
        void push (float x) { d[(size_t) w] = x; w = (w + 1) % (int) d.size(); }
        float read (double delay) const
        {
            const int sz = (int) d.size();
            double rp = (double) w - juce::jlimit (1.0, (double) sz - 2.0, delay);
            if (rp < 0) rp += sz;
            const int i0 = (int) rp;
            const float f = (float) (rp - i0);
            const float a = d[(size_t) (i0 % sz)], b = d[(size_t) ((i0 + 1) % sz)];
            return a + (b - a) * f;
        }
    };

    /** First-order allpass (phaser stage). */
    struct Allpass1
    {
        float x1 = 0.0f, y1 = 0.0f;
        float process (float x, float a) { const float y = a * x + x1 - a * y1; x1 = x; y1 = y; return y; }
    };

    /** Schroeder allpass (decorrelator). */
    struct CombAllpass
    {
        std::vector<float> buf;
        int pos = 0;
        float g = 0.6f;
        void prepare (int delay) { buf.assign ((size_t) juce::jmax (1, delay), 0.0f); pos = 0; }
        float process (float x)
        {
            const float wd = buf[(size_t) pos];
            const float w = x + g * wd;
            buf[(size_t) pos] = w;
            pos = (pos + 1) % (int) buf.size();
            return wd - g * w;
        }
    };

    /** Two allpass chains whose outputs stay 90 degrees apart from ~20 Hz to ~20 kHz
        (a polyphase IIR Hilbert pair, after Olli Niemitalo). 'i' and 'q' have the same
        magnitude as the input; q lags i by a quarter cycle at every frequency. */
    struct HilbertPair
    {
        struct Stage
        {
            float c = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
            float process (float x) { const float y = c * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
        };
        Stage a[4], b[4];
        float qDelay = 0.0f;
        HilbertPair()
        {
            const float ca[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            const float cb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            for (int k = 0; k < 4; ++k) { a[k].c = ca[k] * ca[k]; b[k].c = cb[k] * cb[k]; }
        }
        void process (float x, float& i, float& q)
        {
            float va = x, vb = x;
            for (auto& st : a) va = st.process (va);
            for (auto& st : b) vb = st.process (vb);
            i = vb;
            q = qDelay;
            qDelay = va;
        }
    };

    int moveLength (const Move& m, const MoveContext& c, int n)
    {
        return juce::jmin (n, toSamples (regionLen (divisionToBeats (m.division, c.beatsPerBar), c), c));
    }

    void fadeOutEnd (Buffer& b, int start, int len, int fade)
    {
        fade = juce::jmin (fade, len);
        if (fade > 0) b.applyGainRamp (start + len - fade, fade, 1.0f, 0.0f);
    }

    //--------------------------------------------------------------------------
    /** Delay Throw: Throw sends one beat into tempo-synced ping-pong echoes and drops the
        track out under them; Ping-Pong Build and Tape Echo feed more and more of the build
        into the delay. The echoes go to the fx layer. Division = echo time, knob = feedback. */
    void doDelay (const Move& m, const MoveContext& c, Buffer& main, Buffer& fx)
    {
        const int n = main.getNumSamples();
        const double sr = c.sampleRate;
        const bool throwMode = m.mode == 0, tape = m.mode == 2;
        const int L = throwMode ? juce::jmin (n, toSamples (regionLen (c.beatsPerBar, c), c)) : n;
        if (L < 256) return;
        const int start = n - L;
        double D = juce::jlimit (0.25, c.beatsPerBar, divisionToBeats (m.division, c.beatsPerBar)) * c.samplesPerBeat;
        if (throwMode) D = juce::jmin (D, L * 0.5);
        D = juce::jmax (32.0, D);
        const float fb = 0.3f + 0.6f * m.knob;
        const int throwLen = throwMode ? juce::jlimit (64, juce::jmax (64, L / 2), (int) juce::jmax (D, c.samplesPerBeat)) : 0;
        const int throwFade = juce::jmin (throwLen / 4, (int) (0.008 * sr) + 1);

        // what goes into the delay
        Buffer send (2, L);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* sd = send.getWritePointer (ch);
            auto* a = main.getReadPointer (juce::jmin (ch, main.getNumChannels() - 1), start);
            auto* f = fx.getReadPointer (juce::jmin (ch, fx.getNumChannels() - 1), start);
            for (int i = 0; i < L; ++i)
            {
                float g;
                if (throwMode) g = i < throwLen - throwFade ? 1.0f : (i < throwLen ? (float) (throwLen - i) / throwFade : 0.0f);
                else           g = (float) shapeCurve (m, (double) i / L, 1.5);
                sd[i] = (a[i] + f[i]) * g;
            }
        }

        DelayLine dl, dr;
        dl.prepare ((int) (D * 1.01) + 8);
        dr.prepare ((int) (D * 1.01) + 8);
        const float lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (tape ? 2400.0f : 6500.0f) / (float) sr);
        const float hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 170.0f / (float) sr);
        float lp[2] {}, hp[2] {};
        auto tone = [&] (float x, int k) { lp[k] += (x - lp[k]) * lpCoef; hp[k] += (lp[k] - hp[k]) * hpCoef; return lp[k] - hp[k]; };
        Buffer echo (2, L);
        for (int i = 0; i < L; ++i)
        {
            double d = D;
            if (tape) d *= 1.0 + 0.003 * std::sin (twoPi * 0.7 * i / sr);   // a little wow
            const float oL = dl.read (d), oR = dr.read (d);
            const float fL = tone (oL, 0), fR = tone (oR, 1);
            const float sL = send.getSample (0, i), sR = send.getSample (1, i);
            if (tape)
            {
                // tape: each side repeats itself, darker and a little saturated every time
                dl.push (sL + std::tanh (fL * fb * 1.5f) / 1.5f);
                dr.push (sR + std::tanh (fR * fb * 1.5f) / 1.5f);
            }
            else
            {
                // ping-pong: the send enters on the left, then the echoes bounce across
                dl.push (0.5f * (sL + sR) + fR * fb);
                dr.push (fL * fb);
            }
            echo.setSample (0, i, oL);
            echo.setSample (1, i, oR);
        }

        if (throwMode)
        {
            // the track drops out after the throw; the echoes carry it to the drop
            for (Buffer* b : { &main, &fx })
                for (int ch = 0; ch < b->getNumChannels(); ++ch)
                {
                    auto* d = b->getWritePointer (ch, start);
                    for (int i = throwLen - throwFade; i < L; ++i)
                        d[i] *= i < throwLen ? (float) (throwLen - i) / throwFade : 0.0f;
                }
            echo.applyGain (0.85f);
        }
        else
        {
            // keep the echoes under the track: matched over the end of the build
            const float r = rmsOf (echo, L - L / 4, L / 4);
            if (r > 1.0e-9f) echo.applyGain (juce::jmin (2.0f, c.refRms * 0.6f / r));
            // the dry thins out a touch as the echoes take over
            for (int ch = 0; ch < main.getNumChannels(); ++ch)
            {
                auto* d = main.getWritePointer (ch);
                for (int i = 0; i < n; ++i) d[i] *= 1.0f - 0.2f * (float) shapeCurve (m, (double) i / n, 1.5);
            }
        }
        fadeOutEnd (echo, 0, L, juce::jmin (L / 8, (int) (0.003 * sr) + 1));
        for (int ch = 0; ch < fx.getNumChannels(); ++ch)
            fx.addFrom (ch, start, echo, juce::jmin (ch, 1), 0, L);
    }

    //--------------------------------------------------------------------------
    /** Bitcrush: bit depth (16 -> 3 bits) and/or sample rate (down to ~1.4 kHz) crushed
        more and more into the drop. Knob = how far it goes. */
    void doBitcrush (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const int L = moveLength (m, c, n);
        if (L < 256) return;
        const int start = n - L;
        const float norm = juce::jmax (0.05f, c.refPeak);       // crush relative to the track's own level
        const bool crushBits = m.mode != 1, crushRate = m.mode != 0, drive = m.mode == 2;
        const double minRate = juce::jmin (1.0, 1400.0 / c.sampleRate);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch, start);
            double hold = 1.0;
            float held = 0.0f;
            for (int i = 0; i < L; ++i)
            {
                const float amt = m.knob * (float) shapeCurve (m, (double) i / L, 1.3);
                float x = d[i] / norm;
                if (drive)
                {
                    const float k = 1.0f + 2.0f * amt;
                    x = std::tanh (x * k) / std::tanh (k);
                }
                if (crushRate)
                {
                    hold += std::pow (minRate, (double) amt);
                    if (hold >= 1.0) { hold -= std::floor (hold); held = x; }
                    x = held;
                }
                if (crushBits)
                {
                    const float q = std::pow (2.0f, 15.0f - 14.0f * std::pow (amt, 0.7f));   // 16 bits down to 2
                    x = std::round (x * q) / q;
                }
                d[i] = x * norm;
            }
        }
    }

    //--------------------------------------------------------------------------
    /** Phaser: Barber-Pole Rise (notches that glide up forever - a Shepard
        tone for the whole mix), Phaser Sweep, or Flanger. The sweep speeds up into the drop. */
    void doPhaser (const Move& m, const MoveContext& c, Buffer& b)
    {
        const int n = b.getNumSamples();
        const int L = moveLength (m, c, n);
        if (L < 256) return;
        const int start = n - L;
        const double sr = c.sampleRate, spb = c.samplesPerBeat;
        const float depth = 0.35f + 0.65f * m.knob;
        const double pi = juce::MathConstants<double>::pi;
        auto coef = [sr, pi] (double hz)
        {
            const double t = std::tan (pi * juce::jlimit (20.0, sr * 0.45, hz) / sr);
            return (float) ((t - 1.0) / (t + 1.0));
        };

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch, start);
            if (m.mode == 2)
            {
                // flanger: a short swept delay with feedback
                DelayLine dl;
                dl.prepare ((int) (0.012 * sr) + 8);
                double phase = ch == 0 ? 0.0 : 0.25;
                for (int i = 0; i < L; ++i)
                {
                    const float ramp = (float) shapeCurve (m, (double) i / L, 1.2);
                    phase += 0.125 * std::pow (16.0, (double) ramp) / spb;
                    const double lfo = 0.5 - 0.5 * std::cos (twoPi * phase);
                    const float y = dl.read ((0.0006 + 0.0065 * lfo) * sr);
                    const float x = d[i];
                    dl.push (x + y * 0.62f * m.knob);
                    d[i] = x + (0.5f * (x + y) - x) * depth * ramp;
                }
                continue;
            }

            if (m.mode == 0)
            {
                // Barber-pole: a static 8-stage phaser whose wet path is phase-rotated (via a
                // Hilbert pair) by an ever-increasing angle. Every notch glides up, and as one
                // leaves the top a new one enters at the bottom - an endless rise with no reset.
                // (Esqueda, Valimaki & Parker, "Barberpole phasing and flanging illusions", DAFx-15.)
                Allpass1 ap[8];
                float apc[8];
                for (int k = 0; k < 8; ++k) apc[k] = coef (80.0 * std::pow (100.0, k / 7.0));
                HilbertPair dryPair, wetPair;
                double psi = ch == 0 ? 0.0 : 0.5 * pi;
                const int xfade = juce::jmin (L / 4, (int) (0.01 * sr) + 1);
                for (int i = 0; i < L; ++i)
                {
                    const float ramp = (float) shapeCurve (m, (double) i / L, 1.2);
                    psi += twoPi * 0.25 * std::pow (6.0, (double) ramp) / spb;     // 1/4 -> 1.5 notch steps per beat
                    const float x = d[i];
                    float v = x;
                    for (int k = 0; k < 8; ++k) v = ap[k].process (v, apc[k]);
                    float di, dq, wi, wq;
                    dryPair.process (x, di, dq);
                    wetPair.process (v, wi, wq);
                    const float rotated = wi * (float) std::cos (psi) - wq * (float) std::sin (psi);
                    const float wet = 0.5f * (di + rotated);
                    float y = di + (wet - di) * depth * ramp;   // mixed in the pair's own phase, so no extra combing
                    if (i < xfade) y = x + (y - x) * (float) i / xfade;
                    d[i] = y;
                }
                continue;
            }

            // Phaser Sweep: six stages swept by an LFO that speeds up into the drop
            Allpass1 ap[6];
            float last = 0.0f, a = 0.0f;
            double phase = ch == 0 ? 0.0 : 0.25;
            const float feedback = 0.25f + 0.35f * m.knob;
            for (int i = 0; i < L; ++i)
            {
                const float ramp = (float) shapeCurve (m, (double) i / L, 1.2);
                phase += 0.25 * std::pow (8.0, (double) ramp) / spb;            // 1/4 -> 2 sweeps per beat
                if ((i & 15) == 0)
                    a = coef (180.0 * std::pow (5500.0 / 180.0, 0.5 - 0.5 * std::cos (twoPi * phase)));
                const float x = d[i];
                float y = x + last * feedback;
                for (auto& st : ap) y = st.process (y, a);
                last = y;
                d[i] = x + (0.5f * (x + y) - x) * depth * ramp;
            }
        }
    }

    //--------------------------------------------------------------------------
    /** Stereo width: Collapse To Mono (the drop then hits full width), Widen Out, or Mono Snap. */
    void doWidth (const Move& m, const MoveContext& c, Buffer& b)
    {
        if (b.getNumChannels() < 2) return;
        const int n = b.getNumSamples();
        const int L = moveLength (m, c, n);
        if (L < 256) return;
        const int start = n - L;
        const double sr = c.sampleRate;
        auto* l = b.getWritePointer (0, start);
        auto* r = b.getWritePointer (1, start);
        const int snapFade = (int) (0.01 * sr) + 1;
        CombAllpass dec[3];
        const double decMs[3] = { 4.7, 7.1, 11.3 };
        for (int k = 0; k < 3; ++k) dec[k].prepare ((int) (decMs[k] * 0.001 * sr));
        const float hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) sr);
        const float eCoef = 1.0f - std::exp (-1.0f / (float) (0.03 * sr));
        float hpState = 0.0f, ein = 1.0e-9f, eout = 1.0e-9f;
        for (int i = 0; i < L; ++i)
        {
            const float amt = m.mode == 2 ? m.knob * juce::jmin (1.0f, (float) i / snapFade)
                                          : m.knob * (float) shapeCurve (m, (double) i / L, 1.0);
            const float mid = 0.5f * (l[i] + r[i]);
            float side = 0.5f * (l[i] - r[i]);
            if (m.mode == 1)
            {
                // widen: more side, plus a decorrelated copy of the mid above the bass
                hpState += (mid - hpState) * hpCoef;
                float dcr = mid - hpState;
                for (auto& a : dec) dcr = a.process (dcr);
                side = side * (1.0f + 1.2f * amt) + dcr * 0.55f * amt;
            }
            else
            {
                side *= 1.0f - amt;
            }
            float nl = mid + side, nr = mid - side;
            if (m.mode == 1)
            {
                // widening adds energy: keep the level where it was
                ein += (l[i] * l[i] + r[i] * r[i] - ein) * eCoef;
                eout += (nl * nl + nr * nr - eout) * eCoef;
                const float g = juce::jlimit (0.6f, 1.0f, std::sqrt (ein / juce::jmax (1.0e-12f, eout)));
                nl *= g;
                nr *= g;
            }
            l[i] = nl;
            r[i] = nr;
        }
    }

    //--------------------------------------------------------------------------
    /** Vinyl: Baby Scratch (forward-back strokes on the beat grid), Rewind (the record is
        yanked backwards) or Brake (the turntable's power is cut). Knob = speed. */
    void doVinyl (const Move& m, const MoveContext& c, Buffer& b, Buffer* crackle, bool canReadClip)
    {
        const int n = b.getNumSamples();
        const int L = moveLength (m, c, n);
        if (L < 256) return;
        const int start = n - L;
        const double sr = c.sampleRate, spb = c.samplesPerBeat, pi = juce::MathConstants<double>::pi;

        // the platter's motion: where the needle is (in zone samples) and how fast it moves
        std::vector<double> pos ((size_t) L);
        std::vector<float> speed ((size_t) L);
        juce::Random rng ((juce::int64) m.seed * 7 + 3);
        double maxSpeed = 1.0;
        if (m.mode == 0)
        {
            const double stroke = spb * 0.5 * std::pow (0.25, (double) m.knob);   // 1/8 note down to 1/32
            double amp = 0.55 * stroke;
            int lastStroke = 0;
            for (int i = 0; i < L; ++i)
            {
                const double sp = i / stroke;
                const int k = (int) sp;
                if (k != lastStroke) { lastStroke = k; amp = 0.55 * stroke * (0.8 + 0.4 * rng.nextDouble()); }
                const double ph = sp - k, s = std::sin (pi * ph);
                pos[(size_t) i] = start + amp * s * s;
                speed[(size_t) i] = (float) (amp * pi * std::sin (twoPi * ph) / stroke);
            }
            maxSpeed = 0.66 * pi;
        }
        else if (m.mode == 1)
        {
            const double top = 3.0 + 9.0 * m.knob, grab = 0.12;
            double p = start;
            for (int i = 0; i < L; ++i)
            {
                const double t = (double) i / L;
                double v;
                if (t < grab) { const double u = t / grab; v = 1.0 - (1.0 + top) * u * u * (3.0 - 2.0 * u); }
                else          v = -top * (1.0 - 0.35 * (t - grab) / (1.0 - grab));
                pos[(size_t) i] = p;
                speed[(size_t) i] = (float) v;
                p += v;
            }
            maxSpeed = top;
        }
        else
        {
            const double stopAt = 1.0 - 0.6 * m.knob;    // faster brake = stops earlier
            const double wow = rng.nextDouble();
            double p = start;
            for (int i = 0; i < L; ++i)
            {
                double v = juce::jmax (0.0, 1.0 - ((double) i / L) / stopAt);
                v *= 1.0 + 0.004 * std::sin (twoPi * (wow + 0.55 * i / sr));
                pos[(size_t) i] = p;
                speed[(size_t) i] = (float) v;
                p += v;
            }
        }

        // the audio to play from: the zone, plus the untouched clip before it if we rewind past it
        double minPos = 0.0;
        for (auto p : pos) minPos = juce::jmin (minPos, p);
        const int P = canReadClip && c.clip != nullptr
                        ? (int) std::min (c.zoneStartInClip, std::max ((juce::int64) 0, (juce::int64) std::ceil (-minPos) + 8)) : 0;
        Buffer ext (b.getNumChannels(), P + n);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            if (P > 0) ext.copyFrom (ch, 0, *c.clip, juce::jmin (ch, c.clip->getNumChannels() - 1), (int) (c.zoneStartInClip - P), P);
            ext.copyFrom (ch, P, b, ch, 0, n);
        }
        // fast playback would alias: a band-limited copy to blend to as the platter speeds up
        Buffer extLo;
        if (maxSpeed > 1.3)
        {
            extLo.makeCopyOf (ext);
            const double cut = 0.45 * sr / maxSpeed;
            for (int ch = 0; ch < extLo.getNumChannels(); ++ch)
            {
                Svf f[3];
                for (auto& x : f) x.set (sr, cut, 1.0);
                auto* d = extLo.getWritePointer (ch);
                for (int i = 0; i < extLo.getNumSamples(); ++i)
                {
                    float v = d[i], lo, bp, hi;
                    for (auto& x : f) { x.process (v, lo, bp, hi); v = lo; }
                    d[i] = v;
                }
            }
        }

        const Buffer src (b);
        const int extN = ext.getNumSamples();
        const int tail = juce::jmin (L / 8, (int) (0.003 * sr) + 1);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch, start);
            const float* s = ext.getReadPointer (ch);
            const float* sl = extLo.getNumSamples() > 0 ? extLo.getReadPointer (ch) : nullptr;
            Svf tone;
            for (int i = 0; i < L; ++i)
            {
                const double p = pos[(size_t) i] + P;
                const float av = std::abs (speed[(size_t) i]);
                float v = readInterp (s, extN, p);
                if (sl != nullptr && av > 1.0f)
                {
                    const float w = juce::jlimit (0.0f, 1.0f, (av - 1.0f) / (float) (maxSpeed - 1.0));
                    v += (readInterp (sl, extN, p) - v) * w;
                }
                // a slow-moving record sounds dull and quiet
                if ((i & 15) == 0)
                    tone.set (sr, 150.0 + 18000.0 * std::pow (juce::jmin (1.0, (double) av), 1.5), 1.4);
                float lo, bp, hi;
                tone.process (v, lo, bp, hi);
                float g = std::pow (juce::jmin (1.0f, av * (m.mode == 0 ? 1.3f : 1.0f)), m.mode == 0 ? 0.5f : 0.25f);
                g *= m.mode == 0 ? 0.8f : (m.mode == 1 ? 0.85f : 1.0f);
                if (i >= L - tail) g *= (float) (L - i) / tail;
                d[i] = lo * g;
            }
        }
        crossfadeInto (b, src, start, juce::jmin (L / 8, (int) (0.004 * sr) + 1));

        // the brake's needle crackle (into the fx layer)
        if (crackle != nullptr && m.mode == 2)
        {
            Buffer cr (2, L);
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::Random cr1 ((juce::int64) m.seed * 17 + ch * 29 + 11);
                auto* d = cr.getWritePointer (ch);
                float hissLp = 0.0f;
                int clickLeft = 0;
                float clickAmp = 0.0f;
                for (int i = 0; i < L; ++i)
                {
                    const float v = speed[(size_t) i];
                    hissLp += ((cr1.nextFloat() * 2.0f - 1.0f) - hissLp) * 0.25f;
                    float x = hissLp * 0.05f;
                    if (clickLeft == 0 && cr1.nextFloat() < (float) (10.0 / sr))
                    {
                        clickLeft = 6 + cr1.nextInt (24);
                        clickAmp = (0.4f + 0.6f * cr1.nextFloat()) * (cr1.nextBool() ? 1.0f : -1.0f);
                    }
                    if (clickLeft > 0) { x += clickAmp * (float) clickLeft / 30.0f; --clickLeft; }
                    const float fadeIn = juce::jmin (1.0f, (float) i / (float) (0.02 * sr));
                    d[i] = x * std::sqrt (juce::jmax (0.0f, v)) * fadeIn;
                }
            }
            cr.applyGain (c.refRms * 0.5f);
            fadeOutEnd (cr, 0, L, tail);
            for (int ch = 0; ch < crackle->getNumChannels(); ++ch)
                crackle->addFrom (ch, start, cr, juce::jmin (ch, 1), 0, L);
        }
    }

    //--------------------------------------------------------------------------
    /** Pre-Verb: the drop's own downbeat (or first bar, or the snare) through a big reverb,
        played backwards so the tail swells up into the drop. Goes to the fx layer. */
    void doPreVerb (const Move& m, const MoveContext& c, Buffer& fx)
    {
        const int n = fx.getNumSamples();
        const int L = moveLength (m, c, n);
        if (L < 512) return;
        const double sr = c.sampleRate;

        Buffer hit;
        if (m.mode == 2)
        {
            Buffer fallback;
            hit.makeCopyOf (snareHit (c, fallback));
        }
        else
        {
            const int H = toSamples (m.mode == 1 ? c.beatsPerBar : 1.0, c);
            hit.setSize (2, juce::jmax (64, H));
            hit.clear();
            if (c.clip != nullptr && c.clip->getNumSamples() > 0)
            {
                const auto clipN = (juce::int64) c.clip->getNumSamples();
                juce::int64 from = c.dropInClip;
                if (from < 0 || from + 256 > clipN)   // no audio after the drop: use the build's last downbeat
                    from = std::max ((juce::int64) 0, c.zoneStartInClip + n - (juce::int64) toSamples (c.beatsPerBar, c));
                const int len = (int) std::min ((juce::int64) hit.getNumSamples(), clipN - from);
                for (int ch = 0; ch < 2; ++ch)
                    if (len > 0) hit.copyFrom (ch, 0, *c.clip, juce::jmin (ch, c.clip->getNumChannels() - 1), (int) from, len);
            }
            fadeOutEnd (hit, 0, hit.getNumSamples(), juce::jmin (hit.getNumSamples() / 4, (int) (0.01 * sr) + 1));
        }
        if (hit.getNumSamples() == 0) return;

        FdnReverb rv;
        auto st = atmosSettings (m.mode == 1 ? 1 : 0, 0.5f);
        st.rt60 = juce::jlimit (1.5, 9.0, 1.1 * L / sr);
        st.predelayMs = 0.0;
        rv.prepare (sr, st);
        Buffer wet (2, L);
        const int hn = hit.getNumSamples(), hch = hit.getNumChannels();
        // a single hit: its reverb from the first moment (reversed, it "sucks" into the hit);
        // a whole bar: only the tail after the bar, so it swells instead of smearing the bar
        const int skip = m.mode == 1 ? hn : 0;
        for (int i = 0; i < L + skip; ++i)
        {
            const float inL = i < hn ? hit.getSample (0, i) : 0.0f;
            const float inR = i < hn ? hit.getSample (juce::jmin (1, hch - 1), i) : 0.0f;
            float l, r;
            rv.process (inL, inR, l, r);
            if (i >= skip)
            {
                wet.setSample (0, i - skip, l);
                wet.setSample (1, i - skip, r);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = wet.getWritePointer (ch);
            std::reverse (d, d + L);                                   // the tail now swells up to the drop
            for (int i = 0; i < L; ++i) d[i] *= (float) shapeCurve (m, (double) i / L, 0.0);
        }
        wet.applyGainRamp (0, juce::jmin (L / 10, (int) (0.05 * sr)), 0.0f, 1.0f);
        fadeOutEnd (wet, 0, L, (int) (0.002 * sr) + 1);
        addMatched (fx, wet, n - L, L - L / 6, L / 6, c.refRms * (0.4f + 0.9f * m.knob));
    }
}

//==============================================================================
double afterDropTailBeats (const Move& m, double bpb)
{
    const double beats = divisionToBeats (m.division, bpb);
    return m.type == MoveType::Impact ? juce::jmax (bpb, beats) : juce::jmax (bpb * 0.5, beats);
}

void renderAfterDrop (const Move& m, const MoveContext& c, Buffer& post)
{
    if (! m.enabled || ! playsAfterDrop (m.type)) return;
    const double sr = c.sampleRate;
    const double tailBeats = c.lengthOverride >= 0.0 ? c.lengthOverride : afterDropTailBeats (m, c.beatsPerBar);
    const int T = juce::jmin (post.getNumSamples(), (int) std::llround (tailBeats * c.samplesPerBeat));
    if (T < 256) return;
    const float level = 0.45f + 0.65f * m.knob;
    const int firstHalfSec = juce::jmin (T, (int) (0.5 * sr));

    // your own sample on the downbeat: its attack lands right on the drop
    if (c.userSample != nullptr)
    {
        Buffer smp = trimmedSample (*c.userSample, false);
        if (smp.getNumSamples() < 64) return;
        const int len = juce::jmin (T, smp.getNumSamples());
        Buffer hit (2, len);
        for (int ch = 0; ch < 2; ++ch) hit.copyFrom (ch, 0, smp, ch, 0, len);
        const int rel = len < smp.getNumSamples() ? juce::jmin (len / 4, (int) (0.08 * sr)) : juce::jmin (len / 8, (int) (0.002 * sr) + 1);
        hit.applyGainRamp (len - rel, rel, 1.0f, 0.0f);
        const int win = juce::jmin (len, firstHalfSec);
        const float target = m.type == MoveType::Boom ? juce::jlimit (c.refRms * 0.5f, c.refRms * 1.2f, c.lowRms * 2.0f) * level
                                                      : c.refRms * 1.4f * level;
        addMatched (post, hit, 0, 0, win, target);
        return;
    }

    if (m.type == MoveType::Boom)
    {
        Buffer boom (2, T);
        double phase = 0.0;
        for (int i = 0; i < T; ++i)
        {
            const double t = i / sr;
            double f, env;
            // each glides down onto its end note, which is the song's root when the key is known
            if (m.mode == 1)      { const double e = c.rootHz (36.0, 40.0); f = e + e * 1.125 * std::exp (-t / 0.05); env = std::exp (-t / 0.35); }   // punchy
            else if (m.mode == 2) { const double e = c.rootHz (26.0, 26.0); f = e + e * 1.77 * std::pow (1.0 - (double) i / T, 1.6); env = std::exp (-t / (T / sr * 0.6)); } // long dive
            else                  { const double e = c.rootHz (28.0, 30.0); f = e + e * 0.87 * std::exp (-t / 0.35); env = std::exp (-t / 0.9); }   // deep
            phase += f / sr;
            const float atk = (float) juce::jmin (1.0, t / 0.002);
            const float v = (float) std::tanh (1.5 * std::sin (juce::MathConstants<double>::twoPi * phase)) * (float) env * atk;
            boom.setSample (0, i, v);
            boom.setSample (1, i, v);
        }
        const int rel = juce::jmin (T / 4, (int) (0.05 * sr));
        boom.applyGainRamp (T - rel, rel, 1.0f, 0.0f);
        addMatched (post, boom, 0, 0, firstHalfSec, juce::jlimit (c.refRms * 0.5f, c.refRms * 1.2f, c.lowRms * 2.0f) * level);
        return;
    }

    // Impact
    Buffer hit (2, T);
    hit.clear();
    if (m.mode != 2)
    {
        // transient crack + thump + body
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Random rng ((juce::int64) m.seed * 7 + ch * 3 + 11);
            juce::IIRFilter hp, bp;
            hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 900.0));
            bp.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 180.0, 1.0));
            double phase = 0.0;
            for (int i = 0; i < juce::jmin (T, (int) (1.2 * sr)); ++i)
            {
                const double t = i / sr;
                const float nz = rng.nextFloat() * 2.0f - 1.0f;
                const double thump = c.rootHz (40.0, 48.0);   // the body of the hit sits on the root
                phase += (thump + thump * 0.83 * std::exp (-t / 0.04)) / sr;
                const float v = hp.processSingleSampleRaw (nz) * (float) std::exp (-t / 0.09) * 0.8f
                              + bp.processSingleSampleRaw (nz) * (float) std::exp (-t / 0.18) * 1.5f
                              + (float) std::sin (juce::MathConstants<double>::twoPi * phase) * (float) std::exp (-t / 0.28);
                hit.setSample (ch, i, v * (float) juce::jmin (1.0, t / 0.0008));
            }
        }
    }
    if (m.mode != 1)
    {
        auto crash = synthCrash (sr, T, juce::jmin (2.8, T / sr * 0.45), 0.7, m.seed + 99);
        hit.addFrom (0, 0, crash, 0, 0, T, 0.9f);
        hit.addFrom (1, 0, crash, 1, 0, T, 0.9f);
    }
    if (m.mode == 2)
    {
        // downlifter: a noise sweep falling away from the drop
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Random rng ((juce::int64) m.seed * 5 + ch + 21);
            Svf f;
            for (int i = 0; i < T; ++i)
            {
                const double t = (double) i / T;
                if ((i & 15) == 0) f.set (sr, 9000.0 * std::pow (300.0 / 9000.0, std::pow (t, 0.7)), 1.1);
                float l, b, h;
                f.process (rng.nextFloat() * 2.0f - 1.0f, l, b, h);
                hit.addSample (ch, i, b * 1.6f * (float) std::pow (1.0 - t, 1.5));
            }
        }
    }
    // a short room on the hit
    {
        FdnReverb rv;
        auto st = atmosSettings (0, 0.45f);
        rv.prepare (sr, st);
        for (int i = 0; i < T; ++i)
        {
            float l, r;
            rv.process (hit.getSample (0, i), hit.getSample (1, i), l, r);
            hit.addSample (0, i, l * 0.5f);
            hit.addSample (1, i, r * 0.5f);
        }
    }
    const int rel = juce::jmin (T / 4, (int) (0.08 * sr));
    hit.applyGainRamp (T - rel, rel, 1.0f, 0.0f);
    addMatched (post, hit, 0, 0, firstHalfSec, c.refRms * 1.4f * level);
}

std::pair<double, double> moveRegion (const Move& m, double Z, double bpb)
{
    if (m.customRegion)
        return { m.regionStart, m.regionEnd };
    const double L = divisionToBeats (m.division, bpb);
    const auto last = [Z] (double len) { return std::make_pair (-juce::jmin (Z, len), 0.0); };
    switch (m.type)
    {
        case MoveType::Reverse:   return m.mode == 2 ? last (Z) : last (L);
        case MoveType::Stutter:   return last (stutterSpanBeats (m.knob, bpb));
        case MoveType::TapeStop:
        case MoveType::Riser:
        case MoveType::Gap:
        case MoveType::Bitcrush:
        case MoveType::Phaser:
        case MoveType::Width:
        case MoveType::Scratch:
        case MoveType::PreVerb:   return last (L);
        case MoveType::DelayThrow: return m.mode == 0 ? last (bpb) : last (Z);
        case MoveType::RevCrash:  return last (m.mode == 1 ? juce::jmin (L, 2.0) : L);
        case MoveType::DrumMoves: return m.mode == 0 ? last (4.0 * bpb) : (m.mode == 2 ? last (2.0 * bpb) : last (Z));
        case MoveType::Bass808:   return last (m.mode == 0 ? bpb * 0.5 : bpb);
        case MoveType::Hats:      return last (4.0 * bpb);
        case MoveType::Impact:
        case MoveType::Boom:      return { 0.0, afterDropTailBeats (m, bpb) };
        default:                  return last (Z);   // chops, pitch, filter, atmos, pump: the whole build
    }
}

void applyMove (const Move& m, const MoveContext& c, Buffer& main, Buffer& fx)
{
    if (! m.enabled || main.getNumSamples() < 64)
        return;

    // the fx layer only needs processing once something has been added to it
    const bool fxLive = fx.getMagnitude (0, fx.getNumSamples()) > 0.0f;

    switch (m.type)
    {
        case MoveType::Reverse:   doReverse (m, c, main, false); if (fxLive) doReverse (m, c, fx, true); break;
        case MoveType::Chops:     doChops (m, c, main);    if (fxLive) doChops (m, c, fx);    break;
        case MoveType::Stutter:   doStutter (m, c, main);  if (fxLive) doStutter (m, c, fx);  break;
        case MoveType::Pitch:     doPitch (m, c, main);    if (fxLive) doPitch (m, c, fx);    break;
        case MoveType::TapeStop:  doTapeStop (m, c, main); if (fxLive) doTapeStop (m, c, fx); break;
        case MoveType::Filter:    doFilter (m, c, main);   if (fxLive) doFilter (m, c, fx);   break;
        case MoveType::DrumMoves: doDrumMoves (m, c, main, fx); break;
        case MoveType::Bass808:   do808 (m, c, fx); break;
        case MoveType::Hats:      doHats (m, c, fx); break;
        case MoveType::Atmos:     doAtmos (m, c, main, fx); break;
        case MoveType::Riser:     doRiser (m, c, fx); break;
        case MoveType::RevCrash:  doRevCrash (m, c, fx); break;
        case MoveType::Pump:      doPump (m, c, main); break;
        case MoveType::Gap:       doGap (m, c, main); break;
        case MoveType::DelayThrow: doDelay (m, c, main, fx); break;
        case MoveType::Bitcrush:  doBitcrush (m, c, main); if (fxLive) doBitcrush (m, c, fx); break;
        case MoveType::Phaser:    doPhaser (m, c, main);   if (fxLive) doPhaser (m, c, fx);   break;
        case MoveType::Width:     doWidth (m, c, main);    if (fxLive) doWidth (m, c, fx);    break;
        case MoveType::Scratch:
            if (fxLive) doVinyl (m, c, fx, nullptr, false);
            doVinyl (m, c, main, &fx, true);   // the brake's crackle goes to the fx layer
            break;
        case MoveType::PreVerb:   doPreVerb (m, c, fx); break;
        case MoveType::Impact:
        case MoveType::Boom:      break;   // rendered after the drop (renderAfterDrop)
        default: break;
    }
}

} // namespace km
