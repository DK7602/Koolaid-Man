#include "Presets.h"

namespace km
{

namespace
{
    Move mk (MoveType t, std::function<void (Move&)> tweak = {})
    {
        Move m = defaultMove (t);
        if (tweak) tweak (m);
        return m;
    }
}

std::vector<Preset> factoryPresets()
{
    using T = MoveType;
    std::vector<Preset> p;

    p.push_back ({ "Big Drop Energy", {
        mk (T::Reverse),
        mk (T::Chops),
        mk (T::Stutter),
        mk (T::Pitch),
        mk (T::Filter),
        mk (T::TapeStop) } });

    p.push_back ({ "Tape Stop Slam", {
        mk (T::Filter, [] (Move& m) { m.knob = 0.45f; m.knob2 = 0.3f; }),
        mk (T::Atmos,  [] (Move& m) { m.mode = 0; m.knob = 0.4f; m.knob2 = 0.3f; }),
        mk (T::TapeStop, [] (Move& m) { m.division = 6; m.knob = 0.65f; }) } });

    p.push_back ({ "Reverse Rush", {
        mk (T::Atmos,   [] (Move& m) { m.mode = 6; m.knob = 0.7f; m.knob2 = 0.6f; }),
        mk (T::Reverse, [] (Move& m) { m.mode = 1; m.division = 6; m.knob = 0.9f; }) } });

    p.push_back ({ "Snare Roll Build", {
        mk (T::DrumMoves, [] (Move& m) { m.mode = 0; m.knob = 0.85f; }),
        mk (T::Hats,      [] (Move& m) { m.mode = 0; m.knob = 0.6f; }),
        mk (T::Filter,    [] (Move& m) { m.mode = 1; m.knob = 0.62f; m.knob2 = 0.45f; }),
        mk (T::Pitch,     [] (Move& m) { m.semitones = 5; m.knob = 0.75f; }) } });

    p.push_back ({ "Stutter Riser", {
        mk (T::Stutter, [] (Move& m) { m.mode = 1; m.division = 3; m.knob = 0.75f; }),
        mk (T::Pitch,   [] (Move& m) { m.semitones = 12; m.knob = 0.7f; }),
        mk (T::Filter,  [] (Move& m) { m.mode = 1; m.knob = 0.55f; m.knob2 = 0.5f; }) } });

    p.push_back ({ "808 Dive", {
        mk (T::Chops,    [] (Move& m) { m.division = 2; m.knob = 0.7f; }),
        mk (T::Bass808,  [] (Move& m) { m.mode = 0; m.knob = 0.85f; }),
        mk (T::TapeStop, [] (Move& m) { m.division = 4; m.knob = 0.4f; }) } });

    p.push_back ({ "Atmos Wash", {
        mk (T::Filter,  [] (Move& m) { m.knob = 0.42f; m.knob2 = 0.35f; }),
        mk (T::Atmos,   [] (Move& m) { m.mode = 1; m.knob = 0.8f; m.knob2 = 0.75f; }),
        mk (T::Reverse, [] (Move& m) { m.division = 4; m.knob = 0.8f; }) } });

    p.push_back ({ "Chop Shop", {
        mk (T::DrumMoves, [] (Move& m) { m.mode = 1; m.knob = 0.7f; }),
        mk (T::Chops,     [] (Move& m) { m.mode = 1; m.division = 2; m.knob = 0.8f; m.seed = 7; }),
        mk (T::Stutter,   [] (Move& m) { m.mode = 2; m.division = 1; m.knob = 0.5f; }) } });

    p.push_back ({ "Pitch Dive", {
        mk (T::Pitch,  [] (Move& m) { m.semitones = -24; m.knob = 0.85f; }),
        mk (T::Filter, [] (Move& m) { m.knob = 0.3f; m.knob2 = 0.6f; }),
        mk (T::Atmos,  [] (Move& m) { m.mode = 5; m.knob = 0.6f; m.knob2 = 0.5f; }) } });

    p.push_back ({ "Shimmer Lift", {
        mk (T::Hats,      [] (Move& m) { m.mode = 1; m.knob = 0.55f; }),
        mk (T::DrumMoves, [] (Move& m) { m.mode = 2; m.knob = 0.75f; }),
        mk (T::Filter,    [] (Move& m) { m.mode = 1; m.knob = 0.58f; m.knob2 = 0.55f; }),
        mk (T::Atmos,     [] (Move& m) { m.mode = 2; m.knob = 0.7f; m.knob2 = 0.6f; }) } });

    p.push_back ({ "Init (Clean)", {} });

    //------------------------------------------------------------------ genre library
    auto G = [&] (const char* name, const char* genre, std::vector<Move> mv)
    {
        Preset x;
        x.name = name;
        x.genre = genre;
        x.moves = std::move (mv);
        p.push_back (x);
    };
    using M = Move&;

    // TRAP: hat rolls, 808 slides, snare rolls, silence before the drop
    G ("Trap Lord", "Trap", { mk (T::Hats, [] (M m) { m.mode = 0; m.knob = 0.7f; }), mk (T::DrumMoves, [] (M m) { m.mode = 2; m.knob = 0.75f; }),
                              mk (T::Bass808, [] (M m) { m.mode = 0; m.knob = 0.85f; }), mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }) });
    G ("808 Mafia Slide", "Trap", { mk (T::Filter, [] (M m) { m.mode = 0; m.knob = 0.42f; m.knob2 = 0.4f; }), mk (T::Bass808, [] (M m) { m.mode = 0; m.knob = 0.9f; }),
                                    mk (T::Gap, [] (M m) { m.mode = 0; m.division = 3; }), mk (T::Boom, [] (M m) { m.mode = 1; m.knob = 0.7f; }) });
    G ("Triplet Flex", "Trap", { mk (T::Hats, [] (M m) { m.mode = 1; m.knob = 0.65f; }), mk (T::Pitch, [] (M m) { m.semitones = -12; m.knob = 0.8f; }),
                                 mk (T::TapeStop, [] (M m) { m.division = 4; m.knob = 0.6f; }) });
    G ("Dark Trap Dip", "Trap", { mk (T::Filter, [] (M m) { m.mode = 3; m.knob = 0.35f; m.knob2 = 0.3f; }), mk (T::Atmos, [] (M m) { m.mode = 5; m.knob = 0.6f; m.knob2 = 0.4f; }),
                                  mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }), mk (T::Boom, [] (M m) { m.mode = 0; m.knob = 0.8f; }) });
    G ("Roll Call", "Trap", { mk (T::DrumMoves, [] (M m) { m.mode = 0; m.knob = 0.85f; }), mk (T::Hats, [] (M m) { m.mode = 0; m.knob = 0.6f; }),
                              mk (T::Stutter, [] (M m) { m.mode = 1; m.division = 2; m.knob = 0.3f; }) });
    G ("Tape Trap", "Trap", { mk (T::Chops, [] (M m) { m.mode = 0; m.division = 3; m.knob = 0.6f; }), mk (T::Pitch, [] (M m) { m.semitones = -7; m.knob = 0.7f; }),
                              mk (T::TapeStop, [] (M m) { m.division = 5; m.knob = 0.55f; }) });

    // DRILL: triplet hats, sliding 808s, gaps, half-time chops
    G ("UK Drill Slide", "Drill", { mk (T::Hats, [] (M m) { m.mode = 1; m.knob = 0.7f; }), mk (T::Bass808, [] (M m) { m.mode = 0; m.knob = 0.9f; }),
                                    mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }) });
    G ("NY Drill Stop", "Drill", { mk (T::Chops, [] (M m) { m.mode = 1; m.division = 3; m.knob = 0.6f; m.seed = 11; }), mk (T::Bass808, [] (M m) { m.mode = 1; m.knob = 0.85f; }),
                                   mk (T::TapeStop, [] (M m) { m.division = 4; m.knob = 0.7f; }) });
    G ("Glide Gang", "Drill", { mk (T::Hats, [] (M m) { m.mode = 1; m.knob = 0.6f; }), mk (T::Pitch, [] (M m) { m.semitones = -12; m.knob = 0.7f; }),
                                mk (T::Bass808, [] (M m) { m.mode = 0; m.knob = 0.85f; }), mk (T::Boom, [] (M m) { m.mode = 1; m.knob = 0.7f; }) });
    G ("Dark Opps", "Drill", { mk (T::Filter, [] (M m) { m.mode = 0; m.knob = 0.38f; m.knob2 = 0.5f; }), mk (T::Hats, [] (M m) { m.mode = 1; m.knob = 0.55f; }),
                               mk (T::Gap, [] (M m) { m.mode = 0; m.division = 5; }) });
    G ("Choppa", "Drill", { mk (T::Chops, [] (M m) { m.mode = 0; m.division = 2; m.knob = 0.8f; }), mk (T::Stutter, [] (M m) { m.mode = 2; m.division = 2; m.knob = 0.4f; }),
                            mk (T::Gap, [] (M m) { m.mode = 1; m.division = 4; }) });
    G ("Drill Reverse", "Drill", { mk (T::Hats, [] (M m) { m.mode = 1; m.knob = 0.6f; }), mk (T::Reverse, [] (M m) { m.mode = 1; m.division = 5; m.knob = 0.9f; }) });

    // HIP-HOP: filter dips, tape stops, reverses, gentle chops
    G ("Boom Bap Brake", "Hip-Hop", { mk (T::Filter, [] (M m) { m.mode = 0; m.knob = 0.4f; m.knob2 = 0.35f; }), mk (T::TapeStop, [] (M m) { m.division = 5; m.knob = 0.5f; }) });
    G ("Sample Flip", "Hip-Hop", { mk (T::Chops, [] (M m) { m.mode = 1; m.division = 3; m.knob = 0.7f; m.seed = 5; }), mk (T::Reverse, [] (M m) { m.division = 5; m.knob = 0.85f; }) });
    G ("Low End Theory", "Hip-Hop", { mk (T::Filter, [] (M m) { m.mode = 3; m.knob = 0.32f; m.knob2 = 0.45f; }), mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }) });
    G ("Vinyl Rewind", "Hip-Hop", { mk (T::Pitch, [] (M m) { m.semitones = -5; m.knob = 0.6f; }), mk (T::Reverse, [] (M m) { m.mode = 0; m.division = 6; m.knob = 0.9f; }) });
    G ("Hall Of Fame", "Hip-Hop", { mk (T::Atmos, [] (M m) { m.mode = 0; m.knob = 0.55f; m.knob2 = 0.45f; }), mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.5f; m.knob2 = 0.3f; }),
                                    mk (T::TapeStop, [] (M m) { m.division = 4; m.knob = 0.45f; }) });
    G ("Head Nod Stutter", "Hip-Hop", { mk (T::Stutter, [] (M m) { m.mode = 2; m.division = 3; m.knob = 0.5f; }), mk (T::Chops, [] (M m) { m.mode = 2; m.division = 4; m.knob = 0.5f; }) });

    // EDM: snare builds, risers, pitch up, impacts
    G ("Main Stage", "EDM", { mk (T::DrumMoves, [] (M m) { m.mode = 0; m.knob = 0.85f; }), mk (T::Riser, [] (M m) { m.mode = 1; m.division = 8; m.knob = 0.65f; }),
                              mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.55f; m.knob2 = 0.5f; }), mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }),
                              mk (T::Impact, [] (M m) { m.mode = 0; m.knob = 0.75f; }) });
    G ("Festival Snare", "EDM", { mk (T::DrumMoves, [] (M m) { m.mode = 2; m.knob = 0.9f; }), mk (T::Pitch, [] (M m) { m.semitones = 12; m.knob = 0.7f; }),
                                  mk (T::RevCrash, [] (M m) { m.division = 6; m.knob = 0.6f; }), mk (T::Impact, [] (M m) { m.mode = 0; m.knob = 0.7f; }) });
    G ("Big Room Lift", "EDM", { mk (T::Riser, [] (M m) { m.mode = 1; m.division = 8; m.knob = 0.7f; }), mk (T::Pump, [] (M m) { m.mode = 2; m.knob = 0.7f; }),
                                 mk (T::Stutter, [] (M m) { m.mode = 1; m.division = 2; m.knob = 0.3f; }), mk (T::Impact, [] (M m) { m.mode = 2; m.knob = 0.7f; }) });
    G ("Euphoria", "EDM", { mk (T::Atmos, [] (M m) { m.mode = 2; m.knob = 0.7f; m.knob2 = 0.55f; }), mk (T::Riser, [] (M m) { m.mode = 0; m.division = 7; m.knob = 0.6f; }),
                            mk (T::Pitch, [] (M m) { m.semitones = 7; m.knob = 0.6f; }), mk (T::RevCrash, [] (M m) { m.division = 6; m.knob = 0.6f; }) });
    G ("Laser Build", "EDM", { mk (T::DrumMoves, [] (M m) { m.mode = 0; m.knob = 0.8f; }), mk (T::Filter, [] (M m) { m.mode = 2; m.knob = 0.45f; m.knob2 = 0.7f; }),
                               mk (T::Riser, [] (M m) { m.mode = 2; m.division = 7; m.knob = 0.6f; }) });
    G ("Drop The Bass", "EDM", { mk (T::Riser, [] (M m) { m.mode = 1; m.division = 7; m.knob = 0.65f; }), mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }),
                                 mk (T::Impact, [] (M m) { m.mode = 1; m.knob = 0.8f; }), mk (T::Boom, [] (M m) { m.mode = 0; m.knob = 0.75f; }) });

    // HOUSE: filter sweeps, pumping, crash swells
    G ("Filter House", "House", { mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.6f; m.knob2 = 0.55f; }), mk (T::Pump, [] (M m) { m.mode = 3; m.knob = 0.65f; }),
                                  mk (T::RevCrash, [] (M m) { m.division = 6; m.knob = 0.55f; }) });
    G ("Disco Pump", "House", { mk (T::Pump, [] (M m) { m.mode = 2; m.knob = 0.75f; }), mk (T::Hats, [] (M m) { m.mode = 2; m.knob = 0.55f; }),
                                mk (T::Filter, [] (M m) { m.mode = 0; m.knob = 0.45f; m.knob2 = 0.45f; }) });
    G ("Deep Swell", "House", { mk (T::Filter, [] (M m) { m.mode = 3; m.knob = 0.4f; m.knob2 = 0.4f; }), mk (T::Atmos, [] (M m) { m.mode = 4; m.knob = 0.5f; m.knob2 = 0.5f; }),
                                mk (T::Riser, [] (M m) { m.mode = 0; m.division = 7; m.knob = 0.5f; }) });
    G ("Tech Roll", "House", { mk (T::DrumMoves, [] (M m) { m.mode = 2; m.knob = 0.7f; }), mk (T::Pump, [] (M m) { m.mode = 3; m.knob = 0.6f; }),
                               mk (T::Stutter, [] (M m) { m.mode = 2; m.division = 2; m.knob = 0.35f; }) });
    G ("Piano Lift", "House", { mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.55f; m.knob2 = 0.4f; }), mk (T::Riser, [] (M m) { m.mode = 1; m.division = 7; m.knob = 0.5f; }),
                                mk (T::Impact, [] (M m) { m.mode = 2; m.knob = 0.55f; }) });
    G ("Late Night Pump", "House", { mk (T::Pump, [] (M m) { m.mode = 3; m.division = 4; m.knob = 0.7f; }), mk (T::Atmos, [] (M m) { m.mode = 3; m.knob = 0.5f; m.knob2 = 0.4f; }),
                                     mk (T::Gap, [] (M m) { m.mode = 2; m.division = 4; }) });

    // BASS: accelerating stutters, pitch dives, gaps, sub booms
    G ("Wobble Warning", "Bass", { mk (T::Stutter, [] (M m) { m.mode = 1; m.division = 2; m.knob = 0.55f; }), mk (T::Pitch, [] (M m) { m.semitones = -12; m.knob = 0.7f; }),
                                   mk (T::Gap, [] (M m) { m.mode = 0; m.division = 4; }), mk (T::Boom, [] (M m) { m.mode = 0; m.knob = 0.85f; }) });
    G ("Riddim Gate", "Bass", { mk (T::Chops, [] (M m) { m.mode = 0; m.division = 2; m.knob = 0.85f; }), mk (T::Riser, [] (M m) { m.mode = 1; m.division = 7; m.knob = 0.6f; }),
                                mk (T::Boom, [] (M m) { m.mode = 1; m.knob = 0.8f; }) });
    G ("Sub Drop", "Bass", { mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.62f; m.knob2 = 0.5f; }), mk (T::Gap, [] (M m) { m.mode = 0; m.division = 5; }),
                             mk (T::Boom, [] (M m) { m.mode = 2; m.division = 7; m.knob = 0.85f; }) });
    G ("Machine Gun", "Bass", { mk (T::DrumMoves, [] (M m) { m.mode = 2; m.knob = 0.85f; }), mk (T::Stutter, [] (M m) { m.mode = 1; m.division = 1; m.knob = 0.45f; }),
                                mk (T::Impact, [] (M m) { m.mode = 1; m.knob = 0.8f; }) });
    G ("Dive Bomb", "Bass", { mk (T::Pitch, [] (M m) { m.semitones = -24; m.knob = 0.85f; }), mk (T::TapeStop, [] (M m) { m.division = 5; m.knob = 0.6f; }),
                              mk (T::Boom, [] (M m) { m.mode = 0; m.knob = 0.8f; }) });
    G ("Space Bass", "Bass", { mk (T::Atmos, [] (M m) { m.mode = 5; m.knob = 0.7f; m.knob2 = 0.55f; }), mk (T::Riser, [] (M m) { m.mode = 1; m.division = 8; m.knob = 0.6f; }),
                               mk (T::Gap, [] (M m) { m.mode = 1; m.division = 4; }), mk (T::Impact, [] (M m) { m.mode = 0; m.knob = 0.75f; }) });

    // POP / FUTURE BASS: shimmer, chopped lifts, crash swells
    G ("Shimmer Pop", "Pop", { mk (T::Atmos, [] (M m) { m.mode = 2; m.knob = 0.65f; m.knob2 = 0.5f; }), mk (T::Chops, [] (M m) { m.mode = 2; m.division = 3; m.knob = 0.5f; }),
                               mk (T::RevCrash, [] (M m) { m.mode = 1; m.division = 6; m.knob = 0.55f; }) });
    G ("Future Lift", "Pop", { mk (T::Chops, [] (M m) { m.mode = 0; m.division = 3; m.knob = 0.65f; }), mk (T::Pitch, [] (M m) { m.semitones = 12; m.knob = 0.65f; }),
                               mk (T::Riser, [] (M m) { m.mode = 1; m.division = 7; m.knob = 0.55f; }) });
    G ("Radio Edit", "Pop", { mk (T::Filter, [] (M m) { m.mode = 1; m.knob = 0.5f; m.knob2 = 0.4f; }), mk (T::Reverse, [] (M m) { m.mode = 1; m.division = 5; m.knob = 0.8f; }),
                              mk (T::Impact, [] (M m) { m.mode = 2; m.knob = 0.55f; }) });
    G ("Sparkle Swell", "Pop", { mk (T::Atmos, [] (M m) { m.mode = 6; m.knob = 0.6f; m.knob2 = 0.55f; }), mk (T::RevCrash, [] (M m) { m.mode = 0; m.division = 6; m.knob = 0.6f; }) });
    G ("Heartbeat Pump", "Pop", { mk (T::Pump, [] (M m) { m.mode = 0; m.knob = 0.6f; }), mk (T::Atmos, [] (M m) { m.mode = 3; m.knob = 0.5f; m.knob2 = 0.4f; }),
                                  mk (T::Gap, [] (M m) { m.mode = 1; m.division = 4; }) });
    G ("Hook Rush", "Pop", { mk (T::DrumMoves, [] (M m) { m.mode = 0; m.knob = 0.7f; }), mk (T::Riser, [] (M m) { m.mode = 0; m.division = 7; m.knob = 0.5f; }),
                             mk (T::Stutter, [] (M m) { m.mode = 1; m.division = 3; m.knob = 0.3f; }) });

    // CHILL / LO-FI: muffled filters, washes, slow tape
    G ("Lo-Fi Fade", "Chill", { mk (T::Filter, [] (M m) { m.mode = 0; m.knob = 0.32f; m.knob2 = 0.3f; }), mk (T::TapeStop, [] (M m) { m.division = 6; m.knob = 0.4f; }) });
    G ("Rainy Window", "Chill", { mk (T::Atmos, [] (M m) { m.mode = 3; m.knob = 0.7f; m.knob2 = 0.55f; }), mk (T::Filter, [] (M m) { m.mode = 3; m.knob = 0.36f; m.knob2 = 0.3f; }) });
    G ("Sunset Wash", "Chill", { mk (T::Atmos, [] (M m) { m.mode = 1; m.knob = 0.75f; m.knob2 = 0.6f; }), mk (T::Reverse, [] (M m) { m.mode = 2; m.knob = 0.4f; }) });
    G ("Tape Warble", "Chill", { mk (T::Pitch, [] (M m) { m.semitones = -2; m.mode = 1; m.knob = 0.5f; }), mk (T::Filter, [] (M m) { m.mode = 3; m.knob = 0.4f; m.knob2 = 0.4f; }),
                                 mk (T::TapeStop, [] (M m) { m.division = 5; m.knob = 0.35f; }) });
    G ("Cloud Nine", "Chill", { mk (T::Atmos, [] (M m) { m.mode = 3; m.knob = 0.6f; m.knob2 = 0.5f; }), mk (T::Chops, [] (M m) { m.mode = 2; m.division = 4; m.knob = 0.35f; }) });
    G ("Slow Motion", "Chill", { mk (T::Pitch, [] (M m) { m.semitones = -12; m.knob = 0.5f; }), mk (T::Atmos, [] (M m) { m.mode = 0; m.knob = 0.5f; m.knob2 = 0.4f; }),
                                 mk (T::Gap, [] (M m) { m.mode = 1; m.division = 5; m.knob = 0.8f; }) });
    return p;
}

