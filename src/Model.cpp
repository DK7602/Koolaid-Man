#include "Model.h"

namespace km
{

const std::vector<Division>& divisions()
{
    static const std::vector<Division> d {
        { "1/64",  "1/64",   1.0 / 64.0 },
        { "1/32",  "1/32",   1.0 / 32.0 },
        { "1/16",  "1/16",   1.0 / 16.0 },
        { "1/8",   "1/8",    1.0 / 8.0 },
        { "1/4",   "1/4",    1.0 / 4.0 },
        { "1/2",   "1/2",    1.0 / 2.0 },
        { "1 BAR", "1 Bar",  -1.0 },
        { "2 BAR", "2 Bars", -2.0 },
        { "4 BAR", "4 Bars", -4.0 },
    };
    return d;
}

double divisionToBeats (int index, double beatsPerBar)
{
    const auto& d = divisions();
    index = juce::jlimit (0, (int) d.size() - 1, index);
    const double w = d[(size_t) index].wholeNotes;
    return w < 0 ? -w * beatsPerBar : w * 4.0;
}

const juce::StringArray& atmosTypeNames()
{
    static const juce::StringArray names { "Hall", "Cathedral", "Shimmer", "Cloud", "Plate", "Deep Space", "Reverse Swell" };
    return names;
}

const char* typeName (MoveType t)
{
    switch (t)
    {
        case MoveType::Reverse:   return "Reverse";
        case MoveType::Chops:     return "Chop";
        case MoveType::Stutter:   return "Stutter";
        case MoveType::Pitch:     return "Pitch";
        case MoveType::TapeStop:  return "Tape Stop";
        case MoveType::Filter:    return "Filter";
        case MoveType::DrumMoves: return "Drums";
        case MoveType::Bass808:   return "808";
        case MoveType::Hats:      return "Hats";
        case MoveType::Atmos:     return "Atmos";
        case MoveType::Riser:     return "Riser";
        case MoveType::RevCrash:  return "Reverse Crash";
        case MoveType::Pump:      return "Pump";
        case MoveType::Gap:       return "Drop Gap";
        case MoveType::Impact:    return "Impact";
        case MoveType::Boom:      return "Sub Boom";
        default: break;
    }
    return "";
}

const char* cardTitle (MoveType t)
{
    switch (t)
    {
        case MoveType::Reverse:   return "REVERSE";
        case MoveType::Chops:     return "CHOPS";
        case MoveType::Stutter:   return "STUTTER";
        case MoveType::Pitch:     return "PITCH";
        case MoveType::TapeStop:  return "TAPE STOP";
        case MoveType::Filter:    return "FILTERS";
        case MoveType::DrumMoves: return "DRUM MOVES";
        case MoveType::Bass808:   return "808 DETECT";
        case MoveType::Hats:      return "HAT DETECT";
        case MoveType::Atmos:     return "ATMOS REVERB";
        case MoveType::Riser:     return "NOISE RISER";
        case MoveType::RevCrash:  return "REVERSE CRASH";
        case MoveType::Pump:      return "SIDECHAIN PUMP";
        case MoveType::Gap:       return "DROP GAP";
        case MoveType::Impact:    return "IMPACT";
        case MoveType::Boom:      return "SUB BOOM";
        default: break;
    }
    return "";
}

juce::StringArray modeNames (MoveType t)
{
    switch (t)
    {
        case MoveType::Reverse:   return { "Into The Drop", "Pre-verse (Downbeat)", "Whole Zone" };
        case MoveType::Chops:     return { "Gate", "Shuffle", "Tremolo" };
        case MoveType::Stutter:   return { "Repeat", "Accelerate", "Beat Repeat" };
        case MoveType::Pitch:     return { "Ramp", "Static" };
        case MoveType::TapeStop:  return { "Stop", "Spin-Up" };
        case MoveType::Filter:    return { "LP Sweep", "HP Sweep", "BP Sweep", "LP Static", "HP Static" };
        case MoveType::DrumMoves: return { "Fill", "Gate", "Roll" };
        case MoveType::Bass808:   return { "Slide Down", "Slide Up", "Sub Swell" };
        case MoveType::Hats:      return { "Roll", "Triplet Roll", "Open Hats" };
        case MoveType::Atmos:     return atmosTypeNames();
        case MoveType::Riser:     return { "Noise Sweep", "Uplifter", "Snare Wash" };
        case MoveType::RevCrash:  return { "Crash", "Splash", "Dark Wash" };
        case MoveType::Pump:      return { "Smooth", "Hard", "Rising", "Follow Kick" };
        case MoveType::Gap:       return { "Silence", "Low-pass", "High-pass" };
        case MoveType::Impact:    return { "Hit + Crash", "Hit Only", "Crash + Downlifter" };
        case MoveType::Boom:      return { "Deep", "Punchy", "Long Dive" };
        default: break;
    }
    return {};
}

juce::String knobName (MoveType t, int which)
{
    switch (t)
    {
        case MoveType::Reverse:   return "Mix";
        case MoveType::Chops:     return "Depth";
        case MoveType::Stutter:   return "Span";
        case MoveType::Pitch:     return "Curve";
        case MoveType::TapeStop:  return "Brake";
        case MoveType::Filter:    return which == 0 ? "Cutoff" : "Res";
        case MoveType::DrumMoves: return "Level";
        case MoveType::Bass808:   return "Sens";
        case MoveType::Hats:      return "Sens";
        case MoveType::Atmos:     return which == 0 ? "Size" : "Mix";
        case MoveType::Riser:
        case MoveType::RevCrash:
        case MoveType::Impact:
        case MoveType::Boom:      return "Level";
        case MoveType::Pump:
        case MoveType::Gap:       return "Depth";
        default: break;
    }
    return {};
}

static double stutterSpanBars (float knob) { return std::pow (2.0, -2.0 + 4.0 * (double) knob); }
static double filterCutoffHz (float knob)  { return 20.0 * std::pow (1000.0, (double) knob); }

juce::String knobValueText (const Move& m, int which)
{
    const float v = which == 0 ? m.knob : m.knob2;
    switch (m.type)
    {
        case MoveType::Stutter:
        {
            const double bars = stutterSpanBars (v);
            return bars < 1.0 ? juce::String (bars * 4.0, 1) + " beats" : juce::String (bars, 1) + " bars";
        }
        case MoveType::Filter:
            if (which == 0)
            {
                const double hz = filterCutoffHz (v);
                return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String ((int) hz) + " Hz";
            }
            break;
        default: break;
    }
    return juce::String (juce::roundToInt (v * 100.0f)) + "%";
}

const juce::StringArray& curveNames()
{
    static const juce::StringArray names { "Auto", "Linear", "Ease In", "Ease Out", "S-Curve", "Late Rush", "Steps", "Custom" };
    return names;
}

bool moveHasRamp (const Move& m)
{
    switch (m.type)
    {
        case MoveType::Pitch:     return m.mode == 0;
        case MoveType::Filter:    return m.mode <= 2;
        case MoveType::Atmos:
        case MoveType::Riser:
        case MoveType::Bass808:
        case MoveType::Hats:      return true;
        case MoveType::DrumMoves: return m.mode != 1;
        case MoveType::Pump:      return m.mode == 2;
        default: break;
    }
    return false;
}

double shapeCurve (const Move& m, double t, double autoPow)
{
    t = juce::jlimit (0.0, 1.0, t);
    switch (m.curve)
    {
        case curveLinear:  return t;
        case curveEaseIn:  return std::pow (t, 2.2);
        case curveEaseOut: return 1.0 - std::pow (1.0 - t, 2.2);
        case curveS:       return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);   // smootherstep
        case curveLate:    return std::pow (t, 5.0);
        case curveSteps:   return juce::jmin (1.0, std::floor (t * 4.0 + 1.0e-9) / 3.0);   // 4 steps, full on the last
        case curveCustom:
        {
            const double x = t * 15.0;
            const int i = juce::jmin (14, (int) x);
            const double f = x - i;
            return juce::jlimit (0.0, 1.0, (double) m.curvePoints[(size_t) i] * (1.0 - f) + (double) m.curvePoints[(size_t) i + 1] * f);
        }
        default: break;
    }
    return std::pow (t, autoPow);
}

