#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace km
{

/** Highest inter-sample (true) peak of a section, measured 4x oversampled (linear gain). */
float measureTruePeak (const juce::AudioBuffer<float>&, int start, int numSamples, double sampleRate);

/** Transparent lookahead true-peak limiter over [start, start + numSamples).
    Peaks are detected 4x oversampled so inter-sample overs are caught; gain reduction
    starts 1.5 ms ahead of each peak and recovers smoothly. Returns the max reduction in dB. */
float truePeakLimit (juce::AudioBuffer<float>&, int start, int numSamples, double sampleRate, float ceilingGain);

/** Quantises to 16-bit with TPDF dither and first-order noise shaping (in place). */
void ditherTo16Bit (juce::AudioBuffer<float>&, juce::uint32 seed = 0x4b4d);

} // namespace km
