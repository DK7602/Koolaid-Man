#include "DropRenderer.h"
#include "DrumDetector.h"
#include "MoveProcessors.h"

namespace km
{

namespace
{
    inline float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.85f) return x;
        const float y = 0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f);
        return x < 0 ? -y : y;
    }
}

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
    const int total = clip.getNumSamples();

    res->source = model.music;
    res->sampleRate = sr;
    res->samplesPerBeat = spb;
    res->zoneStart = juce::jlimit<juce::int64> (0, total, (juce::int64) std::llround (model.zoneStart * spb));
    res->zoneEnd = juce::jlimit<juce::int64> (res->zoneStart, total, (juce::int64) std::llround (model.zoneEnd * spb));

    res->mix.setSize (2, total);
    res->fx.setSize (2, total);
    res->fx.clear();
    for (int ch = 0; ch < 2; ++ch)
        res->mix.copyFrom (ch, 0, clip, juce::jmin (ch, clip.getNumChannels() - 1), 0, total);

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

    for (auto& m : model.moves)
        applyMove (m, ctx, main, fx);

    // write the zone back: processed music + fx layer, with a soft safety limiter
    const int dropFade = juce::jmin (n, 64);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = res->mix.getWritePointer (ch, (int) res->zoneStart);
        auto* m = main.getReadPointer (ch);
        auto* f = fx.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            out[i] = softLimit (m[i] + f[i]);
        res->fx.copyFrom (ch, (int) res->zoneStart, fx, ch, 0, n);

        // micro fades either side of the drop so the hit lands without a click
        for (int i = 0; i < dropFade; ++i)
            out[n - dropFade + i] *= (float) (dropFade - i) / dropFade;
        if (res->zoneEnd + 24 < total)
        {
            auto* post = res->mix.getWritePointer (ch, (int) res->zoneEnd);
            for (int i = 0; i < 24; ++i)
                post[i] *= (float) i / 24.0f;
        }
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

void RenderThread::request (const DropModel& model)
{
    {
        const juce::ScopedLock sl (lock);
        pending = std::make_unique<DropModel> (model);
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
        {
            const juce::ScopedLock sl (lock);
            job = std::move (pending);
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

        auto result = renderDrop (*job, analysis, ++version);
        if (onResult && ! threadShouldExit())
            onResult (result);
    }
}

} // namespace km
