// Renders the plugin editor to a PNG without a DAW (used to check the skin).
// Usage: KoolaidSnapshot out.png [audio.wav] [scale] [options...]
// Options include: light, page2, page3, livepads, timeline, curveedit, patternedit, locked,
// zone=<startBeat>,<endBeat>, preset=<factory preset name>, render=<out.wav> (writes the engine's
// rendered audio, for demos)

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

class SnapshotApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "KoolaidSnapshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& cmd) override
    {
        args = juce::StringArray::fromTokens (cmd, true);
        args.removeEmptyStrings();
        for (auto& a : args) a = a.unquoted();
        out = juce::File::getCurrentWorkingDirectory().getChildFile (args.size() > 0 ? args[0] : "snapshot.png");
        scale = args.size() > 2 ? args[2].getFloatValue() : 1.0f;

        proc = std::make_unique<KoolaidProcessor>();
        proc->prepareToPlay (48000.0, 512);
        if (args.size() > 1 && args[1] != "-")
        {
            juce::String err;
            if (! proc->loadAudioFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), 0, err))
                std::cout << "load failed: " << err << "\n";
        }
        if (auto z = argValue ("zone"); z.isNotEmpty())
        {
            proc->getModel().zoneStart = z.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
            proc->getModel().zoneEnd = z.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
            proc->modelChanged (false);
        }
        if (auto name = argValue ("preset"); name.isNotEmpty())
            for (auto& pr : km::factoryPresets())
                if (pr.name == name) { proc->applyPreset (pr); break; }
        startTimer (100);
    }

    juce::String argValue (const juce::String& key) const
    {
        for (auto& a : args)
            if (a.startsWith (key + "=")) return a.fromFirstOccurrenceOf ("=", false, false);
        return {};
    }

    void timerCallback() override
    {
        ++ticks;
        if ((proc->isRendering() || proc->getResultVersion() == 0) && ticks < 80 && proc->hasMusic())
            return;
        stopTimer();
        {
            skin::Skin::get().setTheme (args.contains ("light") ? skin::Theme::light : skin::Theme::dark);
            if (args.contains ("tension") && proc->getModel().moves.size() > 4)
            {
                proc->getModel().moves[1].tensionLink = true;
                proc->getModel().moves[4].tensionLink = true;
                proc->getModel().tension.curve = km::curveEaseIn;
                proc->modelChanged (true);
            }
            if (args.contains ("locked") && proc->getModel().moves.size() > 3)
            {
                proc->getModel().moves[0].locked = true;
                proc->getModel().moves[3].locked = true;
                proc->getModel().moves[3].tensionLink = true;
                proc->modelChanged (true);
            }
            if (args.contains ("timeline") && proc->getModel().moves.size() > 2)
            {
                auto& st = proc->getModel().moves[2];            // time the stutter to the 2nd bar of the build
                st.customRegion = true;
                st.regionStart = -12.0;
                st.regionEnd = -8.0;
                proc->modelChanged (true);
            }
            KoolaidEditor editor (*proc);
            if (args.contains ("curveedit"))
                editor.showCurveEditorForSnapshot();
            if (args.contains ("patternedit"))
                editor.showPatternEditorForSnapshot();
            if (args.contains ("timeline"))
                editor.showTab (3);
            if (args.contains ("page2"))
                editor.showCardPage (1);
            if (args.contains ("usersample"))
            {
                auto clip = std::make_shared<km::ClipData>();
                clip->audio.setSize (2, 4800);
                clip->audio.clear();
                clip->audio.setSample (0, 10, 0.5f);
                proc->getModel().samples["snap"] = clip;
                auto mv = km::defaultMove (km::MoveType::Impact);
                mv.sampleId = "snap";
                mv.sampleName = "Big Hit 01";
                proc->getModel().moves.push_back (mv);
                proc->modelChanged (true);
                editor.refreshForSnapshot();
                editor.showCardPage (1);
            }
            if (args.contains ("seconds"))
            {
                for (auto& mv : proc->getModel().moves)
                    if (mv.type == km::MoveType::TapeStop) { mv.inSeconds = true; mv.seconds = 1.5f; }
                proc->modelChanged (true);
                editor.refreshForSnapshot();
            }
            if (args.contains ("livepads"))
            {
                editor.showCardPage (3);
                proc->setLivePad (2, true);   // one pad held, for the lights
            }
            if (args.contains ("page3"))
            {
                // a few fx moves in the stack, so the page shows them switched on
                auto& mv = proc->getModel().moves;
                mv.push_back (km::defaultMove (km::MoveType::Phaser));
                mv.push_back (km::defaultMove (km::MoveType::Width));
                mv.push_back (km::defaultMove (km::MoveType::PreVerb));
                proc->modelChanged (true);
                editor.refreshForSnapshot();
                editor.showCardPage (2);
            }
            if (args.size() > 3 && args[3].startsWith ("manual"))
            {
                editor.openManual();
                if (args[3] == "manual2") editor.scrollManualForSnapshot (6);
            }
            if (auto path = argValue ("render"); path.isNotEmpty())
                if (auto r = proc->getResult())
                {
                    const auto audio = r->copyOf (km::RenderResult::Layer::mix, 0, r->length);
                    const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (path);
                    f.deleteFile();
                    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
                    juce::WavAudioFormat wav;
                    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate).withNumChannels (2).withBitsPerSample (24);
                    if (auto w = wav.createWriterFor (os, opts))
                        w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
                    std::cout << "rendered audio -> " << f.getFullPathName() << "\n";
                }
            auto img = editor.snapshot (scale);
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
        }
        quit();
    }

    void shutdown() override { proc.reset(); }

private:
    juce::StringArray args;
    juce::File out;
    float scale = 1.0f;
    int ticks = 0;
    std::unique_ptr<KoolaidProcessor> proc;
};

START_JUCE_APPLICATION (SnapshotApp)
