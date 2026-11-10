#include "EffectCards.h"
#include "MoveStackView.h"
#include "MoveProcessors.h"

using namespace skin;
using km::MoveType;

namespace
{
    /** Darkens a card when its move is off. */
    class CardShade : public juce::Component
    {
    public:
        explicit CardShade (juce::Rectangle<int> b) { setBounds (b); setInterceptsMouseClicks (false, false); }
        void setOn (bool on) { if (on != isOn) { isOn = on; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            if (isOn) return;
            g.setColour (juce::Colour (0x4a0c0618));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 12.0f);
        }
        bool isOn = true;
    };

    /** Detection-strength meter (the little green bars). */
    class Meter : public juce::Component
    {
    public:
        explicit Meter (juce::Rectangle<int> b) { setBounds (b); patch = Skin::get().makePatch (b); setInterceptsMouseClicks (false, false); }
        void setLevel (float l) { if (std::abs (l - level) > 0.001f) { level = l; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            g.drawImageAt (patch, 0, 0);
            const int bars = 6;
            const int lit = juce::roundToInt (level * bars);
            const float bw = (float) getWidth() / bars;
            for (int i = 0; i < bars; ++i)
            {
                const float h = (float) getHeight() * (0.35f + 0.65f * (float) (i + 1) / bars) - 2.0f;
                juce::Rectangle<float> r (i * bw + 1.0f, (float) getHeight() - h - 1.0f, bw - 2.5f, h);
                if (i < lit)
                {
                    juce::ColourGradient cg (juce::Colour (0xff9dff8a), 0, r.getY(), juce::Colour (0xff1fae4a), 0, r.getBottom(), false);
                    g.setGradientFill (cg);
                }
                else g.setColour (juce::Colour (0x66203528));
                g.fillRoundedRectangle (r, 1.5f);
            }
        }
        float level = 0.0f;
        juce::Image patch;
    };

    /** FILL / GATE / ROLL. */
    class ModeButton : public juce::Component
    {
    public:
        ModeButton (juce::Rectangle<int> b, juce::String t) : text (std::move (t)) { setBounds (b); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void setOn (bool b) { if (b != on) { on = b; repaint(); } }
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (on)
            {
                g.setColour (col::lime.withAlpha (0.35f));
                g.fillRoundedRectangle (r.expanded (1.0f), 7.0f);
                juce::ColourGradient cg (juce::Colour (0xffdcff6a), 0, r.getY(), juce::Colour (0xff7ad61e), 0, r.getBottom(), false);
                g.setGradientFill (cg);
                g.fillRoundedRectangle (r.reduced (1.5f), 6.0f);
                g.setColour (juce::Colour (0xff20330a));
                g.drawRoundedRectangle (r.reduced (1.5f), 6.0f, 1.5f);
                g.setFont (Skin::get().bold (19.0f));
                g.setColour (juce::Colour (0xff142006));
                g.drawText (text, r, juce::Justification::centred);
            }
            else
            {
                juce::ColourGradient cg (juce::Colour (0xff3a2f52), 0, r.getY(), juce::Colour (0xff1d172b), 0, r.getBottom(), false);
                g.setGradientFill (cg);
                g.fillRoundedRectangle (r.reduced (1.5f), 6.0f);
                g.setColour (juce::Colour (0xff8a80aa));
                g.drawRoundedRectangle (r.reduced (1.5f), 6.0f, 1.2f);
                drawShadowText (g, text, r, Skin::get().bold (19.0f));
            }
            if (isMouseOver())
            {
                g.setColour (juce::Colours::white.withAlpha (0.1f));
                g.fillRoundedRectangle (r.reduced (1.5f), 6.0f);
            }
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
        juce::String text;
        bool on = false;
    };

    enum class ValueKind { none, division, semitones };

