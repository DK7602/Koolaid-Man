#pragma once

#include "Skin.h"
#include <array>

/** A small pop-over editor shared by:
    - curves: draw a 16-point ramp (drag across the bars)
    - patterns: toggle 16 steps on/off (click / drag to paint)
    It sits over the cards panel; Done or Esc closes it. */
class GridEditor : public juce::Component
{
public:
    GridEditor();
    ~GridEditor() override;

    using Values = std::array<float, 16>;
    void openCurve (const juce::String& title, const Values& values, std::function<void (const Values&)> onChange);
    void openPattern (const juce::String& title, juce::uint32 bits, int numSteps, std::function<void (juce::uint32)> onChange);
    void close();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Chip;
    juce::Rectangle<float> gridArea() const;
    int cellAt (float x) const;
    void applyAt (juce::Point<float>, bool firstClick);
    void commit();
    void layoutChips();

    enum class Mode { curve, pattern } mode = Mode::curve;
    juce::String title;
    Values values {};
    juce::uint32 bits = 0;
    int numSteps = 16;
    bool paintOn = true;
    int lastCell = -1;
    std::function<void (const Values&)> curveChanged;
    std::function<void (juce::uint32)> patternChanged;
    std::vector<std::unique_ptr<Chip>> chips;
};