juce::String Move::stackLabel() const
{
    const auto& d = divisions();
    const juce::String div = d[(size_t) juce::jlimit (0, (int) d.size() - 1, division)].longLabel;
    const auto modes = modeNames (type);
    const juce::String modeName = modes[juce::jlimit (0, juce::jmax (0, modes.size() - 1), mode)];

    switch (type)
    {
        case MoveType::Reverse:
        {
            juce::String len = div.containsChar ('/') ? div + " Bar" : div;
            return mode == 0 ? "Reverse (" + len + ")" : "Reverse (" + len + ", " + (mode == 1 ? "Pre-verse" : "Zone") + ")";
        }
        case MoveType::Chops:     return "Chop (" + div + ", " + modeName + ")";
        case MoveType::Stutter:   return mode == 0 ? "Stutter (" + div + ")" : "Stutter (" + div + ", " + modeName + ")";
        case MoveType::Pitch:
        {
            juce::String st = (semitones > 0 ? "+" : (semitones < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : juce::String()))
                              + juce::String (std::abs (semitones)) + " st";
            return mode == 0 ? "Pitch (" + st + ")" : "Pitch (" + st + ", Static)";
        }
        case MoveType::TapeStop:  return (mode == 0 ? "Tape Stop (" : "Tape Spin-Up (") + div + ")";
        case MoveType::Filter:    return "Filter (" + modeName + ")";
        case MoveType::DrumMoves: return "Drum " + modeName + " (Snare)";
        case MoveType::Bass808:   return "808 (" + modeName + ")";
        case MoveType::Hats:      return "Hats (" + modeName + ")";
        case MoveType::Atmos:     return "Atmos (" + modeName + ")";
        case MoveType::Riser:     return "Riser (" + div + ", " + modeName + ")";
        case MoveType::RevCrash:  return "Reverse Crash (" + div + ")";
        case MoveType::Pump:      return "Pump (" + div + ", " + modeName + ")";
        case MoveType::Gap:       return "Drop Gap (" + div + ", " + modeName + ")";
        case MoveType::Impact:    return "Impact (" + modeName + ")";
        case MoveType::Boom:      return "Sub Boom (" + modeName + ")";
        default: break;
    }
    return {};
}

