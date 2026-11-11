#pragma once

#include "Model.h"
#include <atomic>
#include <functional>

namespace km
{

/** One finished render. Immutable once published.

    Only the part that can change is stored: mix / fx / music hold the samples from
    regionStart (the start of the build zone) to the end of the output. Before
    regionStart the output is simply the untouched clip, read from 'source'. This keeps
    renders fast and small even when a whole song is loaded. */
struct RenderResult
{
    enum class Layer { mix, fx, music };

    ClipPtr source;                     // the untouched clip (for A/B and display)
    juce::AudioBuffer<float> mix;       // [regionStart, length): processed music + fx layer
    juce::AudioBuffer<float> fx;        // [regionStart, length): fx layer only
    juce::AudioBuffer<float> music;     // [regionStart, length): processed music only (stems)
    juce::int64 regionStart = 0;
    juce::int64 length = 0;             // full output length (the clip plus any after-drop tail)

    float sampleAt (Layer layer, int ch, juce::int64 p) const noexcept
    {
        if (p < 0 || p >= length) return 0.0f;
        if (p < regionStart)
        {
            if (layer == Layer::fx || source == nullptr || p >= source->audio.getNumSamples()) return 0.0f;
            return source->audio.getSample (juce::jmin (ch, source->audio.getNumChannels() - 1), (int) p);
        }
        const auto& b = layer == Layer::mix ? mix : (layer == Layer::fx ? fx : music);
        const auto i = p - regionStart;
        return i < b.getNumSamples() ? b.getSample (juce::jmin (ch, b.getNumChannels() - 1), (int) i) : 0.0f;
    }
    float mixAt (int ch, juce::int64 p) const noexcept { return sampleAt (Layer::mix, ch, p); }

    /** The same render without the moves linked to Tension (null when nothing is linked).
        The Tension knob blends live between it (0) and this render (1). */
    std::shared_ptr<const RenderResult> base;
    float tensionAt (Layer layer, int ch, juce::int64 p, float tension) const noexcept
    {
        const float v = sampleAt (layer, ch, p);
        if (base == nullptr || tension >= 0.9999f) return v;
        const float b = base->sampleAt (layer, ch, p);
        return b + (v - b) * tension;
    }

    /** A stereo copy of [a, b) of one layer (allocates; not for the audio thread). */
    juce::AudioBuffer<float> copyOf (Layer layer, juce::int64 a, juce::int64 b, float tension = 1.0f) const
    {
        a = juce::jlimit<juce::int64> (0, length, a);
        b = juce::jlimit<juce::int64> (a, length, b);
        juce::AudioBuffer<float> out (2, (int) (b - a));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            for (juce::int64 p = a; p < b; ++p) d[p - a] = tensionAt (layer, ch, p, tension);
        }
        return out;
    }
    AnalysisPtr analysis;
    double sampleRate = 48000.0;
    double samplesPerBeat = 22153.8;
    juce::int64 zoneStart = 0, zoneEnd = 0;
    float limiterReductionDb = 0.0f;    // how hard the true-peak limiter worked (<= 0)
    float ceilingDb = -1.0f;
    int postLength = 0;                 // samples rendered after the drop (Impact / Sub Boom)
    int version = 0;
    int requestId = 0;                  // which request this render answers
};
using ResultPtr = std::shared_ptr<const RenderResult>;

/** Renders a DropModel into a RenderResult. Pure function (thread-safe). */
ResultPtr renderDrop (const DropModel& model, AnalysisPtr analysis, int version);

/** Runs analysis + rendering on a background thread. The newest request wins. */
class RenderThread : private juce::Thread
{
public:
    RenderThread();
    ~RenderThread() override;

    void request (const DropModel& model, int requestId = 0);

    /** Called on the render thread when a result is ready. */
    std::function<void (ResultPtr)> onResult;

    AnalysisPtr getLatestAnalysis() const;

private:
    void run() override;

    juce::CriticalSection lock;
    std::unique_ptr<DropModel> pending;
    int pendingId = 0;
    juce::WaitableEvent wake;
    int version = 0;

    // analysis cache
    struct AnalysisKey
    {
        const void* clip = nullptr; const void* drums = nullptr;
        float s1 = -1, s2 = -1, s3 = -1; double bpm = 0; double bpb = 0; int req = -1;
        bool operator== (const AnalysisKey& o) const
        {
            return clip == o.clip && drums == o.drums && s1 == o.s1 && s2 == o.s2 && s3 == o.s3 && bpm == o.bpm && bpb == o.bpb && req == o.req;
        }
    } cachedKey;
    AnalysisPtr cachedAnalysis;
    ClipPtr keepClip, keepDrums;   // keep the keyed clips alive so pointers stay unique
};

} // namespace km
