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
    juce::int64 dropInClip = -1;                      // where the drop lands in the source clip (-1 = unknown)
    const juce::AudioBuffer<float>* clip = nullptr;   // the untouched music clip
    const DrumAnalysis* drums = nullptr;              // onsets relative to clip start
    const ClipData* userSample = nullptr;             // the move's own sample (Riser / Crash / Impact / Boom), if any

    // the song's key root for tuning (0 = unknown / tuning off): pitch class 0..11
    int tonic = -1;
    double rootHz (double lowHz, double fallbackHz) const { return tonic >= 0 ? pitchClassHz (tonic, lowHz) : fallbackHz; }

    // levels of the original build zone, so added sounds sit at the track's level
    float refPeak = 0.5f;     // sample peak
    float refRms = 0.1f;      // RMS
    float lowRms = 0.05f;     // RMS below ~120 Hz

    // Timeline: when >= 0 the move fills exactly this many beats (its slice) instead of
    // working out its own length from its settings.
    double lengthOverride = -1.0;

    double bpm() const { return 60.0 * sampleRate / juce::jmax (1.0, samplesPerBeat); }
};

/** Where a move acts, in beats relative to the drop: its custom timeline region, or its
    natural spot (e.g. a Tape Stop over the last 1/2 bar). zoneBeats = build zone length. */
std::pair<double, double> moveRegion (const Move&, double zoneBeats, double beatsPerBar, double bpm);

/** Applies one move to the zone. 'main' is the processed music, 'fx' is the
    generated layer (drum fills, 808s, hats, reverb). Both are the zone's length. */
void applyMove (const Move&, const MoveContext&, juce::AudioBuffer<float>& main, juce::AudioBuffer<float>& fx);

/** Impact / Sub Boom: renders the move into 'post', a buffer that starts on the drop's downbeat. */
void renderAfterDrop (const Move&, const MoveContext&, juce::AudioBuffer<float>& post);

/** How far past the drop an after-drop move rings (in beats). */
double afterDropTailBeats (const Move&, double beatsPerBar, double bpm);

double stutterSpanBeats (float knob, double beatsPerBar);
double filterCutoff (float knob);

} // namespace km
