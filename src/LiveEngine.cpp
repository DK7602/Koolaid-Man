#include "LiveEngine.h"

namespace km
{

namespace
{
    float coefFor (double seconds, double sr) { return (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0, seconds * sr))); }
}

//==============================================================================
const char* LiveEngine::padName (int p)
{
    static const char* names[numPads] = { "STUTTER", "ROLL", "FILTER", "RISER", "TAPE STOP", "ECHO OUT", "WASH", "CUT" };
    return names[juce::jlimit (0, numPads - 1, p)];
}

const char* LiveEngine::padHint (int p)
{
    static const char* hints[numPads] = {
        "Repeats the current 1/8 note, locked to the beat",
        "A stutter that rolls faster every beat you hold it: 1/16, 1/32, 1/64",
        "Low-pass sweep down while held (over 2 bars); opens back up when you let go",
        "High-pass sweep up plus a rising noise riser while held",
        "The tape slows to a stop over one beat; let go to bring the track back",
        "Throws the moment into dotted-1/8 echoes and drops the track out under them",
        "A huge reverb swells up while held and rings out when you let go",
        "Silence while held - the breath before the drop (echo and reverb tails keep ringing)" };
    return hints[juce::jlimit (0, numPads - 1, p)];
}

juce::String LiveEngine::noteName (int p)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int note = firstNote + p;
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);   // C3 = 60
}

//==============================================================================
void LiveEngine::Svf::set (double sampleRate, double hz, double res)
{
    hz = juce::jlimit (16.0, sampleRate * 0.45, hz);
    const float g = (float) std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
    k = (float) res;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void LiveEngine::Svf::process (float x, float& lpOut, float& bpOut, float& hpOut)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lpOut = v2; bpOut = v1; hpOut = x - k * v1 - v2;
}

//==============================================================================
void LiveEngine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    histSize = (int) (sr * 8.0) + 8;
    for (auto& h : hist) h.assign ((size_t) histSize, 0.0f);
    dSize = (int) (sr * 2.6) + 8;
    dl.assign ((size_t) dSize, 0.0f);
    dr.assign ((size_t) dSize, 0.0f);
    static const double baseMs[8] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    const double rt60 = 4.5;
    for (int k = 0; k < 8; ++k)
    {
        fdnLen[k] = juce::jmax (16, (int) (baseMs[k] * 1.5 * sr / 1000.0));
        fdn[k].assign ((size_t) fdnLen[k], 0.0f);
        fdnGain[k] = (float) std::pow (10.0, -3.0 * fdnLen[k] / (rt60 * sr));
    }
    prepared = true;
    reset();
}

void LiveEngine::reset()
{
    for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
    std::fill (dl.begin(), dl.end(), 0.0f);
    std::fill (dr.begin(), dr.end(), 0.0f);
    for (int k = 0; k < 8; ++k) { std::fill (fdn[k].begin(), fdn[k].end(), 0.0f); fdnPos[k] = 0; fdnLp[k] = 0.0f; }
    written = 0;
    dW = 0;
    for (int p = 0; p < numPads; ++p) { on[p] = false; level[p] = 0.0f; activity[(size_t) p].store (0.0f); }
    stutEnv = tapeEnv = filterProg = riserProg = echoHold = washSend = washDuck = cutEnv = 0.0f;
    echoSendLeft = 0;
    for (auto& t : echoTone) t = 0.0f;
    for (int ch = 0; ch < 2; ++ch) { lp[ch].clear(); hp[ch].clear(); noiseBp[ch].clear(); tapeLp[ch] = 0.0f; }
    inPow = 0.0f;
    tailsLive = false;
    quietSamples = 0;
    idle = true;
}

float LiveEngine::histAt (int ch, double pos) const
{
    if (pos < 0.0) return 0.0f;
    const auto i0 = (juce::int64) pos;
    const float f = (float) (pos - (double) i0);
    const auto& h = hist[ch];
    const float a = h[(size_t) (i0 % histSize)];
    const float b = i0 + 1 < written ? h[(size_t) ((i0 + 1) % histSize)] : a;
    return a + (b - a) * f;
}

