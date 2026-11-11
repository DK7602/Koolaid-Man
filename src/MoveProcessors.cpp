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
                if (rng.nextFloat() < depth * 0.8f)
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

    void doRiser (const Move& m, const MoveContext& c, Buffer& fx)
    {
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
        case MoveType::Gap:       return last (L);
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
        case MoveType::Impact:
        case MoveType::Boom:      break;   // rendered after the drop (renderAfterDrop)
        default: break;
    }
}

} // namespace km
