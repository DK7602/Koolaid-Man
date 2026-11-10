#pragma once

#include "Skin.h"
#include "PluginProcessor.h"

class WaveformView : public juce::Component
{
public:
    explicit WaveformView (KoolaidProcessor&);

    void refresh();          // model/result changed: rebuild peaks
    void tick();             // ~30 Hz: playhead and status
    void setTab (int t)      { tab = t; refresh(); }
    void zoomBy (double factor);
    void zoomFit();
    void showMessage (const juce::String& msg, int millis = 3500);

    std::function<void()> onZoneEdited;      // called while the zone is dragged
    std::function<void()> onEmptyClicked;    // click on the empty display (load)

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = Hit::none; repaint(); }

private:
    enum class Hit { none, leftEdge, rightEdge, inside, outside };

    struct Peaks
    {
        std::vector<float> mn, mx;   // per bucket
        int bucket = 256;
        juce::int64 numSamples = 0;
        double sampleRate = 48000.0;
    };
    static Peaks makePeaks (const juce::AudioBuffer<float>&, double sr);

    juce::Rectangle<float> contentArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    double snap (double beat, const juce::ModifierKeys&) const;
    Hit hitTest (juce::Point<float>) const;
    double clipBeats() const;

    void drawWave (juce::Graphics&, const Peaks&, juce::Colour outside, juce::Colour inside, float centreY, float halfH);

    KoolaidProcessor& proc;
    juce::Image topStrip, bottomStrip;   // hides the zone outline baked into the mockup
    int tab = 0;
    Peaks peaks;
    int peaksTab = -1;
    const void* peaksSource = nullptr;

    double viewStart = 0.0, viewLen = 32.0;
    bool fitted = true;

    Hit hover = Hit::none, dragMode = Hit::none;
    double dragStartBeat = 0.0, dragZoneStart = 0.0, dragZoneEnd = 0.0;

    float lastPlayheadX = -1.0f;
    juce::String message;
    juce::uint32 messageUntil = 0;
    int blink = 0;
};