void LiveEngine::press (int p, float intensity, double bpm, double ppq)
{
    on[p] = true;
    level[p] = intensity;
    pressedAt[p] = written;
    const double spb = 60.0 * sr / bpm;
    switch (p)
    {
        case stutter:
        case roll:
        {
            // the slice is the 1/8 (or 1/16) the playhead is in right now: it keeps playing,
            // then repeats from the grid line, so the stutter is locked to the beat
            const double sliceBeats = p == stutter ? 0.5 : 0.25;
            slice0 = sliceLen = juce::jmax (32, (int) std::lround (sliceBeats * spb));
            double off = std::fmod (ppq, sliceBeats);
            if (off < 0.0) off += sliceBeats;
            int since = (int) std::lround (off * spb);
            if (since >= sliceLen) since = 0;
            sliceStart = written - since;
            stutPos = since;
            break;
        }
        case tapeStop:
            tapeRead = (double) written;
            tapeDur = juce::jmax (64.0, spb);
            break;
        case echo:
            echoSendLen = echoSendLeft = juce::jmax (64, (int) std::lround (0.5 * spb));   // the 1/8 note you threw
            echoLevel = 0.6f + 0.4f * intensity;
            echoFb = 0.5f + 0.2f * intensity;
            tailsLive = true;
            quietSamples = 0;
            break;
        case wash:
            tailsLive = true;
            quietSamples = 0;
            break;
        default: break;
    }
}

