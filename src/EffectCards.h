#pragma once

#include "Skin.h"
#include "PluginProcessor.h"

class MoveStackView;

/** The "DROP MOVES & EFFECTS" cards. Each card edits the selected stack move of
    its type (or the first one, or the card's default if the type isn't in the stack). */
class EffectCards
{
public:
    EffectCards (KoolaidProcessor&, MoveStackView&, juce::Component& parent);
    ~EffectCards();

    void refresh();
    void setPage (int);          // 0 = main moves, 1 = build & drop, 2 = fx moves
    int getPage() const { return page; }
    void followSelection();      // shows the page of the selected stack move
    std::function<void()> onPageChanged;

    std::function<void()> onEdited;     // model changed -> render
    std::function<void()> onStackChanged;
    std::function<void (int uid)> onEditPattern;   // Chops / Stutter: open the step-pattern editor
    std::function<void (int uid)> onLoadSample;    // Riser / Crash / Impact / Boom: pick your own sample

    /** The move type of the visible card under a point (in the parent's coordinates), or NumTypes. */
    km::MoveType cardTypeAt (juce::Point<int>) const;
    /** Makes sure a move of this type is in the stack and returns its uid. */
    int ensureInStack (km::MoveType);
    std::function<void (skin::SkinKnob&, const juce::String&, bool)> onBubble;

private:
    struct Card;
    km::Move& target (km::MoveType);
    bool hasInstance (km::MoveType) const;
    bool anyEnabled (km::MoveType) const;
    void togglePower (km::MoveType);
    void showTitleMenu (Card&);
    void showValueMenu (Card&, juce::Component* anchor = nullptr);
    void selectType (km::MoveType);

    KoolaidProcessor& proc;
    MoveStackView& stack;
    std::vector<std::unique_ptr<Card>> cards;
    std::unique_ptr<juce::Component> page2Panel;
    std::vector<std::unique_ptr<juce::Component>> tabs;
    int page = 0;
};