Move defaultMove (MoveType t)
{
    Move m;
    m.type = t;
    switch (t)
    {
        case MoveType::Reverse:   m.division = 5; m.knob = 0.85f; break;
        case MoveType::Chops:     m.division = 3; m.knob = 0.80f; break;
        case MoveType::Stutter:   m.division = 2; m.knob = 0.50f; break;
        case MoveType::Pitch:     m.semitones = -12; m.knob = 0.62f; break;
        case MoveType::TapeStop:  m.division = 5; m.knob = 0.50f; break;
        case MoveType::Filter:    m.knob = 0.38f; m.knob2 = 0.55f; break;
        case MoveType::DrumMoves: m.knob = 0.8f; break;
        case MoveType::Bass808:   m.knob = 0.8f; break;
        case MoveType::Hats:      m.knob = 0.7f; break;
        case MoveType::Atmos:     m.knob = 0.6f; m.knob2 = 0.45f; m.mode = 1; break;
        case MoveType::Riser:     m.division = 7; m.knob = 0.6f; break;     // 2 bars
        case MoveType::RevCrash:  m.division = 6; m.knob = 0.6f; break;     // 1 bar
        case MoveType::Pump:      m.division = 4; m.knob = 0.6f; break;     // 1/4
        case MoveType::Gap:       m.division = 4; m.knob = 1.0f; break;     // 1/4 (one beat)
        case MoveType::Impact:    m.division = 7; m.knob = 0.7f; break;     // 2-bar tail
        case MoveType::Boom:      m.division = 6; m.knob = 0.7f; break;     // 1 bar
        default: break;
    }
    return m;
}

int canonicalOrder (MoveType t)
{
    switch (t)
    {
        case MoveType::DrumMoves: return 0;
        case MoveType::Hats:      return 1;
        case MoveType::Pump:      return 2;
        case MoveType::Chops:     return 3;
        case MoveType::Stutter:   return 4;
        case MoveType::Pitch:     return 5;
        case MoveType::Filter:    return 6;
        case MoveType::Riser:     return 7;
        case MoveType::RevCrash:  return 8;
        case MoveType::Atmos:     return 9;
        case MoveType::Bass808:   return 10;
        case MoveType::Reverse:   return 11;
        case MoveType::TapeStop:  return 12;
        case MoveType::Gap:       return 13;
        case MoveType::Impact:    return 14;
        case MoveType::Boom:      return 15;
        default: break;
    }
    return 16;
}

juce::ValueTree Move::toTree() const
{
    juce::ValueTree t ("MOVE");
    t.setProperty ("type", (int) type, nullptr);
    t.setProperty ("enabled", enabled, nullptr);
    t.setProperty ("division", division, nullptr);
    t.setProperty ("semitones", semitones, nullptr);
    t.setProperty ("knob", knob, nullptr);
    t.setProperty ("knob2", knob2, nullptr);
    t.setProperty ("mode", mode, nullptr);
    t.setProperty ("seed", (int) seed, nullptr);
    if (customRegion)
    {
        t.setProperty ("regionStart", regionStart, nullptr);
        t.setProperty ("regionEnd", regionEnd, nullptr);
    }
    if (curve != 0)
    {
        t.setProperty ("curve", curve, nullptr);
        if (curve == curveCustom)
        {
            juce::StringArray pts;
            for (auto v : curvePoints) pts.add (juce::String (v, 4));
            t.setProperty ("curvePoints", pts.joinIntoString (" "), nullptr);
        }
    }
    return t;
}

