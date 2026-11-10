#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <memory>
#include <vector>

namespace km
{

//==============================================================================
/** The ten move types. One effect card exists per type; the stack can hold
    any number of instances in any order. */
enum class MoveType : int
{
    Reverse = 0,
    Chops,
    Stutter,
    Pitch,
    TapeStop,
    Filter,
    DrumMoves,
    Bass808,
    Hats,
    Atmos,
    NumTypes
};

constexpr int numMoveTypes = (int) MoveType::NumTypes;

//==============================================================================
/** Note divisions used by the card dropdowns. Values are in quarter-note beats
    for the fractions; the "bar" entries are resolved against the time signature. */
struct Division
{
    const char* label;      // shown on the card dropdown ("1/8")
    const char* longLabel;  // shown in the stack ("1/8")
    double wholeNotes;      // fraction of a whole note, or -n for n bars
};

const std::vector<Division>& divisions();
double divisionToBeats (int index, double beatsPerBar);

//==============================================================================
struct Move
{
    MoveType type = MoveType::Reverse;
    bool enabled = true;
    int division = 5;       // index into divisions()
    int semitones = -12;    // Pitch only
    float knob = 0.75f;     // main card knob (meaning depends on type)
    float knob2 = 0.5f;     // second knob (Filter resonance, Atmos mix)
    int mode = 0;           // variant (see modeNames)
    juce::uint32 seed = 1;
    int uid = 0;

    juce::String stackLabel() const;
    juce::ValueTree toTree() const;
    static Move fromTree (const juce::ValueTree&);

    bool operator== (const Move& o) const;
};

const char* typeName (MoveType);              // "Reverse"
const char* cardTitle (MoveType);             // "REVERSE"
juce::StringArray modeNames (MoveType);       // variant names for the stack menu
juce::String knobName (MoveType, int which);  // "Mix", "Depth"...
juce::String knobValueText (const Move&, int which);
Move defaultMove (MoveType);
int canonicalOrder (MoveType);                // used when Roll / power adds moves

const juce::StringArray& atmosTypeNames();    // the seven reverbs

//==============================================================================
struct ClipData
{
    juce::AudioBuffer<float> audio;   // at engineRate
    double sampleRate = 48000.0;
    juce::String name;
};
using ClipPtr = std::shared_ptr<const ClipData>;

//==============================================================================
struct DrumHits
{
    bool found = false;
    float confidence = 0.0f;               // 0..1, drives the meters
    std::vector<juce::int64> onsets;       // sample positions in the analysed clip
    juce::AudioBuffer<float> oneShot;      // extracted hit
    float pitchHz = 0.0f;                  // 808 fundamental (808 only)
};

struct DrumAnalysis
{
    DrumHits snare, bass, hats;
    bool fromDrumStem = false;
};
using AnalysisPtr = std::shared_ptr<const DrumAnalysis>;

//==============================================================================
enum class PlaybackMode : int { PreDrop = 0, FullClip, IntoDrop, Original };
enum class ExportRange : int { FullClip = 0, BuildZone, ZoneToEnd, FxLayer };
enum class ExportFormat : int { Wav16 = 0, Wav24, Wav32f, Aiff24 };

juce::StringArray playbackModeNames();
juce::StringArray exportFormatNames();

/** Everything the renderer needs. Copied by value onto the render thread
    (audio is shared through shared_ptr). */
struct DropModel
{
    ClipPtr music;       // AUDIO 1
    ClipPtr drums;       // AUDIO 2 (optional drum/808 stem)

    double bpm = 130.0;
    bool tempoSync = true;
    int tsNum = 4, tsDen = 4;
    int clipStartBar = 1;            // session bar where the clip starts

    double zoneStart = 16.0;         // quarter-note beats from clip start
    double zoneEnd = 32.0;           // the drop

    std::vector<Move> moves;
    std::array<Move, numMoveTypes> templates;   // card values when no instance exists

    float sens808 = 0.55f, sensHats = 0.55f, sensSnare = 0.55f;
    int analysisRequest = 0;         // bump to force re-analysis

    double beatsPerBar() const noexcept { return tsNum * 4.0 / tsDen; }
    double samplesPerBeat (double sr) const noexcept { return sr * 60.0 / bpm; }
    double clipLengthBeats() const;  // music clip length in beats
};

juce::String formatBarPosition (double beatsFromClipStart, const DropModel&, bool withSixteenths = true);
juce::String formatLength (double beats, double beatsPerBar);

} // namespace km
