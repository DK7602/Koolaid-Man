#pragma once

#include "Model.h"
#include <atomic>
#include <functional>

namespace km
{

/** One finished render. Immutable once published. */
struct RenderResult
{
    ClipPtr source;                     // the untouched clip (for A/B and display)
    juce::AudioBuffer<float> mix;       // full clip length with the processed zone
    juce::AudioBuffer<float> fx;        // fx layer only (full clip length)
    juce::AudioBuffer<float> music;     // processed music without the fx layer (for stems)
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
