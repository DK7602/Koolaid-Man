#include "DropRenderer.h"
#include "DrumDetector.h"
#include "MoveProcessors.h"
#include "Mastering.h"

namespace km
{


ResultPtr renderDrop (const DropModel& model, AnalysisPtr analysis, int version)
{
    auto res = std::make_shared<RenderResult>();
    res->version = version;
    res->analysis = analysis;
    if (model.music == nullptr || model.music->audio.getNumSamples() == 0)
        return res;

    const auto& clip = model.music->audio;
    const double sr = model.music->sampleRate;
    const double spb = model.samplesPerBeat (sr);
    const int clipLen = clip.getNumSamples();

    res->source = model.music;
    res->sampleRate = sr;
    res->samplesPerBeat = spb;
    res->zoneStart = juce::jlimit<juce::int64> (0, clipLen, (juce::int64) std::llround (model.zoneStart * spb));
    res->zoneEnd = juce::jlimit<juce::int64> (res->zoneStart, clipLen, (juce::int64) std::llround (model.zoneEnd * spb));

    // Impact / Sub Boom ring past the drop. If the audio ends there, add a tail
    // (rounded up to whole bars so exports still land on the grid).
    int tail = 0;
    for (auto& m : model.moves)
        if (m.enabled && playsAfterDrop (m.type))
        {
            const double endBeats = m.customRegion ? juce::jmax (0.25, m.regionEnd) : afterDropTailBeats (m, model.beatsPerBar());
            tail = juce::jmax (tail, (int) std::llround (endBeats * spb));
        }
    int total = clipLen;
    if (res->zoneEnd + tail > clipLen)
    {
        const double barSamples = model.beatsPerBar() * spb;
        total = (int) std::llround (std::ceil ((res->zoneEnd + tail) / barSamples - 1.0e-6) * barSamples);
    }
    res->postLength = tail;

    res->mix.setSize (2, total);
    res->fx.setSize (2, total);
    res->mix.clear();
    res->fx.clear();
    for (int ch = 0; ch < 2; ++ch)
        res->mix.copyFrom (ch, 0, clip, juce::jmin (ch, clip.getNumChannels() - 1), 0, clipLen);

    const int n = (int) (res->zoneEnd - res->zoneStart);
    if (n < 64)
        return res;

    juce::AudioBuffer<float> main (2, n), fx (2, n);
    fx.clear();
    for (int ch = 0; ch < 2; ++ch)
        main.copyFrom (ch, 0, res->mix, ch, (int) res->zoneStart, n);

    MoveContext ctx;
    ctx.sampleRate = sr;
    ctx.samplesPerBeat = spb;
    ctx.beatsPerBar = model.beatsPerBar();
    ctx.zoneStartInClip = res->zoneStart;
    ctx.clip = &clip;
    ctx.drums = analysis.get();

    // reference levels of the untouched zone
    {
        double ss = 0.0, ls = 0.0;
        float pk = 0.0f;
        juce::IIRFilter lp1, lp2;
        lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 120.0));
        lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 120.0));
        for (int i = 0; i < n; ++i)
        {
            const float l = main.getSample (0, i), r = main.getSample (1, i);
            ss += 0.5 * ((double) l * l + (double) r * r);
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
            const float lo = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (0.5f * (l + r)));
            ls += (double) lo * lo;
        }
        ctx.refRms = (float) juce::jmax (1.0e-4, std::sqrt (ss / n));
        ctx.refPeak = juce::jlimit (0.05f, 1.0f, pk);
        ctx.lowRms = (float) juce::jmax (1.0e-4, std::sqrt (ls / n));
    }
    const juce::AudioBuffer<float> original (main);

    for (auto& m : model.moves)
    {
        if (! m.enabled || playsAfterDrop (m.type))
            continue;
        if (! m.customRegion)
        {
            applyMove (m, ctx, main, fx);
            continue;
        }
        // Timeline: run the move on its own slice of the build, then crossfade it in and out
        const double zoneBeats = n / spb;
        const int s0 = (int) std::llround (juce::jlimit (0.0, zoneBeats, zoneBeats + m.regionStart) * spb);
        const int s1 = (int) std::llround (juce::jlimit (0.0, zoneBeats, zoneBeats + m.regionEnd) * spb);
        const int len = s1 - s0;
        if (len < 64)
            continue;
        juce::AudioBuffer<float> sm (2, len), sf (2, len);
        for (int ch = 0; ch < 2; ++ch)
        {
            sm.copyFrom (ch, 0, main, ch, s0, len);
            sf.copyFrom (ch, 0, fx, ch, s0, len);
        }
        const juce::AudioBuffer<float> beforeMain (sm), beforeFx (sf);
        MoveContext sc = ctx;
        sc.zoneStartInClip = ctx.zoneStartInClip + s0;
        sc.lengthOverride = len / spb;
        applyMove (m, sc, sm, sf);
        const int xf = juce::jmin (len / 4, (int) (0.005 * sr) + 1);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < xf; ++i)
            {
                const float a = (float) i / xf;
                if (s0 > 0)
                {
                    sm.setSample (ch, i, beforeMain.getSample (ch, i) * (1.0f - a) + sm.getSample (ch, i) * a);
                    sf.setSample (ch, i, beforeFx.getSample (ch, i) * (1.0f - a) + sf.getSample (ch, i) * a);
                }
                if (s1 < n)
                {
                    const int k = len - 1 - i;
                    sm.setSample (ch, k, beforeMain.getSample (ch, k) * (1.0f - a) + sm.getSample (ch, k) * a);
                    sf.setSample (ch, k, beforeFx.getSample (ch, k) * (1.0f - a) + sf.getSample (ch, k) * a);
                }
            }
            main.copyFrom (ch, s0, sm, ch, 0, len);
            fx.copyFrom (ch, s0, sf, ch, 0, len);
        }
    }

    // Loudness guard: a build should rise, not jump. Each beat may be at most ~3.5 dB above
    // the original audio (or the zone's average, whichever is louder); smooth gain between beats.
    {
        const int beat = juce::jmax (256, (int) spb);
        const int numBeats = (n + beat - 1) / beat;
        auto rmsOf = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>* b, int s0, int len)
        {
            double acc = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = s0; i < s0 + len; ++i)
                {
                    const float v = a.getSample (ch, i) + (b != nullptr ? b->getSample (ch, i) : 0.0f);
                    acc += (double) v * v;
                }
            return std::sqrt (acc / juce::jmax (1, 2 * len));
        };
        const double allowance = std::pow (10.0, 3.5 / 20.0);
        std::vector<float> gains ((size_t) numBeats, 1.0f);
        for (int b = 0; b < numBeats; ++b)
        {
            const int s0 = b * beat, len = juce::jmin (beat, n - s0);
            const double allowed = juce::jmax (rmsOf (original, nullptr, s0, len), (double) ctx.refRms) * allowance;
            const double out = rmsOf (main, &fx, s0, len);
            gains[(size_t) b] = out > allowed ? (float) (allowed / out) : 1.0f;
        }
        bool any = false;
        for (auto gval : gains) any |= gval < 0.999f;
        if (any)
        {
            for (int i = 0; i < n; ++i)
            {
                // interpolate between beat centres
                const float pos = ((float) i - beat * 0.5f) / (float) beat;
                const int b0 = juce::jlimit (0, numBeats - 1, (int) std::floor (pos));
                const int b1 = juce::jmin (numBeats - 1, b0 + 1);
                const float f = juce::jlimit (0.0f, 1.0f, pos - (float) b0);
                const float gval = gains[(size_t) b0] * (1.0f - f) + gains[(size_t) b1] * f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    main.setSample (ch, i, main.getSample (ch, i) * gval);
                    fx.setSample (ch, i, fx.getSample (ch, i) * gval);
                }
            }
        }
    }

    // write the zone back: processed music + fx layer
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = res->mix.getWritePointer (ch, (int) res->zoneStart);
        auto* m = main.getReadPointer (ch);
        auto* f = fx.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            out[i] = m[i] + f[i];
        res->fx.copyFrom (ch, (int) res->zoneStart, fx, ch, 0, n);
    }

    // True-peak limiting: -1 dBTP, or the track's own true peak if it is already mastered
    // hotter (never above -0.1 dBTP), so the build matches the audio around it.
    {
        const float origTp = measureTruePeak (original, 0, n, sr);
        const float ceiling = juce::jlimit (juce::Decibels::decibelsToGain (-1.0f), juce::Decibels::decibelsToGain (-0.1f), origTp);
        res->limiterReductionDb = truePeakLimit (res->mix, (int) res->zoneStart, n, sr, ceiling);
        truePeakLimit (res->fx, (int) res->zoneStart, n, sr, ceiling);
        res->ceilingDb = juce::Decibels::gainToDecibels (ceiling);
    }

    // micro fades either side of the drop so the hit lands without a click
    const int dropFade = juce::jmin (n, 64);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = res->mix.getWritePointer (ch, (int) res->zoneStart);
        for (int i = 0; i < dropFade; ++i)
            out[n - dropFade + i] *= (float) (dropFade - i) / dropFade;
        if (res->zoneEnd + 24 < total)
        {
            auto* post = res->mix.getWritePointer (ch, (int) res->zoneEnd);
            for (int i = 0; i < 24; ++i)
                post[i] *= (float) i / 24.0f;
        }
    }

    // after-drop moves (Impact, Sub Boom) land on the downbeat
    if (tail > 0)
    {
        const int postLen = juce::jmin (tail, total - (int) res->zoneEnd);
        juce::AudioBuffer<float> post (2, postLen);
        post.clear();
        for (auto& m : model.moves)
        {
            if (! m.enabled || ! playsAfterDrop (m.type)) continue;
            if (! m.customRegion)
            {
                renderAfterDrop (m, ctx, post);
                continue;
            }
            // timed hit: starts regionStart beats after the drop and rings for the region's length
            const int off = (int) std::llround (juce::jmax (0.0, m.regionStart) * spb);
            const int len = juce::jmin (postLen - off, (int) std::llround ((m.regionEnd - juce::jmax (0.0, m.regionStart)) * spb));
            if (off >= postLen || len < 256) continue;
            juce::AudioBuffer<float> part (2, len);
            part.clear();
            MoveContext sc = ctx;
            sc.lengthOverride = len / spb;
            renderAfterDrop (m, sc, part);
            for (int ch = 0; ch < 2; ++ch)
                post.addFrom (ch, off, part, ch, 0, len);
        }
        // what was already there after the drop (if anything) sets the ceiling, else -1 dBTP
        const int origPost = juce::jmax (0, juce::jmin (postLen, clipLen - (int) res->zoneEnd));
        const float origTp = origPost > 0 ? measureTruePeak (res->mix, (int) res->zoneEnd, origPost, sr) : 0.0f;
        const float ceiling = juce::jlimit (juce::Decibels::decibelsToGain (-1.0f), juce::Decibels::decibelsToGain (-0.1f), origTp);
        for (int ch = 0; ch < 2; ++ch)
        {
            res->mix.addFrom (ch, (int) res->zoneEnd, post, ch, 0, postLen);
            res->fx.addFrom (ch, (int) res->zoneEnd, post, ch, 0, postLen);
        }
        truePeakLimit (res->mix, (int) res->zoneEnd, postLen, sr, ceiling);
        truePeakLimit (res->fx, (int) res->zoneEnd, postLen, sr, ceiling);
    }
    return res;
}

