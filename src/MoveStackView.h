#pragma once

#include "Skin.h"
#include "PluginProcessor.h"

/** "BUILD THE DROP - MOVES STACK": the ordered list of moves. */
class MoveStackView : public juce::Component
{
public:
    explicit MoveStackView (KoolaidProcessor&);

    void refresh();
    int getSelectedUid() const { return selectedUid; }
    void select (int uid);
    int selectedIndex() const;

    std::function<void()> onStackEdited;      // moves changed
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void showRowMenu (int index);

private:
    juce::Rectangle<float> rowBounds (int visibleRow) const;
    int rowAt (juce::Point<float>) const;
    int maxScroll() const;

    KoolaidProcessor& proc;
    juce::Image patch, scrollPatch;
    int patchVersion = -1;
    int selectedUid = 0;
    int scroll = 0;
    int hoverRow = -1;
    int dragFrom = -1, dragTo = -1;
    bool dragging = false;
    float dragY = 0.0f;

    static constexpr int visibleRows = 6;
};
