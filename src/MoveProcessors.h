#pragma once

#include "Model.h"

namespace km
{

struct MoveContext
{
    double sampleRate = 48000.0;
    double samplesPerBeat = 22153.8;
    double beatsPerBar = 4.0;
    juce::int64 zoneStartInClip = 0;                  // where the zone starts in the source clip
    const juce::AudioBuffer<float>* clip = nullptr;   // the untouched music clip
    const DrumAnalysis* drums = nullptr;              // onsets relative to clip start
};

/** Applies one move to the zone. 'main' is the processed music, 'fx' is the
    generated layer (drum fills, 808s, hats, reverb). Both are the zone's length. */
void applyMove (const Move&, const MoveContext&, juce::AudioBuffer<float>& main, juce::AudioBuffer<float>& fx);

double stutterSpanBeats (float knob, double beatsPerBar);
double filterCutoff (float knob);

} // namespace km
