#pragma once

#include "Model.h"

namespace km
{

/** Finds the snare, 808/kick and hi-hats in a piece of audio (a full two-track
    mix or a drum stem). Works on band-split onset detection, then scores the
    candidates (snares prefer backbeats, 808s need low-end sustain, hats need
    short bright decays) and extracts the best hit of each as a one-shot. */
DrumAnalysis analyseDrums (const juce::AudioBuffer<float>& audio, double sampleRate,
                           double samplesPerBeat, double beatsPerBar,
                           float sensSnare, float sens808, float sensHats);

/** Fallback one-shots used when a drum was not found. */
juce::AudioBuffer<float> synthSnare (double sampleRate);
juce::AudioBuffer<float> synthHat (double sampleRate, bool open);

} // namespace km
