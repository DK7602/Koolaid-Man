#pragma once

#include "Model.h"
#include <functional>

namespace km
{

struct Preset
{
    juce::String name;
    std::vector<Move> moves;
    bool factory = true;
    juce::File file;   // user presets only
    juce::String genre = "Classic";
};

/** Genres, in browser order (factory presets and ROLL styles). */
const juce::StringArray& presetGenres();
/** ROLL styles: "Any" followed by the genres (minus "Classic"). */
const juce::StringArray& rollStyleNames();

std::vector<Preset> factoryPresets();

juce::File userPresetFolder();
juce::File exportFolder();
std::vector<Preset> loadUserPresets();
bool saveUserPreset (const juce::String& name, const std::vector<Move>& moves);

juce::ValueTree movesToTree (const std::vector<Move>&);
std::vector<Move> movesFromTree (const juce::ValueTree&);

/** ROLL: a fresh, musically sensible stack. */
std::vector<Move> rollMoves (juce::Random& rng, bool haveDrums, int style = 0);

} // namespace km
