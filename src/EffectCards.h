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

    std::function<void()> onEdited;     // model changed -> render
    std::function<void()> onStackChanged;
    std::function<void (skin::SkinKnob&, const juce::String&, bool)> onBubble;

private:
    struct Card;
    km::Move& target (km::MoveType);
    bool hasInstance (km::MoveType) const;
    bool anyEnabled (km::MoveType) const;
    void togglePower (km::MoveType);
    void showTitleMenu (Card&);
    void showValueMenu (Card&);
    void selectType (km::MoveType);

    KoolaidProcessor& proc;
    MoveStackView& stack;
    std::vector<std::unique_ptr<Card>> cards;
};