//==============================================================================
void LiveEngine::process (juce::AudioBuffer<float>& io, int start, int num, const float* held, double bpm, double ppq)
{
    if (! prepared || num <= 0 || io.getNumChannels() == 0)
        return;
    bpm = juce::jlimit (30.0, 300.0, bpm > 0.0 ? bpm : 120.0);
    const double spb = 60.0 * sr / bpm;

    for (int p = 0; p < numPads; ++p)
    {
        const bool want = held[p] > 0.0f;
        if (want && ! on[p]) press (p, juce::jlimit (0.05f, 1.0f, held[p]), bpm, ppq);
        else if (! want && on[p]) on[p] = false;
    }
    bool anyOn = false;
    for (bool b : on) anyOn |= b;

    const int nCh = juce::jmin (2, io.getNumChannels());
    const float powCoef = coefFor (0.3, sr);
    const bool active = anyOn || stutEnv > 0.0f || tapeEnv > 0.0f || filterProg > 0.0f || riserProg > 0.0f
                        || echoHold > 0.0f || cutEnv > 0.0f || washDuck > 0.0f || washSend > 0.0f || tailsLive;
    if (! active)
    {
        // nothing to do: just remember the audio (for stutters / tape stops) and its level
        for (int i = 0; i < num; ++i)
        {
            const float x0 = io.getSample (0, start + i), x1 = nCh > 1 ? io.getSample (1, start + i) : x0;
            const auto w = (size_t) (written % histSize);
            hist[0][w] = x0;
            hist[1][w] = x1;
            inPow += (0.5f * (x0 * x0 + x1 * x1) - inPow) * powCoef;
            ++written;
        }
        for (auto& a : activity) a.store (0.0f, std::memory_order_relaxed);
        idle = true;
        return;
    }
    idle = false;

    const float cInst = coefFor (0.0005, sr), cFast = coefFor (0.003, sr), cMid = coefFor (0.008, sr), cSlow = coefFor (0.05, sr);
    const float filterStep = (float) (1.0 / (8.0 * spb)), filterBack = (float) (1.0 / (0.25 * sr));
    const float riserStep = (float) (1.0 / (8.0 * spb)), riserBack = (float) (1.0 / (0.12 * sr));
    const float washStep = (float) (1.0 / (4.0 * spb)), washBack = (float) (1.0 / (0.05 * sr));
    const float toneLp = coefFor (1.0 / (juce::MathConstants<double>::twoPi * 5000.0), sr);
    const float toneHp = coefFor (1.0 / (juce::MathConstants<double>::twoPi * 150.0), sr);
    const double echoD = juce::jlimit (16.0, (double) dSize - 4.0, 0.75 * spb);   // dotted 1/8
    bool filterLive = false, riserLive = false;
    double tapeRate = 0.0;
    float tailPeak = 0.0f;

    for (int i = 0; i < num; ++i)
    {
        float x[2];
        x[0] = io.getSample (0, start + i);
        x[1] = nCh > 1 ? io.getSample (1, start + i) : x[0];
        const auto w = (size_t) (written % histSize);
        hist[0][w] = x[0];
        hist[1][w] = x[1];
        inPow += (0.5f * (x[0] * x[0] + x[1] * x[1]) - inPow) * powCoef;
        float y[2] = { x[0], x[1] };

        // ---- stutter / roll: replay the slice from the history
        const float stutTarget = (on[stutter] || on[roll]) ? 1.0f : 0.0f;
        stutEnv += (stutTarget - stutEnv) * cFast;
        if (stutTarget == 0.0f && stutEnv < 1.0e-4f) stutEnv = 0.0f;
        if (stutEnv > 0.0f)
        {
            if (on[roll])
            {
                const int k = juce::jmin (3, (int) ((double) (written - pressedAt[roll]) / spb));
                const int len = juce::jmax (32, slice0 >> k);   // faster every beat you hold it
                if (len != sliceLen) { sliceLen = len; stutPos %= sliceLen; }
            }
            const int fade = juce::jmin (sliceLen / 4, (int) (0.0015 * sr) + 1);
            float g = 1.0f;
            if (stutPos < fade) g = (float) stutPos / (float) fade;
            else if (stutPos > sliceLen - fade) g = (float) (sliceLen - stutPos) / (float) fade;
            const juce::int64 idx = sliceStart + stutPos;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float s = idx >= 0 && idx <= written ? hist[ch][(size_t) (idx % histSize)] * g : 0.0f;
                y[ch] += (s - y[ch]) * stutEnv;
            }
            if (++stutPos >= sliceLen) stutPos = 0;
        }

        // ---- tape stop: a read head that slows to a halt
        const float tapeTarget = on[tapeStop] ? 1.0f : 0.0f;
        tapeEnv += (tapeTarget - tapeEnv) * (tapeTarget > tapeEnv ? cInst : cMid);
        if (tapeTarget == 0.0f && tapeEnv < 1.0e-4f) tapeEnv = 0.0f;
        if (tapeEnv > 0.0f)
        {
            if (on[tapeStop])
            {
                const double t = (double) (written - pressedAt[tapeStop]) / tapeDur;
                tapeRate = t < 1.0 ? std::pow (1.0 - t, 1.2) : 0.0;
            }
            const float coef = (float) juce::jlimit (0.02, 1.0, tapeRate * 1.2);
            const float g = (float) std::pow (tapeRate, 0.25) * (float) juce::jmin (1.0, tapeRate / 0.04);   // fades to nothing, no drop-off
            for (int ch = 0; ch < 2; ++ch)
            {
                tapeLp[ch] += (histAt (ch, tapeRead) - tapeLp[ch]) * coef;
                y[ch] += (tapeLp[ch] * g - y[ch]) * tapeEnv;
            }
            tapeRead = juce::jmin ((double) written, tapeRead + tapeRate);
        }

        // ---- filter sweep (low-pass down while held)
        filterProg = on[filter] ? juce::jmin (1.0f, filterProg + filterStep) : juce::jmax (0.0f, filterProg - filterBack);
        if (filterProg > 0.0f)
        {
            if (! filterLive || (i & 15) == 0)
            {
                const double depth = 0.4 + 0.6 * level[filter];
                const double hz = 20000.0 * std::pow (250.0 / 20000.0, std::pow ((double) filterProg, 0.8) * depth);
                for (auto& f : lp) f.set (sr, hz, 1.0);
                filterLive = true;
            }
            const float mix = juce::jmin (1.0f, filterProg * 20.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                float l, b, h;
                lp[ch].process (y[ch], l, b, h);
                y[ch] += (l - y[ch]) * mix;
            }
        }
        else if (filterLive || lp[0].ic1 != 0.0f)
        {
            for (auto& f : lp) f.clear();
            filterLive = false;
        }

        // ---- riser: high-pass up plus rising noise
        riserProg = on[riser] ? juce::jmin (1.0f, riserProg + riserStep) : juce::jmax (0.0f, riserProg - riserBack);
        if (riserProg > 0.0f)
        {
            const double amt = 0.5 + 0.5 * level[riser];
            if (! riserLive || (i & 15) == 0)
            {
                const double hpHz = 20.0 * std::pow (1500.0 / 20.0, std::pow ((double) riserProg, 1.2) * amt);
                const double bpHz = 300.0 * std::pow (9000.0 / 300.0, (double) riserProg);
                for (auto& f : hp) f.set (sr, hpHz, 1.4);
                for (auto& f : noiseBp) f.set (sr, bpHz, 0.7);
                riserLive = true;
            }
            const float mix = juce::jmin (1.0f, riserProg * 20.0f);
            const float nl = std::sqrt (inPow) * 2.8f * std::pow (riserProg, 1.5f) * (float) amt;
            for (int ch = 0; ch < 2; ++ch)
            {
                float l, b, h;
                hp[ch].process (y[ch], l, b, h);
                y[ch] += (h - y[ch]) * mix;
                float nl2, nb, nh;
                noiseBp[ch].process (noise.nextFloat() * 2.0f - 1.0f, nl2, nb, nh);
                y[ch] += nb * nl;
            }
        }
        else if (riserLive || hp[0].ic1 != 0.0f)
        {
            for (int ch = 0; ch < 2; ++ch) { hp[ch].clear(); noiseBp[ch].clear(); }
            riserLive = false;
        }

        // ---- echo out: the thrown 1/8 bounces around while the track drops out
        echoHold += ((on[echo] ? 1.0f : 0.0f) - echoHold) * cFast;
        if (! on[echo] && echoHold < 1.0e-4f) echoHold = 0.0f;
        float eo[2] = { 0.0f, 0.0f }, wo[2] = { 0.0f, 0.0f };
        // ---- wash: a reverb that swells while held
        washSend = on[wash] ? juce::jmin (1.0f, washSend + washStep) : juce::jmax (0.0f, washSend - washBack);
        washDuck += ((on[wash] ? 0.6f * washSend : 0.0f) - washDuck) * cSlow;
        if (! on[wash] && washDuck < 1.0e-4f) washDuck = 0.0f;

        if (tailsLive)
        {
            float send[2] = { 0.0f, 0.0f };
            if (echoSendLeft > 0)
            {
                const int fadeN = juce::jmax (1, juce::jmin (echoSendLen / 4, (int) (0.005 * sr)));
                const int done = echoSendLen - echoSendLeft;
                const float g = juce::jmin (1.0f, (float) echoSendLeft / fadeN, (float) (done + 1) / fadeN);
                send[0] = y[0] * g;
                send[1] = y[1] * g;
                --echoSendLeft;
            }
            double rp = (double) dW - echoD;
            if (rp < 0.0) rp += dSize;
            const int i0 = (int) rp;
            const float f = (float) (rp - i0);
            const int i1 = (i0 + 1) % dSize;
            const float oL = dl[(size_t) i0] + (dl[(size_t) i1] - dl[(size_t) i0]) * f;
            const float oR = dr[(size_t) i0] + (dr[(size_t) i1] - dr[(size_t) i0]) * f;
            // darker and thinner on every repeat
            echoTone[0] += (oL - echoTone[0]) * toneLp;  echoTone[1] += (echoTone[0] - echoTone[1]) * toneHp;
            echoTone[2] += (oR - echoTone[2]) * toneLp;  echoTone[3] += (echoTone[2] - echoTone[3]) * toneHp;
            const float fL = echoTone[0] - echoTone[1], fR = echoTone[2] - echoTone[3];
            dl[(size_t) dW] = 0.5f * (send[0] + send[1]) + fR * echoFb;
            dr[(size_t) dW] = fL * echoFb;
            dW = (dW + 1) % dSize;
            eo[0] = oL * echoLevel;
            eo[1] = oR * echoLevel;

            // reverb: 8 damped delay lines through a Hadamard matrix
            const float wl = 0.5f + 0.5f * level[wash];
            const float in[2] = { y[0] * washSend * wl, y[1] * washSend * wl };
            float o[8];
            for (int k = 0; k < 8; ++k)
            {
                const float v = fdn[k][(size_t) fdnPos[k]];
                fdnLp[k] += (v - fdnLp[k]) * 0.7f;
                o[k] = fdnLp[k] * fdnGain[k];
            }
            for (int h = 1; h < 8; h <<= 1)
                for (int a = 0; a < 8; a += h * 2)
                    for (int b = a; b < a + h; ++b)
                    {
                        const float p = o[b], q = o[b + h];
                        o[b] = p + q;
                        o[b + h] = p - q;
                    }
            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float v = o[k] * 0.35355339f;
                fdn[k][(size_t) fdnPos[k]] = v + in[k & 1] * 0.5f;
                fdnPos[k] = (fdnPos[k] + 1) % fdnLen[k];
                outL += v * ((k % 2) ? -1.0f : 1.0f);
                outR += v * (((k / 2) % 2) ? -1.0f : 1.0f);
            }
            wo[0] = outL * 0.45f;
            wo[1] = outR * 0.45f;
            tailPeak = juce::jmax (tailPeak, juce::jmax (std::abs (eo[0]), std::abs (eo[1])), juce::jmax (std::abs (wo[0]), std::abs (wo[1])));
        }

        // ---- cut
        cutEnv += ((on[cut] ? 1.0f : 0.0f) - cutEnv) * cFast;
        if (! on[cut] && cutEnv < 1.0e-4f) cutEnv = 0.0f;

        const float dry = (1.0f - echoHold) * (1.0f - cutEnv) * (1.0f - washDuck);
        const float out0 = y[0] * dry + eo[0] + wo[0];
        const float out1 = y[1] * dry + eo[1] + wo[1];
        if (nCh > 1)
        {
            io.setSample (0, start + i, out0);
            io.setSample (1, start + i, out1);
        }
        else
            io.setSample (0, start + i, 0.5f * (out0 + out1));
        ++written;
    }

    // tails that have died away: clear them so the plug-in goes back to a clean pass-through
    if (tailsLive)
    {
        if (tailPeak > 1.0e-6f || echoSendLeft > 0 || washSend > 0.0f || on[echo] || on[wash]) quietSamples = 0;
        else quietSamples += num;
        if (quietSamples > (int) sr)
        {
            std::fill (dl.begin(), dl.end(), 0.0f);
            std::fill (dr.begin(), dr.end(), 0.0f);
            for (int k = 0; k < 8; ++k) { std::fill (fdn[k].begin(), fdn[k].end(), 0.0f); fdnLp[k] = 0.0f; }
            for (auto& t : echoTone) t = 0.0f;
            tailsLive = false;
        }
    }

    const float tail = juce::jmin (1.0f, tailPeak * 8.0f);
    const float act[numPads] = { on[stutter] ? 1.0f : 0.0f, on[roll] ? 1.0f : 0.0f, filterProg, riserProg, tapeEnv,
                                 on[echo] ? 1.0f : juce::jmin (0.6f, tail), on[wash] ? juce::jmax (0.4f, washSend) : juce::jmin (0.6f, tail), cutEnv };
    for (int p = 0; p < numPads; ++p)
        activity[(size_t) p].store (on[p] ? juce::jmax (0.35f, act[p]) : act[p], std::memory_order_relaxed);
}

} // namespace km
