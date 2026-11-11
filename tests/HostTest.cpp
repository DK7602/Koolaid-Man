// Host-style tests:
//  1. loads the built VST3 through JUCE's VST3 hosting (like a DAW), processes audio,
//     round-trips its state and opens its editor;
//  2. drives the processor with a simulated DAW transport to check in-sync preview,
//     bar-aligned capture, odd block sizes and state save/restore.
// Usage: KoolaidHostTest "<path to Koolaid Man.vst3>" beat.wav

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        juce::AudioPlayHead::PositionInfo info;
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return info; }

        void set (double ppq, bool playing, double bpm = 130.0)
        {
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        }
    };

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    void waitForRender (KoolaidProcessor& p)
    {
        for (int i = 0; i < 100 && (p.isRendering() || p.getResultVersion() == 0); ++i)
            pump (50);
        pump (50);
    }
}

class HostTestApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "KoolaidHostTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& cmd) override
    {
        auto args = juce::StringArray::fromTokens (cmd, true);
        args.removeEmptyStrings();
        for (auto& a : args) a = a.unquoted();
        juce::MessageManager::callAsync ([this, args] { run (args); });
    }

    void shutdown() override {}

    void run (const juce::StringArray& args)
    {
        if (args.size() > 0 && args[0] != "-") testVst3 (juce::File (args[0]));
        if (args.size() > 1) testProcessor (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
        std::cout << (failures == 0 ? "HOST TESTS PASSED" : "HOST TEST FAILURES: " + juce::String (failures)) << std::endl;
        // nested dispatch loops can leave the Linux message loop waiting; exit directly
        std::_Exit (failures == 0 ? 0 : 1);
    }

    //==========================================================================
    void testVst3 (const juce::File& bundle)
    {
        std::cout << "VST3 hosting test: " << bundle.getFullPathName() << "\n";
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
        juce::OwnedArray<juce::PluginDescription> types;
        for (auto* f : fm.getFormats())
            f->findAllTypesForFile (types, bundle.getFullPathName());
        check (types.size() == 1, "plugin found in bundle (" + juce::String (types.size()) + ")");
        if (types.isEmpty()) return;
        check (types[0]->name == "Koolaid Man", "name is Koolaid Man (got " + types[0]->name + ")");
        check (types[0]->category.containsIgnoreCase ("Fx"), "category " + types[0]->category);

        juce::String err;
        auto inst = fm.createPluginInstance (*types[0], 48000.0, 512, err);
        check (inst != nullptr, "instantiated " + err);
        if (inst == nullptr) return;

        check (inst->getTotalNumInputChannels() == 2 && inst->getTotalNumOutputChannels() == 2, "stereo in/out");
        check (inst->getParameters().size() >= 2, "parameters exposed: " + juce::String (inst->getParameters().size()));

        inst->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() * 0.2f - 0.1f);
            inst->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    finite &= std::isfinite (buf.getSample (ch, i));
        }
        check (finite, "200 blocks processed, output finite");

        juce::MemoryBlock state;
        inst->getStateInformation (state);
        check (state.getSize() > 50, "state saved (" + juce::String ((int) state.getSize()) + " bytes)");
        auto inst2 = fm.createPluginInstance (*types[0], 48000.0, 512, err);
        if (inst2 != nullptr)
        {
            inst2->setStateInformation (state.getData(), (int) state.getSize());
            juce::MemoryBlock state2;
            inst2->getStateInformation (state2);
            check (state2.getSize() == state.getSize(), "state restored into a second instance");
        }

        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
            check (ed != nullptr, "editor created through VST3");
            if (ed != nullptr)
            {
                ed->addToDesktop (0);
                ed->setVisible (true);
                pump (300);
                check (ed->getWidth() > 500 && ed->getHeight() > 300,
                       "editor size " + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));
                ed->setVisible (false);
                ed->removeFromDesktop();
            }
        }
        inst->releaseResources();
    }

    //==========================================================================
    void testProcessor (const juce::File& wav)
    {
        std::cout << "Processor test with simulated DAW transport\n";
        const double sr = 48000.0, bpm = 130.0, spb = sr * 60.0 / bpm;
        KoolaidProcessor p;
        p.setRateAndBufferSizeDetails (sr, 512);
        p.prepareToPlay (sr, 512);
        juce::String err;
        check (p.loadAudioFile (wav, 0, err), "loaded " + wav.getFileName() + " " + err);
        p.getModel().bpm = bpm;
        p.getModel().tempoSync = true;
        p.modelChanged (false);
        waitForRender (p);
        auto result = p.getResult();
        check (result != nullptr && result->length > 0, "render finished");
        if (result == nullptr) return;
        check (std::abs (p.getModel().zoneEnd - 32.0) < 1.0e-6 && std::abs (p.getModel().zoneStart - 16.0) < 1.0e-6,
               "default zone = last 4 bars (5.1.1 to 9.1.1)");
        check (result->analysis != nullptr && result->analysis->snare.found, "drums detected in the loaded clip");

        FakePlayHead ph;
        p.setPlayHead (&ph);
        juce::MidiBuffer midi;

        // ---- in-sync preview: replaces the track's audio inside the clip, passes it through outside
        {
            p.setSyncPreview (true);
            pump (50);
            const int block = 480;
            juce::AudioBuffer<float> buf (2, block);
            bool insideOk = true, outsideOk = true;
            for (double ppq = 16.0; ppq < 40.0; ppq += block / spb)
            {
                ph.set (ppq, true);
                for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.123f, block);
                p.processBlock (buf, midi);
                const auto pos = (juce::int64) std::floor (ppq * spb);
                if (pos + block < result->length)
                {
                    for (int i = 0; i < block; i += 37)
                        insideOk &= std::abs (buf.getSample (0, i) - result->mixAt (0, pos + i)) < 1.0e-5f;
                }
                else if (ppq > 32.5)
                {
                    for (int i = 0; i < block; i += 37)
                        outsideOk &= std::abs (buf.getSample (0, i) - 0.123f) < 1.0e-6f;
                }
            }
            check (insideOk, "sync preview plays the rendered drop sample-accurately at the host position");
            check (outsideOk, "outside the clip the track audio passes through untouched");
            check (p.isSyncPlayheadActive(), "sync playhead active while the DAW plays");

            ph.set (40.0, false);
            p.processBlock (buf, midi);
            check (! p.isSyncPlayheadActive(), "sync playhead stops with the DAW");
        }

        // ---- local preview loops the build zone
        {
            p.setSyncPreview (false);
            p.getSettings().playbackMode = km::PlaybackMode::PreDrop;
            p.getSettings().loop = true;
            p.modelChanged (false);
            waitForRender (p);
            p.setPlaying (true);
            juce::AudioBuffer<float> buf (2, 512);
            const auto zs = (juce::int64) (16.0 * spb), ze = (juce::int64) (32.0 * spb);
            bool inZone = true;
            for (int b = 0; b < 2000; ++b)
            {
                buf.clear();
                p.processBlock (buf, midi);
                const auto ph2 = p.getPlayheadSample();
                inZone &= ph2 >= zs && ph2 <= ze;
            }
            check (inZone && p.isPlaying(), "local preview loops inside the build zone");
            p.setPlaying (false);
        }

        // ---- capture: starts on the next bar line, keeps whole bars, sets the clip's bar
        {
            p.setSyncPreview (false);
            p.armCapture (true);
            const int block = 512;
            juce::AudioBuffer<float> buf (2, block);
            const double startPpq = 6.5;               // middle of bar 2 -> capture starts at bar 3 (ppq 8)
            juce::int64 absSample = (juce::int64) std::llround (startPpq * spb);
            double ppq = startPpq;
            while (ppq < 8.0 + 4 * 4.0 + 1.5)          // 4 bars + a bit
            {
                ph.set (ppq, true);
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) ((absSample + i) % 100000) / 100000.0f;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                p.processBlock (buf, midi);
                absSample += block;
                ppq += block / spb;
            }
            check (p.isCapturing(), "capturing while the DAW plays");
            ph.set (ppq, false);
            p.processBlock (buf, midi);
            pump (400);
            auto& m = p.getModel();
            check (! p.isCaptureArmed(), "capture finished when the DAW stopped");
            check (m.clipStartBar == 3, "captured clip starts at bar 3 (got " + juce::String (m.clipStartBar) + ")");
            const double beats = m.clipLengthBeats();
            check (std::abs (beats - 16.0) < 0.01, "captured clip trimmed to 4 whole bars (got " + juce::String (beats, 3) + " beats)");
            if (m.music != nullptr)
            {
                const float first = m.music->audio.getSample (0, 0);
                const auto barSample = (juce::int64) std::llround (8.0 * spb);
                const float expected = (float) (barSample % 100000) / 100000.0f;
                check (std::abs (first - expected) < 2.0f / 100000.0f * 2.0f,
                       "first captured sample is the bar line (" + juce::String (first, 6) + " vs " + juce::String (expected, 6) + ")");
            }
            waitForRender (p);
            check (km::formatBarPosition (m.zoneEnd, m) == "7.1.1", "drop defaults to the end of the capture: " + km::formatBarPosition (m.zoneEnd, m));
        }

        // ---- capture in cycle mode: the transport loops back -> capture ends at the jump
        {
            p.setSyncPreview (false);
            p.armCapture (true);
            const int block = 512;
            juce::AudioBuffer<float> buf (2, block);
            double ppq = 6.5;
            auto playTo = [&] (double endPpq)
            {
                while (ppq < endPpq)
                {
                    ph.set (ppq, true);
                    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.25f, block);
                    p.processBlock (buf, midi);
                    ppq += block / spb;
                }
            };
            playTo (8.0 + 8.0 + 1.0);   // capture starts at bar 3 (ppq 8); play 2 bars and a beat
            ppq = 8.0;                  // cycle jumps back to bar 3
            playTo (8.0 + 4.0);
            ph.set (ppq, false);
            p.processBlock (buf, midi);
            pump (400);
            const double beats = p.getModel().clipLengthBeats();
            check (! p.isCaptureArmed() && std::abs (beats - 8.0) < 0.01,
                   "cycle jump ends the capture (kept 2 whole bars, got " + juce::String (beats, 3) + " beats)");
            waitForRender (p);
        }

        // ---- sync preview fades in at the clip's start (no click)
        {
            p.setSyncPreview (true);
            p.getSettings().previewReplaces = true;
            p.modelChanged (false);
            waitForRender (p);
            auto r = p.getResult();
            const double clipPpq = (p.getModel().clipStartBar - 1) * 4.0;
            const int block = 256;
            juce::AudioBuffer<float> buf (2, block);
            float maxStep = 0.0f, prev = 0.7f;
            double ppq = clipPpq - 2.0 * block / spb;
            for (int k = 0; k < 6; ++k)
            {
                ph.set (ppq, true);
                for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.7f, block);
                p.processBlock (buf, midi);
                for (int i = 0; i < block; ++i)
                {
                    maxStep = juce::jmax (maxStep, std::abs (buf.getSample (0, i) - prev));
                    prev = buf.getSample (0, i);
                }
                ppq += block / spb;
            }
            ph.set (ppq, false);
            p.processBlock (buf, midi);
            // the input is a constant 0.7 and the clip starts elsewhere: without a fade that's a jump
            check (maxStep < 0.12f, "track -> drop switch is crossfaded (largest step " + juce::String (maxStep, 3) + ")");
            juce::ignoreUnused (r);
        }

        // ---- odd block sizes, no NaNs
        {
            p.setSyncPreview (true);
            bool finite = true;
            double ppq = 8.0;
            for (int bs : { 1, 7, 64, 333, 513, 2048, 4096 })
            {
                juce::AudioBuffer<float> buf (2, bs);
                for (int k = 0; k < 20; ++k)
                {
                    ph.set (ppq, true);
                    buf.clear();
                    p.processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < bs; ++i) finite &= std::isfinite (buf.getSample (ch, i));
                    ppq += bs / spb;
                }
            }
            check (finite, "odd block sizes (1..4096) process cleanly");
        }

        // ---- state round trip
        {
            p.applyPreset (km::factoryPresets()[3]);
            p.getModel().zoneStart = 2.0;
            p.getModel().zoneEnd = 6.0;
            p.getSettings().exportFormat = km::ExportFormat::Wav32f;
            p.modelChanged (false);
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            KoolaidProcessor q;
            q.setRateAndBufferSizeDetails (sr, 512);
            q.prepareToPlay (sr, 512);
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            auto& a = p.getModel();
            auto& b = q.getModel();
            bool movesSame = a.moves.size() == b.moves.size();
            for (size_t i = 0; movesSame && i < a.moves.size(); ++i)
                movesSame = a.moves[i] == b.moves[i];
            check (movesSame, "moves stack restored");
            check (b.zoneStart == 2.0 && b.zoneEnd == 6.0 && b.clipStartBar == a.clipStartBar, "zone and clip position restored");
            check (q.getSettings().exportFormat == km::ExportFormat::Wav32f && q.getSettings().presetName == p.getSettings().presetName,
                   "settings restored");
            bool audioSame = b.music != nullptr && a.music != nullptr && b.music->audio.getNumSamples() == a.music->audio.getNumSamples();
            if (audioSame)
                for (int i = 0; i < a.music->audio.getNumSamples(); i += 101)
                    audioSame &= std::abs (a.music->audio.getSample (0, i) - b.music->audio.getSample (0, i)) < 1.0e-5f;
            check (audioSame, "audio stored in the project (" + juce::String ((int) mb.getSize() / 1024) + " KB state)");

            // export
            juce::String exportErr;
            auto fs = q.exportAudio (exportErr);
            auto f = fs.isEmpty() ? juce::File() : fs.getFirst();
            check (f.existsAsFile(), "export wrote " + f.getFileName() + " " + exportErr);
            if (f.existsAsFile())
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
                check (r != nullptr && r->numChannels == 2 && r->bitsPerSample == 32 && r->usesFloatingPointData,
                       "export is stereo 32-bit float");
                if (r != nullptr)
                    check (std::abs ((double) r->lengthInSamples - b.clipLengthBeats() * spb) < 2.0, "export is exactly the clip length");
                f.deleteFile();
            }
        }

        // ---- after-drop moves: the render (and export) grow by a bar-aligned tail
        {
            auto& m = p.getModel();
            const double bpb = m.beatsPerBar();
            m.zoneEnd = std::floor (m.clipLengthBeats() / bpb + 1.0e-3) * bpb;   // drop at the very end
            m.zoneStart = juce::jmax (0.0, m.zoneEnd - bpb);
            m.moves = { km::defaultMove (km::MoveType::Riser), km::defaultMove (km::MoveType::Impact) };
            p.getSettings().exportRange = km::ExportRange::FullClip;
            p.getSettings().exportFormat = km::ExportFormat::Wav24;
            p.modelChanged (true);
            waitForRender (p);
            auto r = p.getResult();
            const double clipBars = m.clipLengthBeats() / bpb;
            const double outBars = r->length / (r->samplesPerBeat * bpb);
            check (outBars > clipBars + 0.9 && std::abs (outBars - std::round (outBars)) < 1.0e-3,
                   "Impact adds a whole-bar tail after the drop (" + juce::String (clipBars, 2) + " -> " + juce::String (outBars, 2) + " bars)");
            juce::String err;
            auto fs = p.exportAudio (err);
            auto f = fs.isEmpty() ? juce::File() : fs.getFirst();
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            check (rd != nullptr && std::abs ((double) rd->lengthInSamples - r->length) < 2.0, "export includes the tail");
            rd.reset();
            f.deleteFile();
        }

        // ---- undo / redo: one step per gesture
        {
            pump (700);   // let the previous test's edit settle into its own step
            auto& m = p.getModel();
            const auto before = m.moves.size();
            m.moves.push_back (km::defaultMove (km::MoveType::Riser));
            p.modelChanged (true);
            m.moves.back().knob = 0.2f;              // same gesture (within 600 ms) -> merged
            p.modelChanged (true);
            pump (700);
            m.moves.push_back (km::defaultMove (km::MoveType::Gap));
            p.modelChanged (true);
            check (p.canUndo(), "undo available after edits");
            p.undo();
            check (m.moves.size() == before + 1 && std::abs (m.moves.back().knob - 0.2f) < 1.0e-6f, "undo removes the last gesture only");
            p.undo();
            check (m.moves.size() == before, "second undo returns to the start (knob tweak merged into one step)");
            check (p.canRedo(), "redo available");
            p.redo();
            p.redo();
            check (m.moves.size() == before + 2, "redo replays both steps");
        }

        // ---- damaged projects: garbage, truncated and bit-flipped state must never crash, and
        //      the plug-in must keep working afterwards
        {
            juce::MemoryBlock good;
            p.getStateInformation (good);
            KoolaidProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            juce::Random fz (99);
            int tries = 0;
            bool ok = true;
            auto stillWorks = [&]
            {
                juce::AudioBuffer<float> b (2, 512);
                juce::MidiBuffer mb;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i) b.setSample (ch, i, 0.1f * (float) std::sin (i * 0.05));
                q.processBlock (b, mb);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i) if (! std::isfinite (b.getSample (ch, i))) return false;
                return true;
            };
            // pure garbage
            for (int k = 0; k < 60; ++k, ++tries)
            {
                juce::MemoryBlock junk ((size_t) (1 + fz.nextInt (4096)));
                for (size_t i = 0; i < junk.getSize(); ++i) static_cast<char*> (junk.getData())[i] = (char) fz.nextInt (256);
                q.setStateInformation (junk.getData(), (int) junk.getSize());
                ok &= stillWorks();
            }
            // a real project cut short at many points
            for (int k = 1; k <= 40; ++k, ++tries)
            {
                const int len = (int) (good.getSize() * (size_t) k / 41);
                q.setStateInformation (good.getData(), len);
                ok &= stillWorks();
            }
            // a real project with random bytes damaged
            for (int k = 0; k < 40; ++k, ++tries)
            {
                juce::MemoryBlock bad (good);
                for (int f = 0; f < 1 + fz.nextInt (20); ++f)
                    static_cast<char*> (bad.getData())[fz.nextInt ((int) bad.getSize())] ^= (char) (1 + fz.nextInt (255));
                q.setStateInformation (bad.getData(), (int) bad.getSize());
                ok &= stillWorks();
                pump (5);
            }
            q.setStateInformation (good.getData(), (int) good.getSize());   // and a good one still loads afterwards
            pump (300);
            check (ok && stillWorks(), juce::String (tries) + " damaged / garbage projects loaded without a crash, and it kept working");
            check (q.getModel().moves.size() == p.getModel().moves.size(), "a good project still loads after all that");
            q.releaseResources();
        }

        // ---- audio-thread CPU: 96 kHz, 64-sample blocks, sync preview playing the drop and all
        //      eight live pads held at once (the worst case)
        {
            KoolaidProcessor q;
            juce::MemoryBlock good;
            p.getStateInformation (good);
            q.setStateInformation (good.getData(), (int) good.getSize());
            q.setPlayConfigDetails (2, 2, 96000.0, 64);
            q.prepareToPlay (96000.0, 64);
            waitForRender (q);
            FakePlayHead cph;
            q.setPlayHead (&cph);
            for (int pad = 0; pad < km::LiveEngine::numPads; ++pad) q.setLivePad (pad, true);
            juce::AudioBuffer<float> b (2, 64);
            juce::MidiBuffer mb;
            const int blocks = (int) (96000.0 * 10.0 / 64.0);   // 10 seconds of audio
            const double ppqPerBlock = 64.0 * 130.0 / (60.0 * 96000.0);
            const double startPpq = (q.getModel().clipStartBar - 1) * 4.0 + q.getModel().zoneStart;   // inside the drop's build
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < blocks; ++k)
            {
                cph.set (startPpq + k * ppqPerBlock, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i) b.setSample (ch, i, 0.1f * (float) std::sin ((k * 64 + i) * 0.01));
                q.processBlock (b, mb);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double load = ms / 10000.0 * 100.0;
            const bool synced = q.isSyncPlayheadActive();
            for (int pad = 0; pad < km::LiveEngine::numPads; ++pad) q.setLivePad (pad, false);
            q.setPlayHead (nullptr);
            check (synced, "...with the in-sync preview playing the drop");
            check (load < 25.0, "audio-thread CPU, worst case (96 kHz, 64-sample blocks, all pads): " + juce::String (load, 1) + "% of one core");
            q.releaseResources();
        }

        // ---- LIVE PADS: a MIDI note holds a pad, sample-accurately; automation does the same
        {
            KoolaidProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            auto block = [&] (juce::AudioBuffer<float>& b, int startSample)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (startSample + i) / 48000.0);
                    b.setSample (0, i, v);
                    b.setSample (1, i, v);
                }
            };
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer midi;
            bool passBefore = true, silentDuring = true, backAfter = true;
            int n = 0;
            for (int k = 0; k < 40; ++k, n += 512)
            {
                block (b, n);
                juce::AudioBuffer<float> ref (b);
                midi.clear();
                if (k == 10) midi.addEvent (juce::MidiMessage::noteOn (1, km::LiveEngine::firstNote + km::LiveEngine::cut, (juce::uint8) 100), 200);
                if (k == 20) midi.addEvent (juce::MidiMessage::noteOff (1, km::LiveEngine::firstNote + km::LiveEngine::cut), 0);
                q.processBlock (b, midi);
                for (int i = 0; i < 512; ++i)
                {
                    const float d = std::abs (b.getSample (0, i) - ref.getSample (0, i));
                    if (k < 10 || (k == 10 && i < 200)) passBefore &= d == 0.0f;
                    if (k >= 13 && k < 20) silentDuring &= std::abs (b.getSample (0, i)) < 1.0e-4f;   // after the ~3 ms de-click fade
                    if (k > 22) backAfter &= d < 1.0e-4f;
                }
            }
            check (q.acceptsMidi(), "accepts MIDI (pads on notes C1-G1)");
            check (passBefore, "audio passes untouched until the note (sample-accurate)");
            check (silentDuring, "MIDI note C1+7 (G1) holds CUT: silence");
            check (backAfter, "note off: the track comes back");

            // the same pad from its automatable parameter
            q.setLivePad (km::LiveEngine::cut, true);
            bool silentParam = true;
            for (int k = 0; k < 6; ++k, n += 512)
            {
                block (b, n);
                midi.clear();
                q.processBlock (b, midi);
                if (k > 2) silentParam &= b.getMagnitude (0, 0, 512) < 1.0e-4f;
            }
            q.setLivePad (km::LiveEngine::cut, false);
            check (silentParam, "the \"Live Cut\" parameter (automation / on-screen pad) holds it too");
            q.releaseResources();
        }

        // ---- ROLL keeps locked moves and remembers its style
        {
            auto& m = p.getModel();
            m.moves.clear();
            auto f = km::defaultMove (km::MoveType::Filter);
            f.knob = 0.123f;
            f.locked = true;
            m.moves.push_back (f);
            p.modelChanged (true);
            p.getSettings().rollStyle = 2;   // Drill
            bool kept = true, single = true;
            for (int i = 0; i < 12; ++i)
            {
                p.roll();
                int filters = 0;
                bool found = false;
                for (auto& mv : m.moves)
                    if (mv.type == km::MoveType::Filter)
                    {
                        ++filters;
                        found |= mv.locked && std::abs (mv.knob - 0.123f) < 1.0e-6f;
                    }
                kept &= found;
                single &= filters == 1;
            }
            check (kept, "ROLL keeps a locked move exactly as it was");
            check (single, "ROLL doesn't add a second move of a locked type");
            check (p.getSettings().presetName.startsWith ("Drill Roll"), "roll is named after its style: " + p.getSettings().presetName);

            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            KoolaidProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            bool lockSaved = false;
            for (auto& mv : q.getModel().moves) lockSaved |= mv.locked;
            check (q.getSettings().rollStyle == 2 && lockSaved, "ROLL style and locks saved with the project");
            m.moves.clear();
            p.getSettings().rollStyle = 0;
            p.modelChanged (true);
        }

        // ---- your own sample: loads into a move, renders, and is saved with the project
        {
            auto& m = p.getModel();
            m.moves.clear();
            m.moves.push_back (km::defaultMove (km::MoveType::Impact));
            p.modelChanged (true);
            const int uid = m.moves.front().uid;

            // a short sample on disk
            const auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("koolaid-test-hit.wav");
            {
                juce::AudioBuffer<float> hit (1, 24000);
                for (int i = 0; i < hit.getNumSamples(); ++i)
                    hit.setSample (0, i, 0.6f * (float) (std::sin (juce::MathConstants<double>::twoPi * 180.0 * i / 48000.0) * std::exp (-6.0 * i / 24000.0)));
                wavFile.deleteFile();
                std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (wavFile);
                juce::WavAudioFormat wav;
                auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24);
                if (auto wr = wav.createWriterFor (os, opts)) wr->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
            }
            juce::String err;
            check (p.loadMoveSample (uid, wavFile, err), "sample loaded into the Impact move " + err);
            check (m.moves.front().hasSample() && m.moves.front().sampleName == "koolaid-test-hit", "move uses the sample: " + m.moves.front().stackLabel());
            check (m.samples.count (m.moves.front().sampleId) == 1, "sample is in the session's sample pool");

            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            wavFile.deleteFile();   // the project must not need the file any more
            KoolaidProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            const auto& qm = q.getModel();
            const bool restored = ! qm.moves.empty() && qm.moves.front().hasSample()
                                  && qm.samples.count (qm.moves.front().sampleId) == 1
                                  && qm.samples.at (qm.moves.front().sampleId) != nullptr
                                  && qm.samples.at (qm.moves.front().sampleId)->audio.getNumSamples() > 1000;
            check (restored, "sample restored from the project (file deleted)");

            // a preset only remembers the path: it comes back while the file exists
            km::Preset pre;
            pre.name = "With Sample";
            pre.moves = { m.moves.front() };
            pre.moves.front().sampleId = "from-preset";
            pre.moves.front().samplePath = wavFile.getFullPathName();
            {
                juce::AudioBuffer<float> hit (2, 4800);
                hit.clear();
                for (int i = 0; i < 4800; ++i) hit.setSample (0, i, 0.3f * (float) std::sin (i * 0.05));
                std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (wavFile);
                juce::WavAudioFormat wav;
                auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24);
                if (auto wr = wav.createWriterFor (os, opts)) wr->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
            }
            p.applyPreset (pre);
            check (p.getModel().samples.count ("from-preset") == 1, "preset sample reloaded from its file");
            wavFile.deleteFile();

            p.clearMoveSample (p.getModel().moves.front().uid);
            check (! p.getModel().moves.front().hasSample(), "back to the built-in sound");
            p.getModel().moves.clear();
            p.modelChanged (true);
        }

        // ---- A/B compare
        {
            auto& m = p.getModel();
            const auto aMoves = m.moves;
            p.switchAB (1);
            check (m.moves.size() == aMoves.size(), "B starts as a copy of A");
            m.moves.clear();
            p.modelChanged (true);
            p.switchAB (0);
            check (m.moves.size() == aMoves.size(), "back to A restores its moves");
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            KoolaidProcessor q;
            q.setRateAndBufferSizeDetails (sr, 512);
            q.prepareToPlay (sr, 512);
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            q.switchAB (1);
            check (q.getModel().moves.empty(), "B is saved with the project");
            p.switchAB (1);
            p.switchAB (0);
        }

        // ---- Build Amount = 0 plays the untouched audio, in sync
        {
            juce::AudioProcessorParameter* build = nullptr;
            for (auto* prm : p.getParameters())
                if (prm->getName (32) == "Build Amount") build = prm;
            check (build != nullptr, "Build Amount parameter exposed to the host");
            if (build != nullptr)
            {
                build->setValueNotifyingHost (0.0f);
                p.setSyncPreview (true);
                waitForRender (p);
                auto r = p.getResult();
                const int block = 512;
                juce::AudioBuffer<float> buf (2, block);
                const double ppqStart = (p.getModel().clipStartBar - 1) * 4.0 + p.getModel().zoneStart + 1.0;
                ph.set (ppqStart, true);
                p.processBlock (buf, midi);   // first block ramps the amount
                ph.set (ppqStart + block / spb, true);
                buf.clear();
                p.processBlock (buf, midi);
                const auto pos = (juce::int64) std::floor ((ppqStart + block / spb - (p.getModel().clipStartBar - 1) * 4.0) * spb);
                bool same = true;
                for (int i = 0; i < block; i += 17)
                    same &= std::abs (buf.getSample (0, i) - r->source->audio.getSample (0, (int) (pos + i))) < 1.0e-4f;
                check (same, "Build Amount 0% previews the original audio");
                build->setValueNotifyingHost (1.0f);
                ph.set (0.0, false);
                p.processBlock (buf, midi);
            }
        }

        // ---- Tension knob (host parameter) blends live toward the render without linked moves
        {
            juce::AudioProcessorParameter* tensionP = nullptr;
            for (auto* prm : p.getParameters())
                if (prm->getName (32) == "Tension") tensionP = prm;
            check (tensionP != nullptr, "Tension parameter exposed to the host");
            auto& m = p.getModel();
            const auto saved = m.moves;
            km::Move ch = km::defaultMove (km::MoveType::Chops);
            ch.knob = 1.0f; ch.tensionLink = true;
            m.moves = { ch };
            p.modelChanged (true);
            waitForRender (p);
            auto r = p.getResult();
            check (r != nullptr && r->base != nullptr, "render keeps a version without the linked moves");
            if (tensionP != nullptr && r != nullptr && r->base != nullptr)
            {
                tensionP->setValueNotifyingHost (0.0f);
                p.setSyncPreview (true);
                const int block = 512;
                juce::AudioBuffer<float> buf (2, block);
                const double ppq0 = (m.clipStartBar - 1) * 4.0 + m.zoneStart + 2.0;
                for (int k = 0; k < 2; ++k)
                {
                    ph.set (ppq0 + k * block / spb, true);
                    buf.clear();
                    p.processBlock (buf, midi);
                }
                const auto pos = (juce::int64) std::floor ((ppq0 + block / spb - (m.clipStartBar - 1) * 4.0) * spb);
                bool same = true;
                for (int i = 0; i < block; i += 13)
                    same &= std::abs (buf.getSample (0, i) - r->base->mixAt (0, pos + i)) < 1.0e-4f;
                check (same, "Tension 0% plays the build without the linked moves");
                tensionP->setValueNotifyingHost (1.0f);
                ph.set (0.0, false);
                p.processBlock (buf, midi);
            }
            m.moves = saved;
            p.modelChanged (true);
            waitForRender (p);
        }

        // ---- stems export: music + fx, same length
        {
            p.getSettings().exportRange = km::ExportRange::Stems;
            juce::String err;
            auto files = p.exportAudio (err);
            check (files.size() == 2, "stems export writes 2 files " + err);
            if (files.size() == 2)
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r1 (fm.createReaderFor (files[0])), r2 (fm.createReaderFor (files[1]));
                check (r1 != nullptr && r2 != nullptr && r1->lengthInSamples == r2->lengthInSamples, "stems line up (same length)");
                check (files[0].getFileName().contains ("Music") && files[1].getFileName().contains ("FX"), "stems are named Music / FX");
            }
            for (auto& f : files) f.deleteFile();
            p.getSettings().exportRange = km::ExportRange::FullClip;
        }

        // ---- page 2 cards
        {
            std::unique_ptr<KoolaidEditor> ed (dynamic_cast<KoolaidEditor*> (p.createEditor()));
            ed->showCardPage (1);
            auto img = ed->snapshot (0.5f);
            ed->showCardPage (0);
            check (img.isValid(), "BUILD & DROP page renders");
            ed->showTab (3);
            img = ed->snapshot (0.5f);
            ed->showTab (0);
            check (img.isValid(), "TIMELINE view renders");
        }

        // ---- a full 3-minute song: load, render, save state (timings are informational)
        {
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("koolaid-long-test.wav");
            {
                juce::AudioBuffer<float> song (2, (int) (sr * 180.0));
                juce::Random rng (5);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < song.getNumSamples(); ++i) song.setSample (ch, i, (rng.nextFloat() - 0.5f) * 0.4f);
                tmp.deleteFile();
                std::unique_ptr<juce::OutputStream> os = tmp.createOutputStream();
                juce::WavAudioFormat wav;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
                w->writeFromAudioSampleBuffer (song, 0, song.getNumSamples());
            }
            KoolaidProcessor q;
            q.setRateAndBufferSizeDetails (sr, 512);
            q.prepareToPlay (sr, 512);
            juce::String err;
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            check (q.loadAudioFile (tmp, 0, err), "3-minute song loads");
            waitForRender (q);
            const auto tLoad = juce::Time::getMillisecondCounterHiRes() - t0;
            juce::MemoryBlock mb;
            t0 = juce::Time::getMillisecondCounterHiRes();
            q.getStateInformation (mb);
            const auto tSave1 = juce::Time::getMillisecondCounterHiRes() - t0;
            mb.reset();
            t0 = juce::Time::getMillisecondCounterHiRes();
            q.getStateInformation (mb);
            const auto tSave2 = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << "  info  3-min song: load+analyse+render " << juce::String (tLoad, 0) << " ms, project save "
                      << juce::String (tSave1, 0) << " ms (again: " << juce::String (tSave2, 0) << " ms), state "
                      << (int) (mb.getSize() / (1024 * 1024)) << " MB" << std::endl;
            check (tSave2 < 250.0, "saving the project again is quick (" + juce::String (tSave2, 0) + " ms)");
            tmp.deleteFile();
        }

        // ---- editor lifecycle on the real processor
        {
            for (int i = 0; i < 3; ++i)
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
                pump (100);
            }
            check (true, "editor opened/closed 3 times");

            // light/dark switching while the editor is open
            std::unique_ptr<KoolaidEditor> ed (dynamic_cast<KoolaidEditor*> (p.createEditor()));
            const auto before = skin::Skin::get().getTheme();
            auto dark = (skin::Skin::get().setTheme (skin::Theme::dark), ed->snapshot (0.5f));
            skin::Skin::get().setTheme (skin::Theme::light);
            pump (50);
            auto light = ed->snapshot (0.5f);
            const auto px = [] (const juce::Image& im, int x, int y) { return im.getPixelAt (x, y).getPerceivedBrightness(); };
            // a background panel point (moves-stack title bar) should flip from dark to light
            check (px (dark, 300, 300) < 0.45f && px (light, 300, 300) > 0.6f,
                   "theme switch repaints the panels (" + juce::String (px (dark, 300, 300), 2) + " -> " + juce::String (px (light, 300, 300), 2) + ")");
            // the header art is the same in both themes
            check (std::abs (px (dark, 120, 60) - px (light, 120, 60)) < 0.05f, "header art unchanged by the theme");
            skin::Skin::get().setTheme (before);
            ed.reset();
        }
        p.setPlayHead (nullptr);
    }
};

START_JUCE_APPLICATION (HostTestApp)
