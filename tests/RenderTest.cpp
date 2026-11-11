// Offline engine test: builds a synthetic 8-bar beat, runs drum detection and
// renders every move/mode, every factory preset and a batch of Rolls.
// Usage: KoolaidTests [outputDir|-] [sampleRate]   (writes WAVs when an output dir is given)

#include <set>
#include <map>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Model.h"
#include "DropRenderer.h"
#include "DrumDetector.h"
#include "Presets.h"
#include "Mastering.h"
#include "MoveProcessors.h"
#include "KeyDetector.h"

using namespace km;

static juce::AudioBuffer<float> makeBeat (double sr, double bpm, int bars, bool withPad = true)
{
    const double spb = sr * 60.0 / bpm;
    const int n = (int) (spb * 4 * bars);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (42);
    const auto snare = synthSnare (sr);
    const auto hat = synthHat (sr, false);
    auto add = [&] (const juce::AudioBuffer<float>& s, int pos, float g)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < s.getNumSamples() && pos + i < n; ++i)
                b.addSample (ch, pos + i, s.getSample (ch, i) * g);
    };
    for (int beat = 0; beat < 4 * bars; ++beat)
    {
        const int pos = (int) (beat * spb);
        if (beat % 2 == 0)   // 808 kick on 1 and 3
            for (int i = 0; i < (int) (spb * 0.9) && pos + i < n; ++i)
            {
                const double t = i / sr;
                const double f = 45.0 + 120.0 * std::exp (-t * 40.0);
                const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * f * t) * std::exp (-t * 3.0) * 0.6);
                b.addSample (0, pos + i, v);
                b.addSample (1, pos + i, v);
            }
        else
            add (snare, pos, 0.5f);
        add (hat, pos, 0.25f);
        add (hat, pos + (int) (spb / 2), 0.18f);
    }
    // a pad so there is something musical to process
    for (int i = 0; i < (withPad ? n : 0); ++i)
    {
        const double t = i / sr;
        const float v = (float) (0.05 * (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                                         + std::sin (juce::MathConstants<double>::twoPi * 277.18 * t)
                                         + std::sin (juce::MathConstants<double>::twoPi * 329.63 * t)));
        b.addSample (0, i, v);
        b.addSample (1, i, v * 0.9f);
    }
    return b;
}

static juce::AudioBuffer<float> full (const RenderResult& r) { return r.copyOf (RenderResult::Layer::mix, 0, r.length); }

static double rmsDb (const juce::AudioBuffer<float>& b, int start, int len)
{
    double s = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = start; i < start + len; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10.0 * std::log10 (s / juce::jmax (1, len * b.getNumChannels()) + 1.0e-12);
}

/** Largest per-beat level jump (dB) of the rendered zone over the original audio. */
static double maxBeatJump (const RenderResult& r, const juce::AudioBuffer<float>& orig)
{
    const int beat = (int) r.samplesPerBeat;
    const auto mix = full (r);
    double worst = -100.0;
    for (auto s = r.zoneStart; s + beat <= r.zoneEnd; s += beat)
    {
        const double ref = juce::jmax (rmsDb (orig, (int) s, beat), rmsDb (orig, (int) r.zoneStart, (int) (r.zoneEnd - r.zoneStart)));
        worst = juce::jmax (worst, rmsDb (mix, (int) s, beat) - ref);
    }
    return worst;
}

/** A chord progression (triads with a few harmonics + root bass) over the drum beat. */
static juce::AudioBuffer<float> makeSong (double sr, double bpm, const std::vector<std::vector<int>>& chords)
{
    const double spb = sr * 60.0 / bpm;
    const int bars = (int) chords.size();
    auto b = makeBeat (sr, bpm, bars, false);
    const int n = b.getNumSamples();
    const auto hz = [] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); };
    for (int bar = 0; bar < bars; ++bar)
    {
        const int s0 = (int) (bar * 4 * spb), s1 = juce::jmin (n, (int) ((bar + 1) * 4 * spb));
        for (int note : chords[(size_t) bar])
            for (int h = 1; h <= 4; ++h)
            {
                const double f = hz (note) * h;
                for (int i = s0; i < s1; ++i)
                {
                    const double t = (i - s0) / sr;
                    const float v = (float) (0.05 / h * std::sin (juce::MathConstants<double>::twoPi * f * t) * (1.0 - std::exp (-t * 30.0)));
                    b.addSample (0, i, v);
                    b.addSample (1, i, v);
                }
            }
        // bass: the chord root two octaves down
        const double fb = hz (chords[(size_t) bar][0] - 24);
        for (int i = s0; i < s1; ++i)
        {
            const float v = (float) (0.12 * std::sin (juce::MathConstants<double>::twoPi * fb * (i - s0) / sr));
            b.addSample (0, i, v);
            b.addSample (1, i, v);
        }
    }
    return b;
}

static bool checkBuffer (const juce::AudioBuffer<float>& b, const juce::String& what)
{
    float peak = 0;
    int where = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v)) { std::cout << "FAIL non-finite sample in " << what << "\n"; return false; }
            if (std::abs (v) > peak) { peak = std::abs (v); where = i; }
        }
    if (peak > 1.0001f) { std::cout << "FAIL peak " << peak << " at sample " << where << " of " << b.getNumSamples() << " in " << what << "\n"; return false; }
    return true;
}

