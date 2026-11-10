#pragma once

#include "Skin.h"

/** The built-in manual: renders docs/MANUAL.md (embedded in the plug-in) in a
    scrollable panel with a clickable table of contents. */
class ManualView : public juce::Component
{
public:
    ManualView();
    ~ManualView() override;

    void open();
    void close();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Parsed manual (exposed for tests). */
    struct Block
    {
        enum Kind { h1, h2, para, bullet, number } kind = para;
        juce::String text, marker;
        int section = 0;
        juce::TextLayout layout;
        float y = 0.0f, h = 0.0f, indent = 0.0f;
    };
    static std::vector<Block> parse (const juce::String& markdown);
    int numSections() const { return (int) sections.size(); }
    void scrollToSection (int index);

private:
    class Content;
    class Toc;
    class Scroller;
    class CloseButton;

    void layoutBlocks (int width);
    int currentSection() const;

    std::vector<Block> blocks;
    std::vector<std::pair<juce::String, float>> sections;   // title, y
    std::unique_ptr<Content> content;
    std::unique_ptr<Toc> toc;
    std::unique_ptr<Scroller> viewport;
    std::unique_ptr<CloseButton> closeButton;
    int laidOutWidth = 0;
};
