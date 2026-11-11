#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** The skin is the mockup artwork (1448 x 1086 design pixels). Every live
    control is a component placed in those design coordinates on top of it. */
namespace skin
{

constexpr int designW = 1448;
constexpr int designH = 1086;

enum class Theme { dark = 0, light = 1 };

/** Accent colours are fixed; the rest follow the light/dark theme (set by Skin::setTheme). */
namespace col
{
    const juce::Colour pink      { 0xffff4fb4 };
    const juce::Colour hotPink   { 0xffff2d9b };
    const juce::Colour cyan      { 0xff35e6f2 };
    const juce::Colour lime      { 0xffb4f53c };
    const juce::Colour yellow    { 0xffffe14a };

    inline juce::Colour text      { 0xfff7f3ff };
    inline juce::Colour textDim   { 0xff9a90b8 };
    inline juce::Colour shadow    { 0x8c000000 };
    inline juce::Colour lavender  { 0xff9687ea };
    inline juce::Colour menuBg    { 0xff1c1233 };
    inline juce::Colour menuBg2   { 0xff140d26 };
    inline juce::Colour display   { 0xff0e111e };   // waveform display background
    inline juce::Colour gridLine  { 0x0fffffff };
    inline juce::Colour waveOut   { 0xff9887ea };
    inline juce::Colour waveIn    { 0xffff6ccb };
    inline juce::Colour zoneTop   { 0xc0781a5c };
    inline juce::Colour zoneBot   { 0xc0501040 };
    inline juce::Colour pill      { 0xe0120a22 };
    inline juce::Colour rowTop    { 0xff342a4c };
    inline juce::Colour rowBot    { 0xff231c35 };
    inline juce::Colour rowSelTop { 0xff4a3170 };
    inline juce::Colour rowSelBot { 0xff2f1d4a };
    inline juce::Colour rowBorder { 0xff5d4f80 };
    inline juce::Colour rowHover  { 0xff8d7bb8 };
    inline juce::Colour grip      { 0xebffffff };
    inline juce::Colour boxOff    { 0xff1a1426 };
    inline juce::Colour track     { 0xff15111f };
    inline juce::Colour thumb     { 0xffb2a6d6 };
    inline juce::Colour cardShade { 0x4a0c0618 };
    inline juce::Colour panelTop  { 0xff23173f };   // manual / dialogs
    inline juce::Colour panelBot  { 0xff100a1f };
}

class Skin : public juce::ChangeBroadcaster
{
public:
    static Skin& get();

    const juce::Image& image() const     { return theme == Theme::light ? artLight : art; }
    const juce::Image& offImage() const  { return theme == Theme::light ? offLight : off; }

    /** Light / dark. Global preference: remembered for next time and shared by
        every open Koolaid Man window. Listeners are told when it changes. */
    Theme getTheme() const { return theme; }
    void setTheme (Theme);
    int version() const { return themeVersion; }   // bumps on every theme change

    /** Background fill for erasing baked-in text: each row is interpolated
        between the pixel just left and just right of the rectangle. */
    juce::Image makePatch (juce::Rectangle<int> r) const;
    juce::Colour pixel (int x, int y) const;

    juce::Font medium (float h) const;
    juce::Font semibold (float h) const;
    juce::Font bold (float h) const;
    juce::Font heavyItalic (float h) const;
    juce::Font wide (float h) const;       // regular-width semibold (values, list text)
    juce::Font wideBold (float h) const;

private:
    Skin();
    void applyColours();
    juce::Image art, off, artLight, offLight;
    Theme theme = Theme::dark;
    int themeVersion = 0;
    std::unique_ptr<juce::PropertiesFile> prefs;
    juce::Typeface::Ptr tfMedium, tfSemi, tfBold, tfHeavyItalic, tfWide, tfWideBold;
};

/** Neon-90s look for popup menus, tooltips and text editors. */
class KoolLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KoolLookAndFeel();
    void applyTheme();
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

void drawShadowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font,
                     juce::Justification = juce::Justification::centred, juce::Colour = col::text,
                     juce::Colour shadowColour = col::shadow);

juce::PopupMenu::Options menuOptionsFor (juce::Component* target);

//==============================================================================
/** An invisible hotspot over a button painted in the artwork. Adds hover/press
    feedback, and when "lit" is false shows the greyed-out version of the art. */
class SkinButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    SkinButton (juce::Rectangle<int> designBounds, float cornerRadius = 6.0f);

    std::function<void()> onClick;
    std::function<void (const juce::MouseEvent&)> onDragOut;   // drag gesture leaving the button
    std::function<void()> onRightClick;                        // right-click / ctrl-click (optional)

    void setLit (bool);
    bool isLit() const { return lit; }
    void setLitRegion (juce::Rectangle<int> designRegion) { litRegion = designRegion; repaint(); }
    void setShowHover (bool b) { showHover = b; }
    /** Draw this button from another spot of the artwork (lit = art, off = greyed art). */
    void setArtSource (juce::Rectangle<int> designRegion) { artSource = designRegion; repaint(); }
    void setGlow (juce::Colour c) { glow = c; repaint(); }   // transparent = none
    void setFlash (bool b) { flash = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

protected:
    float radius;
    bool lit = true, flash = false, dragging = false, showHover = true, popupClick = false;
    juce::Rectangle<int> litRegion;   // empty = whole button
    juce::Rectangle<int> artSource;   // empty = the art under the button
    juce::Colour glow = juce::Colours::transparentBlack;
};

/** Erases the baked-in value text of a box in the art and draws live text. */
class SkinValueBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    SkinValueBox (juce::Rectangle<int> designBounds, juce::Rectangle<int> eraseArea, float fontHeight,
                  juce::Justification just = juce::Justification::centred);

    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setTextColour (juce::Colour c) { colour = c; customColour = true; repaint(); }
    /** Keep white-on-dark text whatever the theme (for boxes inside the coloured cards). */
    void setFixedDarkStyle() { colour = juce::Colour (0xfff7f3ff); customColour = true; fixedDark = true; repaint(); }
    void setFont (juce::Font f) { font = f; repaint(); }
    const juce::String& getText() const { return text; }

    std::function<void()> onClick;
    std::function<void (float deltaY)> onDrag;      // vertical drag (e.g. tempo)
    std::function<void()> onDoubleClick;
    std::function<void (float)> onWheel;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<int> erase;    // local coords
    juce::Image patch;
    juce::Rectangle<int> eraseDesign;
    int patchVersion = -1;
    juce::String text;
    juce::Font font;
    juce::Justification justification;
    juce::Colour colour;
    bool customColour = false, fixedDark = false;
    float lastDragY = 0.0f;
    bool dragged = false;
};

/** A knob drawn over the knob face in the art (tinted to match its card). */
class SkinKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    SkinKnob (juce::Point<float> designCentre, float faceRadius = 15.5f);

    void setValue (float v, bool notify = false);
    float getValue() const { return value; }
    void setDefault (float d) { defaultValue = d; }
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    void setTint (juce::Colour c) { tint = c; repaint(); }

    std::function<void (float)> onChange;
    std::function<juce::String (float)> valueText;   // for the bubble while dragging
    std::function<void()> onGestureEnd;
    std::function<void (SkinKnob&, const juce::String&, bool show)> onBubble;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    bool isDragging() const { return dragging; }

private:
    float value = 0.5f, defaultValue = 0.5f, dragStartValue = 0.5f;
    float faceR;
    juce::Colour tint;
    bool dragging = false, dimmed = false;
};

} // namespace skin
