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
std::vector<Move> rollMoves (juce::Random& rng, bool haveDrums)
{
    using T = MoveType;
    std::vector<Move> moves;
    auto add = [&] (T t, std::function<void (Move&)> tweak)
    {
        Move m = defaultMove (t);
        m.seed = (juce::uint32) rng.nextInt (1 << 30);
        tweak (m);
        moves.push_back (m);
    };
    const auto pick = [&] (std::initializer_list<int> options) { return *(options.begin() + rng.nextInt ((int) options.size())); };

    // body: 2-3 moves that shape the build
    std::vector<T> body { T::Chops, T::Filter, T::Pitch, T::Atmos, T::DrumMoves, T::Hats, T::Stutter, T::Riser, T::Pump, T::RevCrash };
    if (! haveDrums) body.push_back (T::Filter);
    for (int i = (int) body.size() - 1; i > 0; --i)
        std::swap (body[(size_t) i], body[(size_t) rng.nextInt (i + 1)]);
    const int numBody = 2 + rng.nextInt (2);
    bool usedFilter = false;
    for (int i = 0; i < numBody; ++i)
    {
        const T t = body[(size_t) i];
        if (t == T::Filter && usedFilter) continue;
        usedFilter |= t == T::Filter;
        switch (t)
        {
            case T::Chops:     add (t, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.division = pick ({ 2, 3, 3, 4 }); m.knob = 0.5f + 0.45f * rng.nextFloat(); }); break;
            case T::Filter:    add (t, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.knob = m.mode == 1 ? 0.5f + 0.2f * rng.nextFloat() : 0.28f + 0.2f * rng.nextFloat(); m.knob2 = 0.25f + 0.5f * rng.nextFloat(); }); break;
            case T::Pitch:     add (t, [&] (Move& m) { m.semitones = pick ({ -24, -12, -12, -7, 5, 7, 12 }); m.knob = 0.4f + 0.5f * rng.nextFloat(); }); break;
            case T::Atmos:     add (t, [&] (Move& m) { m.mode = rng.nextInt (7); m.knob = 0.35f + 0.6f * rng.nextFloat(); m.knob2 = 0.3f + 0.45f * rng.nextFloat(); }); break;
            case T::DrumMoves: add (t, [&] (Move& m) { m.mode = pick ({ 0, 0, 2, 1 }); m.knob = 0.6f + 0.35f * rng.nextFloat(); }); break;
            case T::Hats:      add (t, [&] (Move& m) { m.mode = pick ({ 0, 1, 2 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            case T::Stutter:   add (t, [&] (Move& m) { m.mode = pick ({ 0, 1, 1, 2 }); m.division = pick ({ 1, 2, 2, 3 }); m.knob = 0.3f + 0.45f * rng.nextFloat(); }); break;
            case T::Riser:     add (t, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.division = pick ({ 6, 7, 7, 8 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            case T::Pump:      add (t, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.division = pick ({ 4, 4, 3 }); m.knob = 0.4f + 0.45f * rng.nextFloat(); }); break;
            case T::RevCrash:  add (t, [&] (Move& m) { m.mode = pick ({ 0, 1, 2 }); m.division = pick ({ 5, 6, 6, 7 }); m.knob = 0.45f + 0.4f * rng.nextFloat(); }); break;
            default: break;
        }
    }

    // sometimes an 808 under it
    if (rng.nextFloat() < 0.3f)
        add (T::Bass808, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.knob = 0.7f + 0.25f * rng.nextFloat(); });

    // the ending into the drop
    switch (pick ({ 0, 0, 1, 1, 2, 3 }))
    {
        case 0: add (T::TapeStop, [&] (Move& m) { m.mode = pick ({ 0, 0, 0, 1 }); m.division = pick ({ 4, 5, 5, 6 }); m.knob = 0.3f + 0.5f * rng.nextFloat(); }); break;
        case 1: add (T::Reverse,  [&] (Move& m) { m.mode = pick ({ 0, 0, 1 }); m.division = pick ({ 3, 4, 5, 5, 6 }); m.knob = 0.7f + 0.3f * rng.nextFloat(); }); break;
        case 2: add (T::Stutter,  [&] (Move& m) { m.mode = 1; m.division = pick ({ 2, 3 }); m.knob = 0.25f + 0.3f * rng.nextFloat(); }); break;
        default: break; // straight into the drop
    }

    // sometimes a gap right before the drop, and something on the downbeat
    if (rng.nextFloat() < 0.25f)
        add (T::Gap, [&] (Move& m) { m.mode = pick ({ 0, 0, 1, 2 }); m.division = pick ({ 3, 4, 4 }); m.knob = 0.8f + 0.2f * rng.nextFloat(); });
    if (rng.nextFloat() < 0.35f)
        add (pick ({ 0, 1 }) == 0 ? T::Impact : T::Boom, [&] (Move& m) { m.mode = rng.nextInt (3); m.knob = 0.5f + 0.4f * rng.nextFloat(); });

    std::stable_sort (moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return canonicalOrder (a.type) < canonicalOrder (b.type); });
    return moves;
}

} // namespace km
