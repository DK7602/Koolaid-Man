#pragma once

#include "PluginProcessor.h"
#include "Skin.h"
#include "WaveformView.h"
#include "MoveStackView.h"
#include "EffectCards.h"
#include "ManualView.h"

class KoolaidEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit KoolaidEditor (KoolaidProcessor&);
    ~KoolaidEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    /** Renders the editor at design size into an image (used by the snapshot tool). */
    juce::Image snapshot (float scale);
    void openManual();
    void scrollManualForSnapshot (int section) { manual.scrollToSection (section); }
    void showCardPage (int p) { if (cards) cards->setPage (p); }
    void showTab (int t) { setTab (t); }

private:
    /** Paints the artwork; hosts every control in design coordinates. */
    class Root : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    private:
        juce::Image scaled;
        float scaledFor = 0.0f;
        int scaledVersion = -1;
    };

    /** A tab drawn in the style of the art's tabs (for TIMELINE, which the mockup doesn't have). */
    class CodeTab : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        CodeTab (juce::Rectangle<int> b, juce::String t) : text (std::move (t)) { setBounds (b); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void setActive (bool a) { if (a != active) { active = a; repaint(); } }
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
        juce::String text;
        bool active = false, icon = true;
    };

    /** Small undo / redo arrow buttons. */
    class ArrowButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ArrowButton (juce::Rectangle<int> b, bool isRedo) : redo (isRedo) { setBounds (b); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void setAvailable (bool a) { if (a != available) { available = a; repaint(); } }
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && available && onClick) onClick(); }
        bool redo, available = false;
    };

    /** KEY readout in the Detection panel (click to override). */
    class KeyBox : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        KeyBox() { setBounds (352, 904, 124, 22); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void setText (const juce::String& t, bool tuning) { if (t != text || tuning != on) { text = t; on = tuning; repaint(); } }
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
        juce::String text;
        bool on = false;
    };

    class Bubble : public juce::Component
    {
    public:
        Bubble() { setInterceptsMouseClicks (false, false); }
        void show (juce::Component& near, const juce::String& text);
        void paint (juce::Graphics&) override;
        juce::String text;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // theme switched
    void toggleTheme();
    void refreshAll();
    void refreshTransport();
    void refreshDetection();
    void edited();            // moves / params changed (marks preset dirty)
    void zoneEdited();        // zone / clip settings changed

    juce::String presetDisplayName() const;
    std::vector<km::Preset> allPresets() const;
    void stepPreset (int delta);
    void showBrowseMenu (juce::Component* target);
    void saveAs();
    void askClipBar();
    void savePreset();
    void showGearMenu();
    void showLoadMenu (juce::Component* target);
    void chooseFile (int slot);
    void loadFile (const juce::File&, int slot);
    void doRoll();
    void afterRestore();   // after undo / redo / A-B switching
    void doExport (bool forDrag, const juce::MouseEvent* dragEvent);
    void toggleSync();
    void setTab (int);
    void askNumber (const juce::String& title, const juce::String& message, const juce::String& initial,
                    std::function<void (const juce::String&)> onOk);

    KoolaidProcessor& proc;
    skin::KoolLookAndFeel laf;
    Root root;
    juce::TooltipWindow tooltips { &root, 700 };

    using Btn = skin::SkinButton;
    using Box = skin::SkinValueBox;

    // header
    Btn step1 { { 456, 165, 200, 66 }, 12.0f }, step2 { { 694, 165, 206, 66 }, 12.0f },
        step3 { { 935, 165, 202, 66 }, 12.0f }, step4 { { 1169, 165, 218, 66 }, 12.0f };

    // preset bar
    Btn presetPrev { { 230, 268, 32, 30 } }, presetNext { { 523, 268, 33, 30 } };
    Box presetName { { 262, 268, 261, 30 }, { 268, 272, 250, 23 }, 21.0f, juce::Justification::centredLeft };
    Btn saveBtn { { 611, 268, 71, 30 } }, saveAsBtn { { 709, 268, 86, 30 } }, browseBtn { { 811, 268, 120, 30 } };
    Box tempoBox { { 1049, 268, 84, 30 }, { 1053, 272, 76, 23 }, 21.0f };
    Btn syncBtn { { 1140, 268, 66, 30 } };
    Box timeSigBox { { 1270, 268, 87, 30 }, { 1274, 272, 58, 23 }, 21.0f };
    Btn gearBtn { { 1386, 266, 36, 34 } };

    // waveform panel
    Btn tab1 { { 30, 322, 186, 27 } }, tab2 { { 222, 322, 219, 27 } }, tab3 { { 451, 322, 135, 27 } };
    CodeTab timelineTab { { 594, 322, 150, 27 }, "TIMELINE" };
    CodeTab abA { { 752, 322, 38, 27 }, "A" }, abB { { 794, 322, 38, 27 }, "B" }, abCopy { { 836, 322, 96, 27 }, "COPY TO B" };
    ArrowButton undoBtn { { 362, 590, 32, 24 }, false }, redoBtn { { 398, 590, 32, 24 }, true };
    Box barsBox { { 930, 320, 268, 28 }, { 940, 322, 252, 24 }, 20.0f };
    Btn zoomIn { { 1220, 321, 38, 28 } }, zoomOut { { 1269, 321, 36, 28 } }, zoomFit { { 1317, 321, 41, 28 } }, captureBtn { { 1370, 321, 44, 28 } };
    WaveformView waveform { proc };
    Btn playBtn { { 30, 524, 70, 38 } }, stopBtn { { 111, 524, 53, 38 } }, loopBtn { { 173, 524, 53, 38 } },
        prevBtn { { 236, 524, 53, 38 } }, nextBtn { { 297, 524, 53, 38 } };
    Box playbackBox { { 478, 529, 228, 33 }, { 482, 533, 198, 25 }, 20.0f, juce::Justification::centredLeft };
    Box lengthBox { { 825, 529, 109, 33 }, { 829, 533, 72, 25 }, 20.0f, juce::Justification::centredLeft };
    Box dropAtBox { { 1050, 529, 136, 33 }, { 1054, 533, 106, 25 }, 20.0f, juce::Justification::centredLeft };
    Btn syncSessionBtn { { 1229, 527, 182, 38 } };

    // stack
    MoveStackView stack { proc };
    Btn addMoveBtn { { 32, 831, 140, 31 } }, duplicateBtn { { 186, 831, 125, 31 } }, deleteBtn { { 324, 831, 101, 31 } };
    std::unique_ptr<EffectCards> cards;

    // detection
    Btn analyzeBtn { { 36, 952, 188, 75 }, 12.0f };
    KeyBox keyBox;
    void showKeyMenu();
    std::unique_ptr<Btn> detectChecks[3];
    std::unique_ptr<Box> detectText[3];

    // bottom
    Btn rollBtn { { 590, 896, 266, 120 }, 50.0f }, instantBtn { { 630, 1017, 180, 33 }, 12.0f };
    Btn previewBtn { { 964, 914, 187, 66 }, 12.0f }, exportBtn { { 1167, 914, 240, 66 }, 12.0f };
    Box exportRangeBox { { 1036, 1002, 191, 27 }, { 1040, 1005, 160, 21 }, 18.0f, juce::Justification::centredLeft };
    Box formatBox { { 1262, 1002, 145, 27 }, { 1266, 1005, 116, 21 }, 18.0f, juce::Justification::centredLeft };

    Btn manualHotspot { { 22, 262, 106, 46 }, 8.0f };
    Btn mascotBtn { { 96, 10, 336, 238 }, 30.0f };
    ManualView manual;
    Bubble bubble;
    std::unique_ptr<juce::FileChooser> chooser;

    int lastResultVersion = -1;
    bool lastPlaying = false, lastSync = false, lastCapture = false, lastCapturing = false;
    double lastBpm = -1.0;
    int flashCountdown = 0;
    juce::Component::SafePointer<Btn> flashing;
    juce::File lastExport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoolaidEditor)
};