Move Move::fromTree (const juce::ValueTree& t)
{
    Move m = defaultMove ((MoveType) juce::jlimit (0, numMoveTypes - 1, (int) t.getProperty ("type", 0)));
    m.enabled   = t.getProperty ("enabled", true);
    m.division  = juce::jlimit (0, (int) divisions().size() - 1, (int) t.getProperty ("division", m.division));
    m.semitones = juce::jlimit (-24, 24, (int) t.getProperty ("semitones", m.semitones));
    m.knob      = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("knob", m.knob));
    m.knob2     = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("knob2", m.knob2));
    m.mode      = juce::jlimit (0, juce::jmax (0, modeNames (m.type).size() - 1), (int) t.getProperty ("mode", m.mode));
    m.seed      = (juce::uint32) (int) t.getProperty ("seed", 1);
    m.curve = juce::jlimit (0, numCurveShapes - 1, (int) t.getProperty ("curve", 0));
    {
        auto pts = juce::StringArray::fromTokens (t.getProperty ("curvePoints").toString(), " ", "");
        pts.removeEmptyStrings();
        if (pts.size() == 16)
            for (int i = 0; i < 16; ++i) m.curvePoints[(size_t) i] = juce::jlimit (0.0f, 1.0f, pts[i].getFloatValue());
    }
    m.customRegion = t.hasProperty ("regionStart") && t.hasProperty ("regionEnd");
    if (m.customRegion)
    {
        m.regionStart = t.getProperty ("regionStart");
        m.regionEnd = t.getProperty ("regionEnd");
        if (m.regionEnd - m.regionStart < 0.0625) m.customRegion = false;
    }
    return m;
}

bool Move::operator== (const Move& o) const
{
    return type == o.type && enabled == o.enabled && division == o.division && semitones == o.semitones
        && knob == o.knob && knob2 == o.knob2 && mode == o.mode && seed == o.seed
        && customRegion == o.customRegion && (! customRegion || (regionStart == o.regionStart && regionEnd == o.regionEnd))
        && curve == o.curve && (curve != curveCustom || curvePoints == o.curvePoints);
}

juce::String keyName (int tonic, bool minor)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[((tonic % 12) + 12) % 12]) + (minor ? " minor" : " major");
}

juce::String keyShortName (int tonic, bool minor)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[((tonic % 12) + 12) % 12]) + (minor ? "m" : "");
}

double pitchClassHz (int tonic, double lowHz)
{
    double f = 440.0 * std::pow (2.0, ((((tonic % 12) + 12) % 12) - 9) / 12.0);   // in the A4 octave
    while (f >= 2.0 * lowHz) f *= 0.5;
    while (f < lowHz) f *= 2.0;
    return f;
}

juce::StringArray playbackModeNames()   { return { "PRE-DROP", "FULL CLIP", "INTO THE DROP", "ORIGINAL (A/B)" }; }
juce::StringArray exportFormatNames()   { return { "WAV (16-BIT)", "WAV (24-BIT)", "WAV (32 FLOAT)", "AIFF (24-BIT)" }; }

double DropModel::clipLengthBeats() const
{
    if (music == nullptr || music->sampleRate <= 0)
        return 0.0;
    return music->audio.getNumSamples() / samplesPerBeat (music->sampleRate);
}

juce::String formatBarPosition (double beats, const DropModel& m, bool withSixteenths)
{
    const double bpb = m.beatsPerBar();
    const double beatUnit = 4.0 / m.tsDen;
    const double abs = (m.clipStartBar - 1) * bpb + beats + 1.0e-3;   // tolerate sample rounding
    const int bar = (int) std::floor (abs / bpb) + 1;
    const double rem = abs - (bar - 1) * bpb;
    const int beat = (int) std::floor (rem / beatUnit) + 1;
    const int six = (int) std::floor ((rem - (beat - 1) * beatUnit) / 0.25) + 1;
    juce::String s = juce::String (bar) + "." + juce::String (beat);
    if (withSixteenths)
        s << "." << six;
    return s;
}

juce::String formatLength (double beats, double bpb)
{
    const double bars = beats / bpb;
    const auto isInt = [] (double v) { return std::abs (v - std::round (v)) < 1.0e-6; };
    if (isInt (bars))
    {
        const int n = (int) std::round (bars);
        return juce::String (n) + (n == 1 ? " BAR" : " BARS");
    }
    if (std::abs (bars - 0.5) < 1.0e-6) return "1/2 BAR";
    if (std::abs (bars - 0.25) < 1.0e-6) return "1/4 BAR";
    const int wholeBars = (int) std::floor (bars + 1.0e-6);
    const double restBeats = beats - wholeBars * bpb;
    juce::String s;
    if (wholeBars > 0)
        s << wholeBars << (wholeBars == 1 ? " BAR " : " BARS ");
    s << (isInt (restBeats) ? juce::String ((int) std::round (restBeats)) : juce::String (restBeats, 2))
      << (std::abs (restBeats - 1.0) < 1.0e-6 ? " BEAT" : " BEATS");
    return s;
}

} // namespace km