    juce::String divisionText (int d) { return km::divisions()[(size_t) juce::jlimit (0, 8, d)].label; }
    juce::String semiText (int s)     { return s > 0 ? "+" + juce::String (s) : (s < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (-s) : "0"); }
    const int semitoneChoices[] = { 24, 19, 12, 7, 5, 3, 2, 1, -1, -2, -3, -5, -7, -12, -19, -24 };
}

struct EffectCards::Card
{
    MoveType type;
    juce::Rectangle<int> bounds;
    std::unique_ptr<CardShade> shade;
    std::unique_ptr<SkinButton> title, power;
    std::unique_ptr<SkinKnob> knob, knob2;
    std::unique_ptr<SkinValueBox> value;
    ValueKind kind = ValueKind::none;
    std::unique_ptr<Meter> meter;
    std::vector<std::unique_ptr<ModeButton>> modes;
};

EffectCards::EffectCards (KoolaidProcessor& p, MoveStackView& s, juce::Component& parent) : proc (p), stack (s)
{
    struct Def
    {
        MoveType type;
        juce::Rectangle<int> card, power;
        juce::Point<float> k1, k2;
        juce::Rectangle<int> valueBox;
        ValueKind kind;
        juce::Rectangle<int> meter;
    };
    const juce::Point<float> none { -1.0f, -1.0f };
    const Def defs[] = {
        { MoveType::Reverse,   { 467, 630, 181, 109 }, { 617, 634, 25, 22 }, { 561, 682 }, none, { 566, 709, 62, 22 }, ValueKind::division, {} },
        { MoveType::Chops,     { 661, 630, 181, 109 }, { 809, 634, 25, 22 }, { 751, 687 }, none, { 756, 709, 64, 22 }, ValueKind::division, {} },
        { MoveType::Stutter,   { 852, 630, 183, 109 }, { 1001, 634, 25, 22 }, { 943, 687 }, none, { 949, 709, 66, 22 }, ValueKind::division, {} },
        { MoveType::Pitch,     { 1045, 630, 182, 109 }, { 1193, 634, 25, 22 }, { 1136, 681 }, none, { 1152, 709, 60, 22 }, ValueKind::semitones, {} },
        { MoveType::TapeStop,  { 1235, 630, 183, 109 }, { 1387, 634, 25, 22 }, { 1328, 681 }, none, { 1345, 709, 61, 22 }, ValueKind::division, {} },
        { MoveType::Filter,    { 467, 752, 181, 109 }, { 616, 758, 25, 22 }, { 526, 811 }, { 597, 808 }, {}, ValueKind::none, {} },
        { MoveType::DrumMoves, { 659, 752, 192, 109 }, { 820, 758, 25, 22 }, none, none, {}, ValueKind::none, {} },
        { MoveType::Bass808,   { 861, 752, 176, 109 }, { 1009, 758, 25, 22 }, { 930, 807 }, none, {}, ValueKind::none, { 965, 803, 47, 28 } },
        { MoveType::Hats,      { 1045, 752, 182, 109 }, { 1198, 758, 25, 22 }, { 1121, 808 }, none, {}, ValueKind::none, { 1156, 803, 46, 28 } },
        { MoveType::Atmos,     { 1237, 752, 181, 109 }, { 1390, 758, 25, 22 }, { 1300, 808 }, { 1371, 808 }, {}, ValueKind::none, {} },
    };

    for (auto& d : defs)
    {
        auto c = std::make_unique<Card>();
        auto* card = c.get();
        c->type = d.type;
        c->bounds = d.card;
        c->kind = d.kind;

        c->title = std::make_unique<SkinButton> (juce::Rectangle<int> (d.card.getX() + 6, d.card.getY() + 4, d.power.getX() - d.card.getX() - 10, 28), 6.0f);
        c->title->onClick = [this, card] { showTitleMenu (*card); };
        c->title->setTooltip (juce::String (km::cardTitle (d.type)) + ": click for variations, add/remove from the stack");
        parent.addAndMakeVisible (*c->title);

        const bool sensCard = d.type == MoveType::Bass808 || d.type == MoveType::Hats;
        auto setupKnob = [&] (std::unique_ptr<SkinKnob>& k, juce::Point<float> pos, int which)
        {
            if (pos.x < 0) return;
            k = std::make_unique<SkinKnob> (pos);
            k->onBubble = [this] (SkinKnob& kn, const juce::String& t, bool show) { if (onBubble) onBubble (kn, t, show); };
            if (sensCard)
            {
                k->setDefault (0.55f);
                k->valueText = [] (float v) { return "SENS " + juce::String (juce::roundToInt (v * 100)) + "%"; };
                k->onChange = [this, card] (float v)
                {
                    auto& m = proc.getModel();
                    (card->type == MoveType::Bass808 ? m.sens808 : m.sensHats) = v;
                    if (onEdited) onEdited();
                };
                k->setTooltip ("Detection sensitivity: how easily " + juce::String (d.type == MoveType::Bass808 ? "808s / kicks" : "hi-hats")
                               + " are found in your audio. The meter shows what was found.");
            }
            else
            {
                const auto def = km::defaultMove (d.type);
                k->setDefault (which == 0 ? def.knob : def.knob2);
                k->valueText = [this, card, which] (float) { return km::knobName (card->type, which).toUpperCase() + " " + km::knobValueText (target (card->type), which); };
                k->onChange = [this, card, which] (float v)
                {
                    auto& m = target (card->type);
                    (which == 0 ? m.knob : m.knob2) = v;
                    if (onEdited) onEdited();
                    if (onStackChanged) onStackChanged();
                };
                k->setTooltip (juce::String (km::cardTitle (d.type)) + " " + km::knobName (d.type, which).toUpperCase() + " (drag, double-click to reset)");
            }
            parent.addAndMakeVisible (*k);
        };
        setupKnob (c->knob, d.k1, 0);
        setupKnob (c->knob2, d.k2, 1);

        if (! d.valueBox.isEmpty())
        {
            const auto b = d.valueBox;
            c->value = std::make_unique<SkinValueBox> (b, juce::Rectangle<int> (b.getX() + 3, b.getY() + 3, b.getWidth() - 23, b.getHeight() - 6), 18.0f);
            c->value->setFont (Skin::get().wideBold (16.5f));
            c->value->onClick = [this, card] { showValueMenu (*card); };
            c->value->onWheel = [this, card] (float dy)
            {
                auto& m = target (card->type);
                if (card->kind == ValueKind::division)
                    m.division = juce::jlimit (0, 8, m.division + (dy > 0 ? 1 : -1));
                else
                {
                    int idx = 0;
                    for (int i = 0; i < 16; ++i) if (semitoneChoices[i] == m.semitones) idx = i;
                    m.semitones = semitoneChoices[juce::jlimit (0, 15, idx + (dy > 0 ? -1 : 1))];
                }
                if (onEdited) onEdited();
                if (onStackChanged) onStackChanged();
                refresh();
            };
            c->value->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            c->value->setTooltip (d.kind == ValueKind::semitones ? "Pitch target in semitones" : "Length / rate");
            parent.addAndMakeVisible (*c->value);
        }

        if (! d.meter.isEmpty())
        {
            c->meter = std::make_unique<Meter> (d.meter);
            parent.addAndMakeVisible (*c->meter);
        }

        if (d.type == MoveType::DrumMoves)
        {
            const juce::Rectangle<int> r[] = { { 667, 810, 53, 28 }, { 729, 810, 53, 28 }, { 791, 810, 52, 28 } };
            const char* names[] = { "FILL", "GATE", "ROLL" };
            const char* tips[] = { "Snare fill: your snare in 1/4 > 1/8 > 1/16 > 1/32 into the drop",
                                   "Gate the build to the drum hits found in your audio",
                                   "Snare roll: steady 1/16s with a crescendo and rising pitch" };
            for (int i = 0; i < 3; ++i)
            {
                auto mb = std::make_unique<ModeButton> (r[i], names[i]);
                mb->setHelpText (tips[i]);
                mb->onClick = [this, i]
                {
                    auto& m = target (MoveType::DrumMoves);
                    m.mode = i;
                    if (! hasInstance (MoveType::DrumMoves)) togglePower (MoveType::DrumMoves);
                    if (onEdited) onEdited();
                    if (onStackChanged) onStackChanged();
                    refresh();
                };
                parent.addAndMakeVisible (*mb);
                c->modes.push_back (std::move (mb));
            }
        }

        c->shade = std::make_unique<CardShade> (d.card);
        parent.addAndMakeVisible (*c->shade);

        c->power = std::make_unique<SkinButton> (d.power, 5.0f);
        c->power->onClick = [this, card] { togglePower (card->type); };
        c->power->setTooltip ("Turn " + juce::String (km::typeName (d.type)) + " on/off (adds it to the stack if it isn't there)");
        parent.addAndMakeVisible (*c->power);

        cards.push_back (std::move (c));
    }
    refresh();
}

EffectCards::~EffectCards() = default;

bool EffectCards::hasInstance (MoveType t) const
{
    for (auto& m : proc.getModel().moves)
        if (m.type == t) return true;
    return false;
}

bool EffectCards::anyEnabled (MoveType t) const
{
    for (auto& m : proc.getModel().moves)
        if (m.type == t && m.enabled) return true;
    return false;
}

km::Move& EffectCards::target (MoveType t)
{
    auto& model = proc.getModel();
    const int sel = stack.selectedIndex();
    if (sel >= 0 && model.moves[(size_t) sel].type == t)
        return model.moves[(size_t) sel];
    for (auto& m : model.moves)
        if (m.type == t) return m;
    return model.templates[(size_t) t];
}

void EffectCards::selectType (MoveType t)
{
    const int sel = stack.selectedIndex();
    auto& moves = proc.getModel().moves;
    if (sel >= 0 && moves[(size_t) sel].type == t) return;
    for (auto& m : moves)
        if (m.type == t) { stack.select (m.uid); return; }
}

void EffectCards::togglePower (MoveType t)
{
    auto& model = proc.getModel();
    if (hasInstance (t))
    {
        auto& m = target (t);
        m.enabled = ! m.enabled;
        selectType (t);
    }
    else
    {
        auto m = model.templates[(size_t) t];
        m.enabled = true;
        m.uid = 0;
        m.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (1 << 30);
        auto pos = model.moves.begin();
        while (pos != model.moves.end() && km::canonicalOrder (pos->type) <= km::canonicalOrder (t)) ++pos;
        model.moves.insert (pos, m);
        proc.modelChanged (true);   // assigns the uid
        selectType (t);
    }
    if (onEdited) onEdited();
    if (onStackChanged) onStackChanged();
    refresh();
}

void EffectCards::showTitleMenu (Card& card)
{
    selectType (card.type);
    const auto t = card.type;
    auto& m = target (t);
    juce::PopupMenu menu;
    menu.addSectionHeader (km::cardTitle (t));
    const auto modes = km::modeNames (t);
    for (int i = 0; i < modes.size(); ++i)
        menu.addItem (100 + i, modes[i], true, m.mode == i);
    menu.addSeparator();
    if (hasInstance (t))
    {
        menu.addItem (1, "Add Another " + juce::String (km::typeName (t)));
        menu.addItem (2, "Remove From Stack");
    }
    else
        menu.addItem (1, "Add To Stack");

    menu.showMenuAsync (menuOptionsFor (card.title.get()), [this, t] (int r)
    {
        if (r == 0) return;
        auto& model = proc.getModel();
        if (r >= 100)
        {
            target (t).mode = r - 100;
            if (! hasInstance (t)) { togglePower (t); return; }
        }
        else if (r == 1)
        {
            auto m = hasInstance (t) ? target (t) : model.templates[(size_t) t];
            m.uid = 0; m.enabled = true;
            auto pos = model.moves.begin();
            while (pos != model.moves.end() && km::canonicalOrder (pos->type) <= km::canonicalOrder (t)) ++pos;
            model.moves.insert (pos, m);
            proc.modelChanged (true);
        }
        else if (r == 2)
        {
            const int sel = stack.selectedIndex();
            if (sel >= 0 && model.moves[(size_t) sel].type == t)
                model.moves.erase (model.moves.begin() + sel);
            else
                for (auto it = model.moves.begin(); it != model.moves.end(); ++it)
                    if (it->type == t) { model.moves.erase (it); break; }
        }
        if (onEdited) onEdited();
        if (onStackChanged) onStackChanged();
        refresh();
    });
}

void EffectCards::showValueMenu (Card& card)
{
    selectType (card.type);
    auto& m = target (card.type);
    juce::PopupMenu menu;
    if (card.kind == ValueKind::division)
    {
        menu.addSectionHeader (card.type == MoveType::Chops || card.type == MoveType::Stutter ? "RATE" : "LENGTH");
        for (int i = 0; i < (int) km::divisions().size(); ++i)
            menu.addItem (1 + i, km::divisions()[(size_t) i].longLabel, true, m.division == i);
    }
    else
    {
        menu.addSectionHeader ("SEMITONES");
        for (int i = 0; i < 16; ++i)
            menu.addItem (1000 + semitoneChoices[i] + 100, semiText (semitoneChoices[i]) + " st", true, m.semitones == semitoneChoices[i]);
    }
    const auto t = card.type;
    menu.showMenuAsync (menuOptionsFor (card.value.get()), [this, t] (int r)
    {
        if (r == 0) return;
        auto& mv = target (t);
        if (r >= 1000) mv.semitones = r - 1100;
        else mv.division = r - 1;
        if (onEdited) onEdited();
        if (onStackChanged) onStackChanged();
        refresh();
    });
}

void EffectCards::refresh()
{
    const auto analysis = proc.getAnalysis();
    for (auto& c : cards)
    {
        const bool on = anyEnabled (c->type);
        c->power->setLit (on);
        c->shade->setOn (on);
        auto& m = target (c->type);

        if (c->type == MoveType::Bass808 || c->type == MoveType::Hats)
        {
            auto& model = proc.getModel();
            if (c->knob && ! c->knob->isDragging())
                c->knob->setValue (c->type == MoveType::Bass808 ? model.sens808 : model.sensHats);
            if (c->meter)
            {
                float level = 0.0f;
                if (analysis != nullptr)
                    level = c->type == MoveType::Bass808 ? analysis->bass.confidence : analysis->hats.confidence;
                c->meter->setLevel (level);
            }
        }
        else
        {
            if (c->knob && ! c->knob->isDragging())  c->knob->setValue (m.knob);
            if (c->knob2 && ! c->knob2->isDragging()) c->knob2->setValue (m.knob2);
        }
        if (c->value)
            c->value->setText (c->kind == ValueKind::semitones ? semiText (m.semitones) : divisionText (m.division));
        for (size_t i = 0; i < c->modes.size(); ++i)
            c->modes[i]->setOn ((int) i == m.mode);

        const auto modes = km::modeNames (c->type);
        c->title->setTooltip (juce::String (km::cardTitle (c->type)) + " - " + modes[juce::jlimit (0, modes.size() - 1, m.mode)]
                              + " (click for variations)");
    }
}