//==============================================================================
static juce::File appFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Koolaid Man");
}

juce::File userPresetFolder()
{
    auto f = appFolder().getChildFile ("Presets");
    f.createDirectory();
    return f;
}

juce::File exportFolder()
{
    auto f = appFolder().getChildFile ("Exports");
    f.createDirectory();
    return f;
}

juce::ValueTree movesToTree (const std::vector<Move>& moves)
{
    juce::ValueTree t ("MOVES");
    for (auto& m : moves)
        t.appendChild (m.toTree(), nullptr);
    return t;
}

std::vector<Move> movesFromTree (const juce::ValueTree& t)
{
    std::vector<Move> moves;
    for (auto c : t)
        if (c.hasType ("MOVE"))
            moves.push_back (Move::fromTree (c));
    return moves;
}

std::vector<Preset> loadUserPresets()
{
    std::vector<Preset> out;
    for (auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.kmpreset"))
    {
        if (auto xml = juce::XmlDocument::parse (f))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            Preset p;
            p.name = tree.getProperty ("name", f.getFileNameWithoutExtension());
            p.moves = movesFromTree (tree.getChildWithName ("MOVES"));
            p.factory = false;
            p.file = f;
            out.push_back (std::move (p));
        }
    }
    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

bool saveUserPreset (const juce::String& name, const std::vector<Move>& moves)
{
    juce::ValueTree t ("KOOLAID_PRESET");
    t.setProperty ("name", name, nullptr);
    t.appendChild (movesToTree (moves), nullptr);
    auto file = userPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".kmpreset");
    if (auto xml = t.createXml())
        return xml->writeTo (file);
    return false;
}