//==============================================================================
RenderThread::RenderThread() : juce::Thread ("Koolaid render")
{
    startThread (juce::Thread::Priority::normal);
}

RenderThread::~RenderThread()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void RenderThread::request (const DropModel& model, int requestId)
{
    {
        const juce::ScopedLock sl (lock);
        pending = std::make_unique<DropModel> (model);
        pendingId = requestId;
    }
    wake.signal();
}

AnalysisPtr RenderThread::getLatestAnalysis() const
{
    const juce::ScopedLock sl (lock);
    return cachedAnalysis;
}

void RenderThread::run()
{
    while (! threadShouldExit())
    {
        wake.wait (500);
        std::unique_ptr<DropModel> job;
        int jobId = 0;
        {
            const juce::ScopedLock sl (lock);
            job = std::move (pending);
            jobId = pendingId;
        }
        if (job == nullptr || threadShouldExit())
            continue;

        AnalysisPtr analysis;
        {
            AnalysisKey key;
            key.clip = job->music.get();
            key.drums = job->drums.get();
            key.s1 = job->sensSnare; key.s2 = job->sens808; key.s3 = job->sensHats;
            key.bpm = job->bpm; key.bpb = job->beatsPerBar(); key.req = job->analysisRequest;

            bool needs;
            {
                const juce::ScopedLock sl (lock);
                needs = ! (key == cachedKey);
                analysis = cachedAnalysis;
            }
            if (needs)
            {
                std::shared_ptr<DrumAnalysis> a;
                const ClipPtr src = job->drums != nullptr ? job->drums : job->music;
                if (src != nullptr)
                {
                    a = std::make_shared<DrumAnalysis> (analyseDrums (src->audio, src->sampleRate, job->samplesPerBeat (src->sampleRate),
                                                                      job->beatsPerBar(), job->sensSnare, job->sens808, job->sensHats));
                    a->fromDrumStem = job->drums != nullptr;
                }
                analysis = a;
                const juce::ScopedLock sl (lock);
                cachedKey = key;
                cachedAnalysis = analysis;
                keepClip = job->music;
                keepDrums = job->drums;
            }
        }

        // if a newer request arrived meanwhile, skip straight to it
        {
            const juce::ScopedLock sl (lock);
            if (pending != nullptr)
                continue;
        }

        auto rendered = renderDrop (*job, analysis, ++version);
        std::const_pointer_cast<RenderResult> (rendered)->requestId = jobId;
        auto result = std::move (rendered);
        if (onResult && ! threadShouldExit())
            onResult (result);
    }
}

} // namespace km
