// Renders the plugin editor to a PNG without a DAW (used to check the skin).
// Usage: KoolaidSnapshot out.png [audio.wav] [scale]

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
        startTimer (100);
    }

    void timerCallback() override
    {
        ++ticks;
        if ((proc->isRendering() || proc->getResultVersion() == 0) && ticks < 80 && proc->hasMusic())
            return;
        stopTimer();
        {
            KoolaidEditor editor (*proc);
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
