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
        check (result != nullptr && result->mix.getNumSamples() > 0, "render finished");
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
                if (pos + block < result->mix.getNumSamples())
                {
                    for (int i = 0; i < block; i += 37)
                        insideOk &= std::abs (buf.getSample (0, i) - result->mix.getSample (0, (int) (pos + i))) < 1.0e-5f;
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
            p.getModel().zoneStart = 4.0;
            p.getModel().zoneEnd = 12.0;
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
            check (b.zoneStart == 4.0 && b.zoneEnd == 12.0 && b.clipStartBar == a.clipStartBar, "zone and clip position restored");
            check (q.getSettings().exportFormat == km::ExportFormat::Wav32f && q.getSettings().presetName == p.getSettings().presetName,
                   "settings restored");
            bool audioSame = b.music != nullptr && a.music != nullptr && b.music->audio.getNumSamples() == a.music->audio.getNumSamples();
            if (audioSame)
                for (int i = 0; i < a.music->audio.getNumSamples(); i += 101)
                    audioSame &= std::abs (a.music->audio.getSample (0, i) - b.music->audio.getSample (0, i)) < 1.0e-5f;
            check (audioSame, "audio stored in the project (" + juce::String ((int) mb.getSize() / 1024) + " KB state)");

            // export
            juce::String exportErr;
            auto f = q.exportAudio (exportErr);
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
