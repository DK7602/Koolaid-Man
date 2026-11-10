// Offline engine test: builds a synthetic 8-bar beat, runs drum detection and
// renders every move/mode, every factory preset and a batch of Rolls.
// Usage: KoolaidTests [outputDir]   (writes WAVs when an output dir is given)

#include <juce_audio_formats/juce_audio_formats.h>
#include "Model.h"
#include "DropRenderer.h"
#include "DrumDetector.h"
#include "Presets.h"
#include "Mastering.h"
#include "MoveProcessors.h"

using namespace km;

static juce::AudioBuffer<float> makeBeat (double sr, double bpm, int bars)
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
    for (int i = 0; i < n; ++i)
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
    double worst = -100.0;
    for (auto s = r.zoneStart; s + beat <= r.zoneEnd; s += beat)
    {
        const double ref = juce::jmax (rmsDb (orig, (int) s, beat), rmsDb (orig, (int) r.zoneStart, (int) (r.zoneEnd - r.zoneStart)));
        worst = juce::jmax (worst, rmsDb (r.mix, (int) s, beat) - ref);
    }
    return worst;
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
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File();
    const double sr = 48000.0, bpm = 130.0;
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
            if (! checkBuffer (r->mix, name)) ++failures;
            const bool after = playsAfterDrop (mv.type);
            const double barSamples = 4.0 * m.samplesPerBeat (sr);
            const double bars = r->mix.getNumSamples() / barSamples;
            const bool lengthOk = after ? (r->mix.getNumSamples() >= clip->audio.getNumSamples() && std::abs (bars - std::round (bars)) < 1.0e-3)
                                        : r->mix.getNumSamples() == clip->audio.getNumSamples();
            if (! lengthOk) { std::cout << "FAIL length " << name << " (" << bars << " bars)\n"; ++failures; }
            // the zone (or, for Impact / Sub Boom, the audio after the drop) must actually change
            double diff = 0;
            const int d0 = after ? (int) r->zoneEnd + 64 : (int) r->zoneStart;
            const int d1 = after ? r->mix.getNumSamples() : (int) r->zoneEnd;
            for (int i = d0; i < d1; ++i)
                diff += std::abs (r->mix.getSample (0, i) - (i < clip->audio.getNumSamples() ? clip->audio.getSample (0, i) : 0.0f));
            // and audio outside the zone must be untouched (apart from the drop micro-fade)
            double outside = 0;
            for (int i = 0; i < (int) r->zoneStart; ++i)
                outside += std::abs (r->mix.getSample (0, i) - clip->audio.getSample (0, i));
            const double jump = maxBeatJump (*r, clip->audio);
            std::cout << "  " << name.paddedRight (' ', 44) << juce::String (ms, 1).paddedLeft (' ', 7) << " ms   zone diff "
                      << juce::String (diff / juce::jmax (1, d1 - d0), 4) << "   max beat jump " << juce::String (jump, 1) << " dB\n";
            if (jump > 6.0) { std::cout << "FAIL: " << name << " is " << juce::String (jump, 1) << " dB louder than the track\n"; ++failures; }
            const float tp = juce::Decibels::gainToDecibels (measureTruePeak (r->mix, (int) r->zoneStart, (int) (r->zoneEnd - r->zoneStart), sr));
            if (tp > r->ceilingDb + 0.3f) { std::cout << "FAIL: " << name << " true peak " << tp << " dBTP over ceiling " << r->ceilingDb << "\n"; ++failures; }
            if (diff < 1.0e-3) { std::cout << "FAIL: " << name << " did nothing\n"; ++failures; }
            if (outside > 1.0e-6) { std::cout << "FAIL: " << name << " touched audio before the zone\n"; ++failures; }
            writeWav (outDir, "move - " + name, r->mix, sr);
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
        if (! checkBuffer (r->mix, p.name)) ++failures;
        writeWav (outDir, "preset - " + p.name, r->mix, sr);
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
        if (! checkBuffer (r->mix, "roll " + desc)) ++failures;
        if (maxBeatJump (*r, clip->audio) > 6.0)
        {
            std::cout << "FAIL: roll " << desc << " jumps " << juce::String (maxBeatJump (*r, clip->audio), 1) << " dB\n";
            ++failures;
        }
        if (i < 5)
        {
            std::cout << "roll " << i << ": " << desc << "\n";
            writeWav (outDir, "roll " + juce::String (i), r->mix, sr);
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
                d += std::abs (r->mix.getSample (0, i) - clip->audio.getSample (0, i));
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
        writeWav (outDir, "pitch-tone-plus12", r->mix, sr);
        const int a0 = r->mix.getNumSamples() / 3, len = (int) sr;   // a second from the middle
        auto goertzel = [&] (double f)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, cw = 2.0 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (int i = 0; i < len; ++i) { const double s0 = r->mix.getSample (0, a0 + i) + cw * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - cw * s1 * s2;
        };
        double total = 0;
        for (int i = 0; i < len; ++i) total += (double) r->mix.getSample (0, a0 + i) * r->mix.getSample (0, a0 + i);
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
