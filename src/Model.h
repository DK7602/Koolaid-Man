#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace km
{

//==============================================================================
/** The move types. One effect card exists per type; the stack can hold
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
    // page 2: classic build & drop tools
    Riser,      // noise riser / uplifter into the drop
    RevCrash,   // reversed crash swelling into the drop
    Pump,       // sidechain pump
    Gap,        // the "breath" right before the drop
    Impact,     // hit + crash on the drop (plays after the drop)
    Boom,       // sub boom on the drop (plays after the drop)
    // page 3: fx moves
    DelayThrow, // tempo-synced echo throw / ping-pong build
    Bitcrush,   // bit depth / sample rate crush ramp
    Phaser,     // barber-pole rise, phaser sweep, flanger
    Width,      // stereo collapse to mono / widen
    Scratch,    // vinyl scratch, rewind, brake
    PreVerb,    // the drop's own downbeat, reverb-swelled in reverse into the drop
    NumTypes
};

constexpr int numMoveTypes = (int) MoveType::NumTypes;

/** Moves that play after the drop (on the downbeat) rather than inside the build zone. */
inline bool playsAfterDrop (MoveType t) { return t == MoveType::Impact || t == MoveType::Boom; }

/** Moves that can play your own sample. */
inline bool moveTakesSample (MoveType t)
{
    return t == MoveType::Riser || t == MoveType::RevCrash || t == MoveType::Impact || t == MoveType::Boom;
}

/** Moves that can follow a 16-step pattern. */
inline bool moveHasPattern (MoveType t) { return t == MoveType::Chops || t == MoveType::Stutter; }
constexpr int patternSteps = 16;

/** Which page of effect cards a move lives on: 0 main moves, 1 build & drop, 2 fx moves. */
inline int cardPage (MoveType t) { return (int) t >= (int) MoveType::DelayThrow ? 2 : ((int) t >= (int) MoveType::Riser ? 1 : 0); }

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

    // Timeline: when customRegion is set, the move only works inside [regionStart, regionEnd),
    // in beats relative to the drop (so negative = before the drop). Otherwise it uses its
    // natural spot (see moveRegion()).
    bool customRegion = false;
    double regionStart = -4.0, regionEnd = 0.0;

    // Linked to the Tension macro: fades in along the tension curve and follows the Tension knob
    bool tensionLink = false;
    bool locked = false;          // ROLL keeps locked moves

    // Chops / Stutter step pattern: 16 steps of the move's division, on the song's grid,
    // repeating (bit k = step k + 1). Off = the move's built-in rhythm.
    bool patternOn = false;
    juce::uint32 pattern = 0xffffu;

    // Riser / Reverse Crash / Impact / Sub Boom: your own sample instead of the built-in sound.
    // The audio lives in DropModel::samples (and in the project); presets keep the file path.
    juce::String sampleId, sampleName, samplePath;
    bool hasSample() const { return sampleId.isNotEmpty(); }

    // How the move ramps over its time (see curveNames()); 0 = the move's own shape.
    int curve = 0;
    std::array<float, 16> curvePoints { 0.0f, 0.0667f, 0.1333f, 0.2f, 0.2667f, 0.3333f, 0.4f, 0.4667f,
                                        0.5333f, 0.6f, 0.6667f, 0.7333f, 0.8f, 0.8667f, 0.9333f, 1.0f };   // Custom

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

enum CurveShape { curveAuto = 0, curveLinear, curveEaseIn, curveEaseOut, curveS, curveLate, curveSteps, curveCustom, numCurveShapes };
const juce::StringArray& curveNames();
/** Does this move ramp over time (so its curve matters)? */
bool moveHasRamp (const Move&);
/** The ramp value (0..1) at position t (0..1); 'autoPow' is the move's own shape (t^autoPow). */
double shapeCurve (const Move&, double t, double autoPow);

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

/** The song's key (from detectKey). tonic: 0 = C ... 11 = B. */
struct KeyInfo
{
    bool found = false;
    int tonic = 0;
    bool minor = false;
    float confidence = 0.0f;
};

juce::String keyName (int tonic, bool minor);          // "F# minor"
juce::String keyShortName (int tonic, bool minor);     // "F#m"
/** Frequency of pitch class 'tonic' in the octave [lowHz, 2 * lowHz). */
double pitchClassHz (int tonic, double lowHz);

struct DrumAnalysis
{
    DrumHits snare, bass, hats;
    bool fromDrumStem = false;
    KeyInfo key;                 // always from the music (AUDIO 1)
};
using AnalysisPtr = std::shared_ptr<const DrumAnalysis>;

//==============================================================================
enum class PlaybackMode : int { PreDrop = 0, FullClip, IntoDrop, Original };
enum class ExportRange : int { FullClip = 0, BuildZone, ZoneToEnd, FxLayer, Stems };
enum class ExportFormat : int { Wav16 = 0, Wav24, Wav32f, Aiff24 };

juce::StringArray playbackModeNames();
juce::StringArray exportFormatNames();

/** Everything the renderer needs. Copied by value onto the render thread
    (audio is shared through shared_ptr). */
struct DropModel
{
    ClipPtr music;       // AUDIO 1
    ClipPtr drums;       // AUDIO 2 (optional drum/808 stem)
    std::map<juce::String, ClipPtr> samples;   // your own riser / impact samples, by Move::sampleId

    double bpm = 130.0;
    bool tempoSync = true;
    int tsNum = 4, tsDen = 4;
    int clipStartBar = 1;            // session bar where the clip starts

    double zoneStart = 16.0;         // quarter-note beats from clip start
    double zoneEnd = 32.0;           // the drop

    std::vector<Move> moves;
    std::array<Move, numMoveTypes> templates;   // card values when no instance exists

    float sens808 = 0.55f, sensHats = 0.55f, sensSnare = 0.55f;
    int keyMode = -1;            // -1 auto (detected), -2 off (don't tune), 0..23 = tonic + 12 * minor

    // Tension macro: how linked moves come in across the build (curve over the zone)
    Move tension = [] { Move t; t.curve = curveS; return t; }();   // only curve / curvePoints are used
    bool anyTensionLinks() const { for (auto& m : moves) if (m.enabled && m.tensionLink) return true; return false; }

    /** The key used for tuning, or found == false when tuning is off / unknown. */
    KeyInfo effectiveKey (const KeyInfo& detected) const
    {
        if (keyMode == -2) return {};
        if (keyMode >= 0) { KeyInfo k; k.found = true; k.tonic = keyMode % 12; k.minor = keyMode >= 12; k.confidence = 1.0f; return k; }
        return detected;
    }
    int analysisRequest = 0;         // bump to force re-analysis

    double beatsPerBar() const noexcept { return tsNum * 4.0 / tsDen; }
    double samplesPerBeat (double sr) const noexcept { return sr * 60.0 / bpm; }
    double clipLengthBeats() const;  // music clip length in beats
};

juce::String formatBarPosition (double beatsFromClipStart, const DropModel&, bool withSixteenths = true);
juce::String formatLength (double beats, double beatsPerBar);

} // namespace km
