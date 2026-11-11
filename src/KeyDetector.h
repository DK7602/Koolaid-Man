#pragma once

#include "Model.h"

namespace km
{

/** Estimates the key of a piece of music: a pitch-class profile ("chroma") is built
    from spectral peaks between 55 Hz and 2 kHz, then correlated with the
    Krumhansl-Kessler major and minor key profiles in all 12 transpositions. */
KeyInfo detectKey (const juce::AudioBuffer<float>& audio, double sampleRate);

} // namespace km