//==============================================================================
const juce::StringArray& presetGenres()
{
    static const juce::StringArray g { "Classic", "Trap", "Drill", "Hip-Hop", "EDM", "House", "Bass", "Pop", "Chill" };
    return g;
}

const juce::StringArray& rollStyleNames()
{
    static const juce::StringArray s { "Any", "Trap", "Drill", "Hip-Hop", "EDM", "House", "Bass", "Pop", "Chill" };
    return s;
}

std::vector<Move> rollMoves (juce::Random& rng, bool haveDrums, int style)
{
    using T = MoveType;
    style = juce::jlimit (0, rollStyleNames().size() - 1, style);
    enum { Any, Trap, Drill, HipHop, EDM, House, Bass, Pop, Chill };
    std::vector<Move> moves;
    auto add = [&] (T t, std::function<void (Move&)> tweak)
    {
        Move m = defaultMove (t);
        m.seed = (juce::uint32) rng.nextInt (1 << 30);
        tweak (m);
        moves.push_back (m);
    };
    const auto pick = [&] (std::initializer_list<int> options) { return *(options.begin() + rng.nextInt ((int) options.size())); };
    const auto weighted = [&] (const std::vector<float>& w)
    {
        float total = 0;
        for (auto x : w) total += x;
        float r = rng.nextFloat() * total;
        for (size_t k = 0; k < w.size(); ++k) { r -= w[k]; if (r <= 0) return (int) k; }
        return (int) w.size() - 1;
    };

    // how each style builds: weights for the body moves
    //                                   Chops Filt  Pitch Atmos Drums Hats  Stut  Riser Pump  Crash
    static const float bodyW[9][10] = { { 1,    1,    1,    1,    1,    1,    1,    1,    1,    1 },     // Any
                                        { 1,    1,    1.5f, .5f,  2.5f, 3,    1,    .5f,  .5f,  .5f },   // Trap
                                        { 1.5f, .5f,  1,    .3f,  1.5f, 3,    1,    .3f,  .3f,  .3f },   // Drill
                                        { 1.5f, 3,    .7f,  1,    1,    1,    .6f,  .3f,  .5f,  .5f },   // Hip-Hop
                                        { 1,    2,    2,    1,    3,    1.5f, 1.5f, 3,    2,    2 },     // EDM
                                        { 1,    3,    .5f,  1,    1,    1.5f, .7f,  2,    3,    2 },     // House
                                        { 1.5f, 1.5f, 2,    1,    1.5f, 1,    3,    2.5f, 1,    1.5f },  // Bass
                                        { 2,    1.5f, 1.5f, 3,    1,    1,    1,    2,    1,    2 },     // Pop
                                        { .5f,  3,    1,    3,    .3f,  .5f,  .5f,  .5f,  .5f,  .7f } }; // Chill
    static const T bodyTypes[10] = { T::Chops, T::Filter, T::Pitch, T::Atmos, T::DrumMoves, T::Hats, T::Stutter, T::Riser, T::Pump, T::RevCrash };
    //                                   Tape  Rev   Stut  Gap   none
    static const float endW[9][5] = { { 2,    2,    1,    1,    1 },
                                      { 2,    1,    1,    3,    1 },
                                      { 1,    1,    1,    3,    1 },
                                      { 3,    2,    1,    1,    1 },
                                      { .5f,  1,    2,    1,    2 },
                                      { .5f,  2,    1,    1,    2 },
                                      { 1,    1,    3,    3,    .5f },
                                      { 1,    2,    1,    1,    1 },
                                      { 3,    2,    .3f,  .5f,  1 } };
    static const float p808[9]   = { .3f, .7f, .8f, .4f, .2f, .1f, .3f, .2f, .1f };
    static const float pAfter[9] = { .35f, .4f, .4f, .3f, .8f, .5f, .8f, .5f, .2f };

    std::vector<float> w (bodyW[style], bodyW[style] + 10);
    if (! haveDrums) { w[4] *= 0.4f; w[5] *= 0.4f; w[1] += 1.0f; }
    const int numBody = 2 + rng.nextInt (2);
    for (int k = 0; k < numBody; ++k)
    {
        const int idx = weighted (w);
        w[(size_t) idx] = 0.0f;   // no repeats
        const T t = bodyTypes[idx];
        switch (t)
        {
            case T::Chops:     add (t, [&] (Move& m) { m.mode = style == Drill ? pick ({ 0, 1 }) : pick ({ 0, 0, 1, 2 }); m.division = pick ({ 2, 3, 3, 4 }); m.knob = 0.5f + 0.45f * rng.nextFloat(); }); break;
            case T::Filter:    add (t, [&] (Move& m)
                               {
                                   m.mode = style == House || style == EDM ? pick ({ 1, 1, 0 }) : style == Chill || style == HipHop ? pick ({ 0, 0, 3 }) : pick ({ 0, 0, 1, 2 });
                                   m.knob = m.mode == 1 ? 0.5f + 0.2f * rng.nextFloat() : 0.28f + 0.2f * rng.nextFloat();
                                   m.knob2 = 0.25f + 0.5f * rng.nextFloat();
                               }); break;
            case T::Pitch:     add (t, [&] (Move& m)
                               {
                                   m.semitones = style == EDM || style == Pop ? pick ({ 5, 7, 12, 12 }) : style == Bass ? pick ({ -12, -24, -12 })
                                               : pick ({ -24, -12, -12, -7, 5, 7, 12 });
                                   m.knob = 0.4f + 0.5f * rng.nextFloat();
                               }); break;
            case T::Atmos:     add (t, [&] (Move& m) { m.mode = style == Chill ? pick ({ 0, 3, 4 }) : style == Pop ? pick ({ 2, 3 }) : rng.nextInt (7); m.knob = 0.35f + 0.6f * rng.nextFloat(); m.knob2 = 0.3f + 0.45f * rng.nextFloat(); }); break;
            case T::DrumMoves: add (t, [&] (Move& m) { m.mode = style == EDM ? pick ({ 0, 2 }) : pick ({ 0, 0, 2, 1 }); m.knob = 0.6f + 0.35f * rng.nextFloat(); }); break;
            case T::Hats:      add (t, [&] (Move& m) { m.mode = style == Drill ? pick ({ 1, 1, 0 }) : style == Trap ? pick ({ 0, 1 }) : pick ({ 0, 1, 2 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            case T::Stutter:   add (t, [&] (Move& m) { m.mode = style == Bass ? 1 : pick ({ 0, 1, 1, 2 }); m.division = pick ({ 1, 2, 2, 3 }); m.knob = 0.3f + 0.45f * rng.nextFloat(); }); break;
            case T::Riser:     add (t, [&] (Move& m) { m.mode = style == EDM || style == Bass ? pick ({ 0, 1, 1 }) : pick ({ 0, 0, 1, 2 }); m.division = pick ({ 6, 7, 7, 8 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            case T::Pump:      add (t, [&] (Move& m) { m.mode = haveDrums ? pick ({ 3, 3, 0, 1, 2 }) : pick ({ 0, 0, 1, 2 }); m.division = pick ({ 4, 4, 3 }); m.knob = 0.4f + 0.45f * rng.nextFloat(); }); break;
            case T::RevCrash:  add (t, [&] (Move& m) { m.mode = pick ({ 0, 1, 2 }); m.division = pick ({ 5, 6, 6, 7 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            default: break;
        }
    }

    if (rng.nextFloat() < p808[style])
        add (T::Bass808, [&] (Move& m) { m.mode = style == Trap || style == Drill ? 0 : pick ({ 0, 0, 1, 2 }); m.knob = 0.7f + 0.25f * rng.nextFloat(); });

    // the ending into the drop
    switch (weighted ({ endW[style], endW[style] + 5 }))
    {
        case 0: add (T::TapeStop, [&] (Move& m) { m.mode = pick ({ 0, 0, 0, 1 }); m.division = pick ({ 4, 5, 5, 6 }); m.knob = 0.3f + 0.5f * rng.nextFloat(); }); break;
        case 1: add (T::Reverse,  [&] (Move& m) { m.mode = pick ({ 0, 0, 1 }); m.division = pick ({ 3, 4, 5, 5, 6 }); m.knob = 0.7f + 0.3f * rng.nextFloat(); }); break;
        case 2:
            if (std::none_of (moves.begin(), moves.end(), [] (const Move& m) { return m.type == T::Stutter; }))
                add (T::Stutter, [&] (Move& m) { m.mode = 1; m.division = pick ({ 2, 3 }); m.knob = 0.25f + 0.3f * rng.nextFloat(); });
            break;
        case 3: add (T::Gap, [&] (Move& m) { m.mode = style == Trap || style == Drill || style == Bass ? 0 : pick ({ 0, 1, 2 }); m.division = pick ({ 3, 4, 4 }); m.knob = 0.85f + 0.15f * rng.nextFloat(); }); break;
        default: break; // straight into the drop
    }

    // something on the downbeat
    if (rng.nextFloat() < pAfter[style])
    {
        const bool boom = style == Bass || style == Trap || style == Drill ? rng.nextFloat() < 0.7f : rng.nextFloat() < 0.35f;
        add (boom ? T::Boom : T::Impact, [&] (Move& m) { m.mode = rng.nextInt (3); m.knob = 0.5f + 0.4f * rng.nextFloat(); });
    }

    std::stable_sort (moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return canonicalOrder (a.type) < canonicalOrder (b.type); });
    return moves;
}

} // namespace km