static void writeWav (const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& b, double sr)
{
    if (dir == juce::File()) return;
    dir.createDirectory();
    auto f = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    const juce::File outDir = argc > 1 && juce::String (argv[1]) != "-" ? juce::File (argv[1]) : juce::File();
    const double sr = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 48000.0, bpm = 130.0;
    std::cout << "sample rate " << sr << "\n";
    int failures = 0;

    auto clip = std::make_shared<ClipData>();
    clip->audio = makeBeat (sr, bpm, 8);
    clip->sampleRate = sr;
    clip->name = "test beat";

    DropModel model;
    model.music = clip;
    model.bpm = bpm;
    model.zoneStart = 16.0;
    model.zoneEnd = 28.0;   // drop at bar 8 so there is audio after it (pre-verse)

    // ---- detection
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto analysis = std::make_shared<DrumAnalysis> (analyseDrums (clip->audio, sr, model.samplesPerBeat (sr), 4.0, 0.55f, 0.55f, 0.55f));
    std::cout << "analysis: " << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << " ms\n"
              << "  snare found=" << analysis->snare.found << " hits=" << analysis->snare.onsets.size() << " conf=" << analysis->snare.confidence << "\n"
              << "  808   found=" << analysis->bass.found << " hits=" << analysis->bass.onsets.size() << " conf=" << analysis->bass.confidence
              << " pitch=" << analysis->bass.pitchHz << "Hz\n"
              << "  hats  found=" << analysis->hats.found << " hits=" << analysis->hats.onsets.size() << " conf=" << analysis->hats.confidence << "\n";
    if (! analysis->snare.found || ! analysis->bass.found || ! analysis->hats.found)
    {
        std::cout << "FAIL: drum detection missed something on the synthetic beat\n";
        ++failures;
    }
    // snare hits should sit on beats 2 and 4
    {
        const double spb = model.samplesPerBeat (sr);
        int good = 0;
        for (auto s : analysis->snare.onsets)
        {
            const double beat = s / spb;
            const int nearest = (int) std::round (beat);
            if (std::abs (beat - nearest) < 0.1 && nearest % 2 == 1) ++good;
        }
        std::cout << "  snare onsets on backbeats: " << good << "/" << analysis->snare.onsets.size() << "\n";
        if (good < (int) analysis->snare.onsets.size() * 3 / 4) { std::cout << "FAIL: snare onsets not on backbeats\n"; ++failures; }
    }

    // ---- every move, every mode
    for (int t = 0; t < numMoveTypes; ++t)
    {
        const auto modes = modeNames ((MoveType) t);
        for (int mode = 0; mode < modes.size(); ++mode)
        {
            DropModel m = model;
            Move mv = defaultMove ((MoveType) t);
            mv.mode = mode;
            m.moves = { mv };
            const auto s = juce::Time::getMillisecondCounterHiRes();
            auto r = renderDrop (m, analysis, 1);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - s;
            const juce::String name = mv.stackLabel() + " [" + modes[mode] + "]";
            if (! checkBuffer (full (*r), name)) ++failures;
            const bool after = playsAfterDrop (mv.type);
            const double barSamples = 4.0 * m.samplesPerBeat (sr);
            const double bars = (int) r->length / barSamples;
            const bool lengthOk = after ? ((int) r->length >= clip->audio.getNumSamples() && std::abs (bars - std::round (bars)) < 1.0e-3)
                                        : (int) r->length == clip->audio.getNumSamples();
            if (! lengthOk) { std::cout << "FAIL length " << name << " (" << bars << " bars)\n"; ++failures; }
            // the zone (or, for Impact / Sub Boom, the audio after the drop) must actually change
            double diff = 0;
            const int d0 = after ? (int) r->zoneEnd + 64 : (int) r->zoneStart;
            const int d1 = after ? (int) r->length : (int) r->zoneEnd;
            for (int i = d0; i < d1; ++i)
                diff += std::abs (r->mixAt (0, i) - (i < clip->audio.getNumSamples() ? clip->audio.getSample (0, i) : 0.0f));
            // and audio outside the zone must be untouched (apart from the drop micro-fade)
            double outside = 0;
            for (int i = 0; i < (int) r->zoneStart; ++i)
                outside += std::abs (r->mixAt (0, i) - clip->audio.getSample (0, i));
            const double jump = maxBeatJump (*r, clip->audio);
            std::cout << "  " << name.paddedRight (' ', 44) << juce::String (ms, 1).paddedLeft (' ', 7) << " ms   zone diff "
                      << juce::String (diff / juce::jmax (1, d1 - d0), 4) << "   max beat jump " << juce::String (jump, 1) << " dB\n";
            if (jump > 6.0) { std::cout << "FAIL: " << name << " is " << juce::String (jump, 1) << " dB louder than the track\n"; ++failures; }
            const float tp = juce::Decibels::gainToDecibels (measureTruePeak (full (*r), (int) r->zoneStart, (int) (r->zoneEnd - r->zoneStart), sr));
            // the ceiling is -1 dBTP, or the original's own peak where it was already hotter
            const float origTp = juce::Decibels::gainToDecibels (measureTruePeak (clip->audio, (int) r->zoneStart, (int) (r->zoneEnd - r->zoneStart), sr));
            if (tp > juce::jmax (r->ceilingDb, origTp) + 0.3f) { std::cout << "FAIL: " << name << " true peak " << tp << " dBTP over ceiling " << r->ceilingDb << "\n"; ++failures; }
            if (diff < 1.0e-3) { std::cout << "FAIL: " << name << " did nothing\n"; ++failures; }
            if (outside > 1.0e-6) { std::cout << "FAIL: " << name << " touched audio before the zone\n"; ++failures; }
            writeWav (outDir, "move - " + name, full (*r), sr);
        }
    }

    // ---- presets
    for (auto& p : factoryPresets())
    {
        DropModel m = model;
        m.moves = p.moves;
        const auto s = juce::Time::getMillisecondCounterHiRes();
        auto r = renderDrop (m, analysis, 1);
        std::cout << "preset " << p.name.paddedRight (' ', 20) << juce::String (juce::Time::getMillisecondCounterHiRes() - s, 1) << " ms\n";
        if (! checkBuffer (full (*r), p.name)) ++failures;
        if (maxBeatJump (*r, clip->audio) > 6.0)
        {
            std::cout << "FAIL: preset " << p.name << " jumps " << juce::String (maxBeatJump (*r, clip->audio), 1) << " dB\n";
            ++failures;
        }
        if (! presetGenres().contains (p.genre)) { std::cout << "FAIL: preset " << p.name << " has unknown genre " << p.genre << "\n"; ++failures; }
        writeWav (outDir, "preset - " + p.name, full (*r), sr);
    }
    {
        int perGenre[16] = {};
        juce::StringArray names;
        for (auto& p : factoryPresets())
        {
            ++perGenre[juce::jmax (0, presetGenres().indexOf (p.genre))];
            if (names.contains (p.name)) { std::cout << "FAIL: duplicate preset name " << p.name << "\n"; ++failures; }
            names.add (p.name);
        }
        juce::String counts;
        for (int g = 0; g < presetGenres().size(); ++g)
        {
            counts << presetGenres()[g] << " " << perGenre[g] << "  ";
            if (perGenre[g] < 5) { std::cout << "FAIL: genre " << presetGenres()[g] << " has only " << perGenre[g] << " presets\n"; ++failures; }
        }
        std::cout << "presets by genre: " << counts << "\n";
    }

    // ---- ROLL styles: every style makes valid, distinct-feeling stacks
    {
        juce::Random srng (21);
        for (int style = 1; style < rollStyleNames().size(); ++style)
        {
            std::map<int, int> typeCount;
            int rolls = 0;
            for (int k = 0; k < 10; ++k)
            {
                DropModel m = model;
                m.moves = rollMoves (srng, true, style);
                ++rolls;
                std::set<int> seen;
                for (auto& mv : m.moves)
                {
                    if (! seen.insert ((int) mv.type).second)
                    { std::cout << "FAIL: " << rollStyleNames()[style] << " roll has two " << typeName (mv.type) << " moves\n"; ++failures; }
                    ++typeCount[(int) mv.type];
                }
                if (m.moves.size() < 2) { std::cout << "FAIL: " << rollStyleNames()[style] << " roll is nearly empty\n"; ++failures; }
                auto r = renderDrop (m, analysis, 1);
                if (! checkBuffer (full (*r), rollStyleNames()[style] + " roll")) ++failures;
                const double jump = maxBeatJump (*r, clip->audio);
                if (jump > 6.0) { std::cout << "FAIL: " << rollStyleNames()[style] << " roll jumps " << juce::String (jump, 1) << " dB\n"; ++failures; }
            }
            for (int k = 0; k < 300; ++k)   // more rolls (not rendered) for the stats
            {
                for (auto& mv : rollMoves (srng, true, style)) ++typeCount[(int) mv.type];
                ++rolls;
            }
            juce::String top;
            std::vector<std::pair<int, int>> sorted (typeCount.begin(), typeCount.end());
            std::sort (sorted.begin(), sorted.end(), [] (auto& x, auto& y) { return x.second > y.second; });
            for (size_t i = 0; i < juce::jmin<size_t> (5, sorted.size()); ++i)
                top << typeName ((MoveType) sorted[i].first) << " " << juce::roundToInt (100.0 * sorted[i].second / rolls) << "%  ";
            std::cout << "roll style " << rollStyleNames()[style].paddedRight (' ', 8) << top << "\n";
            // each style's signature move shows up often
            const std::pair<int, MoveType> signature[] = { { 1, MoveType::Bass808 }, { 2, MoveType::Hats }, { 3, MoveType::Filter }, { 4, MoveType::Riser },
                                                           { 5, MoveType::Pump }, { 6, MoveType::Boom }, { 7, MoveType::Atmos }, { 8, MoveType::Atmos } };
            for (auto& [st, t] : signature)
                if (st == style && typeCount[(int) t] < rolls * 3 / 10)
                {
                    std::cout << "FAIL: " << rollStyleNames()[style] << " rolls rarely use " << typeName (t) << "\n";
                    ++failures;
                }
        }
    }

    // ---- stems (music + fx) add up exactly to the mix, limiter included
    {
        int bad = 0;
        juce::Random srng (11);
        for (int k = 0; k < 12; ++k)
        {
            DropModel m = model;
            m.moves = rollMoves (srng, true);
            m.moves.push_back (defaultMove (MoveType::Impact));
            auto r = renderDrop (m, analysis, 1);
            float worst = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (juce::int64 p = 0; p < r->length; p += 7)
                    worst = juce::jmax (worst, std::abs (r->sampleAt (RenderResult::Layer::music, ch, p)
                                                         + r->sampleAt (RenderResult::Layer::fx, ch, p) - r->mixAt (ch, p)));
            if (worst > 1.0e-5f) { ++bad; std::cout << "  stems off by " << worst << " in render " << k << "\n"; }
        }
        std::cout << "stems null against the mix: " << (12 - bad) << "/12 renders\n";
        if (bad > 0) { std::cout << "FAIL: stems don't add up to the mix\n"; ++failures; }
    }

    // ---- rolls
    juce::Random rng (7);
    for (int i = 0; i < 30; ++i)
    {
        DropModel m = model;
        m.moves = rollMoves (rng, true);
        juce::String desc;
        for (auto& mv : m.moves) desc << mv.stackLabel() << " | ";
        auto r = renderDrop (m, analysis, 1);
        if (! checkBuffer (full (*r), "roll " + desc)) ++failures;
        if (maxBeatJump (*r, clip->audio) > 6.0)
        {
            std::cout << "FAIL: roll " << desc << " jumps " << juce::String (maxBeatJump (*r, clip->audio), 1) << " dB\n";
            ++failures;
        }
        if (i < 5)
        {
            std::cout << "roll " << i << ": " << desc << "\n";
            writeWav (outDir, "roll " + juce::String (i), full (*r), sr);
        }
    }

    // ---- timeline: a timed move only touches its own slice of the build
    {
        const double spb = model.samplesPerBeat (sr);
        DropModel m = model;                 // zone = beats 16..28 (drop at 28)
        Move t = defaultMove (MoveType::TapeStop);
        t.customRegion = true;
        t.regionStart = -8.0;                // beats 20..24
        t.regionEnd = -4.0;
        m.moves = { t };
        auto r = renderDrop (m, analysis, 1);
        auto diffIn = [&] (double b0, double b1)
        {
            double d = 0.0;
            for (int i = (int) (b0 * spb); i < (int) (b1 * spb); ++i)
                d += std::abs (r->mixAt (0, i) - clip->audio.getSample (0, i));
            return d / juce::jmax (1.0, (b1 - b0) * spb);
        };
        const double before = diffIn (16.0, 19.95), inside = diffIn (20.5, 23.5), after = diffIn (24.05, 27.5);
        std::cout << "timeline: tape stop timed to beats 20-24 -> change before " << before << ", inside " << inside << ", after " << after << "\n";
        if (before > 1.0e-6 || after > 1.0e-6 || inside < 1.0e-3) { std::cout << "FAIL: timed move leaked outside its slice\n"; ++failures; }

        // natural regions: a Tape Stop's default spot is the last 1/2 bar
        const auto nat = moveRegion (defaultMove (MoveType::TapeStop), 12.0, 4.0);
        if (std::abs (nat.first + 2.0) > 1.0e-9 || std::abs (nat.second) > 1.0e-9) { std::cout << "FAIL: natural region\n"; ++failures; }

        // a timed Impact lands where it was put (2 beats after the drop)
        Move im = defaultMove (MoveType::Impact);
        im.customRegion = true;
        im.regionStart = 2.0;
        im.regionEnd = 6.0;
        m.moves = { im };
        r = renderDrop (m, analysis, 1);
        const double early = diffIn (28.1, 29.9), late = diffIn (30.05, 31.5);
        std::cout << "timeline: impact timed to +2 beats -> change before it " << early << ", after it " << late << "\n";
        if (early > 1.0e-6 || late < 1.0e-3) { std::cout << "FAIL: timed impact\n"; ++failures; }

        // round trip of the timing through presets / project state
        auto back = Move::fromTree (t.toTree());
        if (! (back == t)) { std::cout << "FAIL: timeline region round trip\n"; ++failures; }
    }

    // ---- Sidechain Pump "Follow Kick" ducks on the detected kicks (beats 1 and 3 here), not on every beat
    {
        const double spb = model.samplesPerBeat (sr);
        auto duckAt = [&] (const RenderResult& r, double beat)
        {
            const int a = (int) ((beat + 0.03) * spb), len = (int) (0.12 * spb);
            double o = 0, in = 0;
            for (int i = a; i < a + len; ++i) { o += std::abs (r.mixAt (0, i)); in += std::abs (clip->audio.getSample (0, i)); }
            return o / juce::jmax (1.0e-9, in);
        };
        auto pumpOn = [&] (int mode)
        {
            DropModel m = model;
            Move p = defaultMove (MoveType::Pump);
            p.mode = mode; p.knob = 0.8f;
            m.moves = { p };
            return renderDrop (m, analysis, 1);
        };
        auto kick = pumpOn (3), grid = pumpOn (0);
        const double kOnKick = duckAt (*kick, 20.0), kOffKick = duckAt (*kick, 21.0);
        const double gOnKick = duckAt (*grid, 20.0), gOffKick = duckAt (*grid, 21.0);
        std::cout << "pump follow-kick: level after kick " << juce::String (kOnKick, 2) << ", after snare " << juce::String (kOffKick, 2)
                  << "  (grid pump: " << juce::String (gOnKick, 2) << " / " << juce::String (gOffKick, 2) << ")\n";
        if (! (kOnKick < 0.6 && kOffKick > 0.85 && gOffKick < 0.6)) { std::cout << "FAIL: follow-kick pump\n"; ++failures; }
    }

    // ---- Tension macro: a linked move fades in along the tension curve; Tension 0 = without it
    {
        const double spb = model.samplesPerBeat (sr);
        auto change = [&] (const RenderResult& r, double b0, double b1, float tension)
        {
            double d = 0;
            for (auto p = (juce::int64) (b0 * spb); p < (juce::int64) (b1 * spb); ++p)
                d += std::abs (r.tensionAt (RenderResult::Layer::mix, 0, p, tension) - clip->audio.getSample (0, (int) p));
            return d / ((b1 - b0) * spb);
        };
        DropModel m = model;   // zone: beats 16..28
        Move ch = defaultMove (MoveType::Chops);
        ch.knob = 1.0f;
        m.moves = { ch };
        auto plain = renderDrop (m, analysis, 1);
        m.moves[0].tensionLink = true;
        m.tension.curve = curveLinear;
        auto linked = renderDrop (m, analysis, 1);
        const double pStart = change (*plain, 16.0, 17.0, 1.0f), pEnd = change (*plain, 27.0, 28.0, 1.0f);
        const double lStart = change (*linked, 16.0, 17.0, 1.0f), lEnd = change (*linked, 27.0, 28.0, 1.0f);
        double lZero = 0;   // at Tension 0 the output is exactly the render without the linked move
        for (auto p = (juce::int64) (16.0 * spb); p < (juce::int64) (28.0 * spb); ++p)
            lZero += std::abs (linked->tensionAt (RenderResult::Layer::mix, 0, p, 0.0f) - linked->base->mixAt (0, p));
        std::cout << "tension: chops change start/end unlinked " << juce::String (pStart, 4) << "/" << juce::String (pEnd, 4)
                  << ", linked " << juce::String (lStart, 4) << "/" << juce::String (lEnd, 4) << ", linked at Tension 0: " << lZero << "\n";
        if (linked->base == nullptr || plain->base != nullptr) { std::cout << "FAIL: tension base render\n"; ++failures; }
        if (! (lStart < pStart * 0.2 && lEnd > pEnd * 0.8 && lZero < 1.0e-6)) { std::cout << "FAIL: tension macro\n"; ++failures; }
        Move t = ch; t.tensionLink = true;
        if (! (Move::fromTree (t.toTree()) == t)) { std::cout << "FAIL: tension link round trip\n"; ++failures; }
    }

    // ---- curve shapes: a riser with "Late Rush" keeps its energy for the very end
    {
        auto riserWith = [&] (int curve)
        {
            DropModel m = model;
            Move rz = defaultMove (MoveType::Riser);
            rz.curve = curve;
            m.moves = { rz };
            auto r = renderDrop (m, analysis, 1);
            const auto len = (r->zoneEnd - r->zoneStart);
            const auto start = r->zoneEnd - (juce::int64) (len / 1.5);   // the riser's 2 bars ~ last part of the zone
            double first = 0, second = 0;
            const auto mid = (start + r->zoneEnd) / 2;
            for (auto p = start; p < r->zoneEnd; ++p)
            {
                const double v = r->sampleAt (RenderResult::Layer::fx, 0, p);
                (p < mid ? first : second) += v * v;
            }
            return first / juce::jmax (1.0e-12, second);
        };
        const double linear = riserWith (curveLinear), late = riserWith (curveLate), easeOut = riserWith (curveEaseOut);
        std::cout << "curves: riser energy first-half/second-half: linear " << juce::String (linear, 3)
                  << ", late rush " << juce::String (late, 3) << ", ease out " << juce::String (easeOut, 3) << "\n";
        if (! (late < linear * 0.3 && easeOut > linear)) { std::cout << "FAIL: curve shapes don't change the ramp\n"; ++failures; }

        Move cm = defaultMove (MoveType::Filter);
        cm.curve = curveCustom;
        for (int i = 0; i < 16; ++i) cm.curvePoints[(size_t) i] = (float) (i % 2);
        const auto back = Move::fromTree (cm.toTree());
        if (! (back == cm) || std::abs (shapeCurve (back, 1.0 / 15.0, 1.0) - 1.0) > 1.0e-4)
        { std::cout << "FAIL: custom curve round trip\n"; ++failures; }
        if (std::abs (shapeCurve (cm, 0.5 / 15.0, 1.0) - 0.5) > 1.0e-3) { std::cout << "FAIL: custom curve interpolation\n"; ++failures; }
    }

    // ---- key detection on chord progressions in known keys
    {
        struct Case { const char* name; int tonic; bool minor; std::vector<std::vector<int>> chords; };
        const std::vector<Case> cases {
            { "A minor (Am F C G)",    9, true,  { { 69, 72, 76 }, { 65, 69, 72 }, { 60, 64, 67 }, { 67, 71, 74 }, { 69, 72, 76 }, { 65, 69, 72 }, { 64, 68, 71 }, { 69, 72, 76 } } },
            { "D major (D G A D)",     2, false, { { 62, 66, 69 }, { 67, 71, 74 }, { 69, 73, 76 }, { 62, 66, 69 }, { 71, 74, 78 }, { 67, 71, 74 }, { 69, 73, 76 }, { 62, 66, 69 } } },
            { "F# minor (F#m D A E)",  6, true,  { { 66, 69, 73 }, { 62, 66, 69 }, { 69, 73, 76 }, { 64, 68, 71 }, { 66, 69, 73 }, { 62, 66, 69 }, { 61, 65, 68 }, { 66, 69, 73 } } },
            { "Eb major (Eb Ab Bb Eb)", 3, false, { { 63, 67, 70 }, { 68, 72, 75 }, { 70, 74, 77 }, { 63, 67, 70 }, { 60, 63, 67 }, { 68, 72, 75 }, { 70, 74, 77 }, { 63, 67, 70 } } },
        };
        int right = 0;
        for (auto& c : cases)
        {
            const auto song = makeSong (sr, bpm, c.chords);
            const auto k = detectKey (song, sr);
            const bool ok = k.found && k.tonic == c.tonic && k.minor == c.minor;
            right += ok ? 1 : 0;
            std::cout << "key: " << juce::String (c.name).paddedRight (' ', 24) << " -> "
                      << (k.found ? keyName (k.tonic, k.minor) : juce::String ("not found")) << "  conf " << juce::String (k.confidence, 2)
                      << (ok ? "" : "   <-- WRONG") << "\n";
        }
        if (right < (int) cases.size()) { std::cout << "FAIL: key detection " << right << "/" << cases.size() << "\n"; ++failures; }

        // the Sub Boom lands on the root: A minor -> A1 (55 Hz)
        DropModel m = model;
        m.keyMode = 12 + 9;   // A minor, set by hand
        Move boom = defaultMove (MoveType::Boom);
        boom.division = 7;     // 2 bars, so the glide has settled
        m.moves = { boom };
        auto r = renderDrop (m, analysis, 1);
        const juce::int64 a0 = r->zoneEnd + (juce::int64) (1.5 * sr), a1 = r->zoneEnd + (juce::int64) (2.3 * sr);
        int crossings = 0;
        float prev = r->sampleAt (RenderResult::Layer::fx, 0, a0);
        for (juce::int64 p = a0 + 1; p < a1; ++p)
        {
            const float v = r->sampleAt (RenderResult::Layer::fx, 0, p);
            if ((prev < 0.0f) != (v < 0.0f)) ++crossings;
            prev = v;
        }
        const double f = crossings / 2.0 / ((a1 - a0) / sr);
        std::cout << "sub boom in A minor ends at " << juce::String (f, 1) << " Hz (A1 = 55 Hz)\n";
        if (std::abs (f - 55.0) > 2.0) { std::cout << "FAIL: sub boom not tuned to the key\n"; ++failures; }
    }

    // ---- FX moves on white noise (for listening / analysis of the sweeps)
    {
        auto noise = std::make_shared<ClipData>();
        noise->sampleRate = sr;
        noise->audio.setSize (2, (int) (sr * 60.0 / bpm * 16));
        juce::Random nr (5);
        for (int i = 0; i < noise->audio.getNumSamples(); ++i)
        {
            const float v = 0.2f * (nr.nextFloat() * 2.0f - 1.0f);
            noise->audio.setSample (0, i, v);
            noise->audio.setSample (1, i, v);
        }
        writeWav (outDir, "noise-dry", noise->audio, sr);
        for (int mode = 0; mode < 3; ++mode)
        {
            DropModel m = model;
            m.music = noise;
            m.zoneStart = 0.0;
            m.zoneEnd = 16.0;
            Move ph = defaultMove (MoveType::Phaser);
            ph.mode = mode;
            ph.knob = 1.0f;
            m.moves = { ph };
            auto r = renderDrop (m, nullptr, 1);
            if (! checkBuffer (full (*r), "phaser on noise")) ++failures;
            writeWav (outDir, "noise-phaser-" + juce::String (mode), full (*r), sr);
        }
    }

    // ---- your own samples: a riser ends on the drop, a crash is reversed into it, an impact hits on it
    {
        const double spbS = model.samplesPerBeat (sr);
        auto smp = std::make_shared<ClipData>();
        smp->sampleRate = sr;
        const int sl = (int) (0.8 * sr), lead = (int) (0.05 * sr);
        smp->audio.setSize (2, lead + sl + (int) (0.1 * sr));   // 50 ms of silence, the sound, 100 ms of silence
        smp->audio.clear();
        for (int i = 0; i < sl; ++i)
        {
            // a decaying 1 kHz ping: loud attack, quiet tail
            const float v = 0.5f * (float) (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sr) * std::exp (-3.0 * i / sl));
            smp->audio.setSample (0, lead + i, v);
            smp->audio.setSample (1, lead + i, v);
        }
        auto envAt = [&] (const RenderResult& r, juce::int64 p0, int len)
        {
            double acc = 0.0;
            for (int i = 0; i < len; ++i) { const float v = r.sampleAt (RenderResult::Layer::fx, 0, p0 + i); acc += (double) v * v; }
            return std::sqrt (acc / len);
        };
        const int w = (int) (0.02 * sr);
        for (auto t : { MoveType::Riser, MoveType::RevCrash, MoveType::Impact, MoveType::Boom })
        {
            DropModel m = model;
            m.samples["test"] = smp;
            Move mv = defaultMove (t);
            mv.sampleId = "test";
            mv.sampleName = "ping";
            m.moves = { mv };
            auto r = renderDrop (m, analysis, 1);
            if (! checkBuffer (full (*r), juce::String ("sample ") + typeName (t))) ++failures;
            const auto drop = r->zoneEnd;
            // the loud end of the ping must sit right before the drop (crash) / right after it (impact)
            const double before = envAt (*r, drop - w, w), after = envAt (*r, drop, w);
            const double early = envAt (*r, drop - sl + w, w), late = envAt (*r, drop + sl - 2 * w, w);
            juce::String what;
            bool ok = true;
            if (t == MoveType::Riser)        { ok = before > 0.0 && early > before * 3.0; what = "riser sample (as is) ends on the drop"; }   // the decaying tail ends at the drop
            else if (t == MoveType::RevCrash) { ok = before > early * 3.0 && after < before * 0.05; what = "crash sample reversed into the drop"; }
            else                              { ok = after > late * 3.0 && before < after * 0.05; what = juce::String (typeName (t)) + " sample hits on the drop"; }
            // and the attack must be on time: the first loud sample after the drop within 1 ms (impact / boom)
            if (t == MoveType::Impact || t == MoveType::Boom)
            {
                juce::int64 first = -1;
                const float pk = (float) after * 1.2f;
                for (juce::int64 p = drop; p < drop + (juce::int64) (0.05 * sr) && first < 0; ++p)
                    if (std::abs (r->sampleAt (RenderResult::Layer::fx, 0, p)) > pk * 0.3f) first = p;
                ok &= first >= 0 && first - drop < (juce::int64) (0.001 * sr) + 2;
            }
            std::cout << what << ": " << (ok ? "ok" : "WRONG") << "  (before " << juce::String (before, 4) << ", after " << juce::String (after, 4)
                      << ", early " << juce::String (early, 4) << ", late " << juce::String (late, 4) << ")\n";
            if (! ok) { std::cout << "FAIL: " << what << "\n"; ++failures; }
            if (maxBeatJump (*r, clip->audio) > 6.0) { std::cout << "FAIL: sample " << typeName (t) << " level jump\n"; ++failures; }
            juce::ignoreUnused (spbS);
        }
    }

    // ---- step patterns: on steps chop / stutter, off steps follow the pattern exactly
    {
        auto tone = std::make_shared<ClipData>();
        tone->sampleRate = sr;
        tone->audio.setSize (2, (int) (sr * 60.0 / bpm * 16));
        for (int i = 0; i < tone->audio.getNumSamples(); ++i)
        {
            // a tone that glides slowly, so a repeated slice never matches the original
            const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * (300.0 + 40.0 * i / sr) * i / sr);
            tone->audio.setSample (0, i, v);
            tone->audio.setSample (1, i, v);
        }
        const double spbT = 60.0 / bpm * sr;
        const int step = (int) std::llround (spbT * 0.25);   // 1/16
        auto stepRms = [&] (const RenderResult& r, const juce::AudioBuffer<float>* ref, int k)
        {
            // RMS of step k (from the clip start), or of its difference to 'ref'; skips the ramps
            const int a0 = k * step + step / 8, a1 = (k + 1) * step - step / 8;
            double acc = 0.0;
            for (int i = a0; i < a1; ++i)
            {
                const float v = r.mixAt (0, i) - (ref != nullptr ? ref->getSample (0, i) : 0.0f);
                acc += (double) v * v;
            }
            return std::sqrt (acc / (a1 - a0));
        };
        const juce::uint32 pattern = 0x4949u;   // tresillo: steps 1, 4, 7, 9, 12, 15
        auto on = [pattern] (int k) { return ((pattern >> (k % 16)) & 1u) != 0; };

        {   // Chops gate, full depth: off steps silent, on steps at full level
            DropModel m = model;
            m.music = tone; m.zoneStart = 0.0; m.zoneEnd = 16.0;
            Move ch = defaultMove (MoveType::Chops);
            ch.mode = 0; ch.division = 2; ch.knob = 1.0f; ch.patternOn = true; ch.pattern = pattern;
            m.moves = { ch };
            auto r = renderDrop (m, nullptr, 1);
            double worstOff = 0.0, weakestOn = 1.0;
            for (int k = 4; k < 60; ++k)
            {
                const double v = stepRms (*r, nullptr, k);
                if (on (k)) weakestOn = juce::jmin (weakestOn, v); else worstOff = juce::jmax (worstOff, v);
            }
            std::cout << "chops step pattern: on steps >= " << juce::String (weakestOn, 3) << ", off steps <= " << juce::String (worstOff, 4) << "\n";
            if (worstOff > 0.005 || weakestOn < 0.15) { std::cout << "FAIL: chops step pattern\n"; ++failures; }
            writeWav (outDir, "pattern-chops", full (*r), sr);
        }
        {   // Stutter: off steps are the original audio, on steps are repeats
            DropModel m = model;
            m.music = tone; m.zoneStart = 0.0; m.zoneEnd = 16.0;
            Move st = defaultMove (MoveType::Stutter);
            st.mode = 0; st.division = 2; st.knob = 0.75f; st.patternOn = true; st.pattern = pattern;   // 2 bars of stutter
            m.moves = { st };
            auto r = renderDrop (m, nullptr, 1);
            double worstOff = 0.0, leastOn = 1.0;
            const int firstStep = (int) ((16.0 - stutterSpanBeats (st.knob, 4.0)) * 4.0);
            for (int k = firstStep + 1; k < 63; ++k)
            {
                const double v = stepRms (*r, &tone->audio, k);
                if (on (k)) leastOn = juce::jmin (leastOn, v); else worstOff = juce::jmax (worstOff, v);
            }
            std::cout << "stutter step pattern: on steps differ by >= " << juce::String (leastOn, 3) << ", off steps by <= " << juce::String (worstOff, 4) << "\n";
            if (worstOff > 0.005 || leastOn < 0.02) { std::cout << "FAIL: stutter step pattern\n"; ++failures; }
            writeWav (outDir, "pattern-stutter", full (*r), sr);
        }
        {   // Shuffle: off steps untouched
            DropModel m = model;
            m.music = tone; m.zoneStart = 0.0; m.zoneEnd = 16.0;
            Move ch = defaultMove (MoveType::Chops);
            ch.mode = 1; ch.division = 2; ch.patternOn = true; ch.pattern = pattern;
            m.moves = { ch };
            auto r = renderDrop (m, nullptr, 1);
            double worstOff = 0.0;
            for (int k = 4; k < 63; ++k)
                if (! on (k)) worstOff = juce::jmax (worstOff, stepRms (*r, &tone->audio, k));
            if (worstOff > 0.005) { std::cout << "FAIL: shuffle pattern touched an off step (" << worstOff << ")\n"; ++failures; }
        }
    }

    // ---- studio-grade pitch: a pure 440 Hz tone shifted +12 st must come out as a clean 880 Hz
    {
        auto tone = std::make_shared<ClipData>();
        tone->sampleRate = sr;
        tone->audio.setSize (2, (int) (sr * 60.0 / bpm * 16));
        for (int i = 0; i < tone->audio.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr);
            tone->audio.setSample (0, i, v);
            tone->audio.setSample (1, i, v);
        }
        DropModel m = model;
        m.music = tone;
        m.zoneStart = 0.0;
        m.zoneEnd = 16.0;
        Move p = defaultMove (MoveType::Pitch);
        p.mode = 1; p.semitones = 12;
        m.moves = { p };
        auto r = renderDrop (m, nullptr, 1);
        writeWav (outDir, "pitch-tone-plus12", full (*r), sr);
        const int a0 = (int) r->length / 3, len = (int) sr;   // a second from the middle
        auto goertzel = [&] (double f)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, cw = 2.0 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (int i = 0; i < len; ++i) { const double s0 = r->mixAt (0, a0 + i) + cw * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - cw * s1 * s2;
        };
        double total = 0;
        for (int i = 0; i < len; ++i) total += (double) r->mixAt (0, a0 + i) * r->mixAt (0, a0 + i);
        // energy in the 870-890 Hz band (1 Hz bins over a 1 s window)
        double at880 = 0.0;
        for (int f = 870; f <= 890; ++f) at880 += goertzel ((double) f) * 2.0 / len;
        const double at440 = goertzel (440.0) * 2.0 / len;
        const double purity = at880 / juce::jmax (1.0e-12, total);
        std::cout << "pitch +12 on a 440 Hz tone: energy at 880 Hz (+-10 Hz) " << juce::String (purity * 100.0, 1) << "%, left at 440 Hz "
                  << juce::String (at440 / juce::jmax (1.0e-12, total) * 100.0, 2) << "%\n";
        if (purity < 0.9) { std::cout << "FAIL: pitch shift is not clean\n"; ++failures; }
    }

    // ---- 16-bit export dither: values land exactly on 16-bit steps and quiet audio survives
    {
        juce::AudioBuffer<float> q (2, 48000);
        for (int i = 0; i < 48000; ++i)
            for (int ch = 0; ch < 2; ++ch)
                q.setSample (ch, i, 0.00005f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0));
        ditherTo16Bit (q);
        bool onGrid = true, nonSilent = false;
        for (int i = 0; i < 48000; ++i)
        {
            const float v = q.getSample (0, i) * 32767.0f;
            onGrid &= std::abs (v - std::round (v)) < 1.0e-3f;
            nonSilent |= v != 0.0f;
        }
        std::cout << "dither: on 16-bit grid " << (int) onGrid << ", -86 dB tone preserved " << (int) nonSilent << "\n";
        if (! onGrid || ! nonSilent) { std::cout << "FAIL: dither\n"; ++failures; }
    }

    // ---- state round trip of moves
    {
        auto moves = factoryPresets()[3].moves;
        Move pat = defaultMove (MoveType::Chops);
        pat.patternOn = true;
        pat.pattern = 0x4949u;
        moves.push_back (pat);
        auto back = movesFromTree (movesToTree (moves));
        if (back.size() != moves.size()) { std::cout << "FAIL moves round trip\n"; ++failures; }
        for (size_t i = 0; i < juce::jmin (back.size(), moves.size()); ++i)
            if (! (back[i] == moves[i])) { std::cout << "FAIL move " << i << " round trip\n"; ++failures; }
    }

    // ---- formatting
    std::cout << "bar pos 32 beats -> " << formatBarPosition (32.0, model) << ", 16 beats long -> " << formatLength (16.0, 4.0) << "\n";
    if (formatBarPosition (32.0, model) != "9.1.1") { std::cout << "FAIL bar format\n"; ++failures; }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "FAILURES: " + juce::String (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
