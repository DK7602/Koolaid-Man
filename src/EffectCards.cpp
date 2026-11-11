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
            g.setColour (col::cardShade);
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
                drawShadowText (g, text, r, Skin::get().bold (19.0f), juce::Justification::centred,
                                juce::Colour (0xfff7f3ff), juce::Colour (0x8c000000));   // on the card: always light-on-dark
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


    //==========================================================================
    // Page 2 ("BUILD & DROP") cards, built from the artwork's own card
    //==========================================================================
    constexpr float sourceHue = 0.74f;   // the Atmos card the wide cards are made from

    juce::Colour shiftHue (juce::Colour c, float targetHue)
    {
        if (c.getSaturation() < 0.10f) return c;
        float d = c.getHue() - sourceHue;
        if (d > 0.5f) d -= 1.0f;
        if (d < -0.5f) d += 1.0f;
        if (std::abs (d) > 0.16f) return c;            // keep the green power button, white, etc.
        return c.withHue (std::fmod (targetHue + d + 1.0f, 1.0f));
    }

    /** Builds a wide card (w x h + 4 px glow margin, in design pixels) from the Atmos card art.
        k = 1 builds it at design size from the 1x art, k = 2 at twice the size from the 2x art. */
    juce::Image buildWideCard (int w, int h, float hue, juce::Point<int> knobAt, int k)
    {
        const auto& art = k == 2 ? Skin::get().image2x() : Skin::get().image();
        const juce::Rectangle<int> src (1233 * k, 748 * k, 189 * k, 117 * k);   // Atmos card + 4 px margin
        auto s = art.getClippedImage (src).createCopy().convertedToFormat (juce::Image::ARGB);
        const int sw = s.getWidth(), sh = s.getHeight();
        const int mx = 13 * k, my = 9 * k;
        {
            // clear the card's contents, keeping its border, gradient and power button
            juce::Image::BitmapData bd (s, juce::Image::BitmapData::readWrite);
            const juce::Rectangle<int> power ((1390 - 1233 - 3) * k, (758 - 748 - 3) * k, 31 * k, 28 * k);
            // each row's typical background colour (median brightness), then smoothed
            // vertically, so clearing the title / icon / knobs leaves no streaks or bands
            std::vector<juce::Colour> rowCol ((size_t) sh);
            for (int y = my; y < sh - my; ++y)
            {
                std::vector<juce::Colour> row;
                for (int x = mx; x < sw - mx; ++x)
                    if (! power.contains (x, y)) row.push_back (bd.getPixelColour (x, y));
                std::sort (row.begin(), row.end(), [] (juce::Colour a, juce::Colour b) { return a.getPerceivedBrightness() < b.getPerceivedBrightness(); });
                rowCol[(size_t) y] = row.empty() ? bd.getPixelColour (mx - 1, y) : row[row.size() / 2];
            }
            std::vector<juce::Colour> smooth ((size_t) sh);
            for (int y = my; y < sh - my; ++y)
            {
                float r = 0, g = 0, b = 0; int cnt = 0;
                for (int j = juce::jmax (my, y - 7 * k); j <= juce::jmin (sh - my - 1, y + 7 * k); ++j)
                {
                    r += rowCol[(size_t) j].getFloatRed(); g += rowCol[(size_t) j].getFloatGreen(); b += rowCol[(size_t) j].getFloatBlue(); ++cnt;
                }
                smooth[(size_t) y] = juce::Colour::fromFloatRGBA (r / cnt, g / cnt, b / cnt, 1.0f);
            }
            for (int y = my; y < sh - my; ++y)
                for (int x = mx; x < sw - mx; ++x)
                    if (! power.contains (x, y))
                    {
                        // blend into the border over a few pixels so the bevel stays intact
                        const int edge = juce::jmin (x - mx, sw - mx - 1 - x, y - my, sh - my - 1 - y);
                        const float a = juce::jlimit (0.0f, 1.0f, (edge + 1) / (4.0f * (float) k));
                        bd.setPixelColour (x, y, bd.getPixelColour (x, y).interpolatedWith (smooth[(size_t) y], a));
                    }
        }
        const int W = (w + 8) * k, H = (h + 8) * k;
        juce::Image out (juce::Image::ARGB, W, H, true);
        {
            juce::Graphics g (out);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            const int left = 28 * k, right = 40 * k;   // 9-slice: keep the corners and the power button
            g.drawImage (s, 0, 0, left, H, 0, 0, left, sh);
            g.drawImage (s, left, 0, W - left - right, H, left, 0, sw - left - right, sh);
            g.drawImage (s, W - right, 0, right, H, sw - right, 0, right, sh);
            // the knob ring (with its tick marks) from the Atmos SIZE knob
            auto ring = art.getClippedImage ({ 1273 * k, 781 * k, 54 * k, 54 * k });
            juce::Path circle;
            circle.addEllipse ((float) ((knobAt.x - 27) * k), (float) ((knobAt.y - 27) * k), 54.0f * k, 54.0f * k);
            g.saveState();
            g.reduceClipRegion (circle);
            g.drawImageAt (ring, (knobAt.x - 27) * k, (knobAt.y - 27) * k);
            g.restoreState();
        }
        juce::Image::BitmapData bd (out, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                bd.setPixelColour (x, y, shiftHue (bd.getPixelColour (x, y), hue));
        return out;
    }

    void drawIcon (juce::Graphics& g, MoveType t, juce::Rectangle<float> r)
    {
        juce::Path p;
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        switch (t)
        {
            case MoveType::Riser:     // rising zig-zag with an arrow
                p.startNewSubPath (x, y + h * 0.95f);
                p.lineTo (x + w * 0.3f, y + h * 0.6f); p.lineTo (x + w * 0.45f, y + h * 0.75f); p.lineTo (x + w * 0.8f, y + h * 0.25f);
                p = p.createPathWithRoundedCorners (2.0f);
                { juce::Path s; juce::PathStrokeType (4.0f).createStrokedPath (s, p); p = s; }
                p.addTriangle (x + w * 0.62f, y + h * 0.18f, x + w, y, x + w * 0.9f, y + h * 0.42f);
                break;
            case MoveType::RevCrash:  // cymbal with a back arrow
                p.addEllipse (x + w * 0.05f, y + h * 0.3f, w * 0.9f, h * 0.32f);
                p.addRectangle (x + w * 0.46f, y + h * 0.55f, w * 0.08f, h * 0.45f);
                p.addTriangle (x + w * 0.1f, y + h * 0.1f, x + w * 0.35f, y, x + w * 0.35f, y + h * 0.22f);
                p.addRectangle (x + w * 0.33f, y + h * 0.07f, w * 0.45f, h * 0.08f);
                break;
            case MoveType::Pump:      // ducking envelope
                for (int k = 0; k < 3; ++k)
                {
                    const float x0 = x + k * w / 3.0f;
                    p.addTriangle (x0, y + h, x0, y + h * 0.15f, x0 + w / 3.0f, y + h);
                }
                break;
            case MoveType::Gap:       // pause bars
                p.addRoundedRectangle (x + w * 0.15f, y, w * 0.25f, h, 2.0f);
                p.addRoundedRectangle (x + w * 0.6f, y, w * 0.25f, h, 2.0f);
                break;
            case MoveType::Impact:    // starburst
                p.addStar ({ x + w * 0.5f, y + h * 0.5f }, 8, h * 0.22f, h * 0.55f, 0.2f);
                break;
            case MoveType::Boom:      // speaker
                p.addEllipse (x + w * 0.1f, y, h, h);
                p.addEllipse (x + w * 0.1f + h * 0.3f, y + h * 0.3f, h * 0.4f, h * 0.4f);
                p.setUsingNonZeroWinding (false);
                p.addRectangle (x + w * 0.1f + h + 2.0f, y + h * 0.42f, w - h - w * 0.1f - 2.0f, h * 0.16f);
                break;
            case MoveType::DelayThrow: // echoes getting smaller
                for (int k = 0; k < 4; ++k)
                {
                    const float r = h * (0.5f - 0.09f * k);
                    p.addEllipse (x + w * (0.12f + 0.26f * k) - r * 0.5f, y + h * 0.5f - r * 0.5f, r, r);
                }
                break;
            case MoveType::Bitcrush:  // staircase
                for (int k = 0; k < 5; ++k)
                    p.addRectangle (x + w * 0.2f * k, y + h * (0.8f - 0.18f * k), w * 0.2f + 0.5f, h * (0.2f + 0.18f * k));
                break;
            case MoveType::Phaser:    // rising stripes (barber pole)
                for (int k = 0; k < 4; ++k)
                {
                    const float x0 = x + w * (0.05f + 0.24f * k);
                    p.startNewSubPath (x0, y + h);
                    p.lineTo (x0 + w * 0.12f, y + h);
                    p.lineTo (x0 + w * 0.3f, y);
                    p.lineTo (x0 + w * 0.18f, y);
                    p.closeSubPath();
                }
                break;
            case MoveType::Width:     // arrows out
                p.addTriangle (x, y + h * 0.5f, x + w * 0.3f, y + h * 0.1f, x + w * 0.3f, y + h * 0.9f);
                p.addTriangle (x + w, y + h * 0.5f, x + w * 0.7f, y + h * 0.1f, x + w * 0.7f, y + h * 0.9f);
                p.addRectangle (x + w * 0.28f, y + h * 0.4f, w * 0.44f, h * 0.2f);
                break;
            case MoveType::Scratch:   // record
                p.addEllipse (x + w * 0.5f - h * 0.55f, y - h * 0.05f, h * 1.1f, h * 1.1f);
                p.addEllipse (x + w * 0.5f - h * 0.18f, y + h * 0.32f, h * 0.36f, h * 0.36f);
                p.setUsingNonZeroWinding (false);
                break;
            case MoveType::PreVerb:   // swell into a hit
                p.startNewSubPath (x, y + h * 0.95f);
                p.quadraticTo (x + w * 0.55f, y + h * 0.9f, x + w * 0.72f, y + h * 0.05f);
                p.lineTo (x + w * 0.72f, y + h * 0.95f);
                p.closeSubPath();
                p.addRectangle (x + w * 0.82f, y, w * 0.14f, h * 0.95f);
                break;
            default: break;
        }
        g.setColour (juce::Colour (0xaa1a0b2a));
        g.strokePath (p, juce::PathStrokeType (2.5f), juce::AffineTransform::translation (1.0f, 1.5f));
        g.setColour (juce::Colour (0xfffdf6ff));
        g.fillPath (p);
    }

    void drawValueBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool hover)
    {
        g.setColour (juce::Colour (0x66000000));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 5.0f);
        juce::ColourGradient bg (juce::Colour (0xff22202a), 0, r.getY(), juce::Colour (0xff0b0a10), 0, r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff6f6a82));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        if (hover) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (r, 5.0f); }
        auto tr = r.reduced (8.0f, 0.0f).withTrimmedRight (14.0f);
        g.setFont (Skin::get().wideBold (15.5f));
        g.setColour (juce::Colour (0x8c000000));
        g.drawFittedText (text, tr.translated (1.0f, 1.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (juce::Colour (0xfff7f3ff));
        g.drawFittedText (text, tr.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        juce::Path tri;
        const float ax = r.getRight() - 12.0f, ay = r.getCentreY();
        tri.addTriangle (ax - 5.0f, ay - 2.5f, ax + 5.0f, ay - 2.5f, ax, ay + 3.5f);
        g.fillPath (tri);
    }

    /** The baked wide card plus its title, icon and labels. */
    class WideCard : public juce::Component
    {
    public:
        WideCard (juce::Rectangle<int> card, MoveType t, float h, juce::String knobLabel, juce::String valueLabel)
            : type (t), hue (h), knobText (std::move (knobLabel)), valueText (std::move (valueLabel))
        {
            setBounds (card.expanded (4));
            setInterceptsMouseClicks (false, false);
        }
        juce::Colour faceTint()
        {
            ensure (scale);
            return image.getPixelAt ((4 + 62 - 7) * scale, (4 + 66 - 8) * scale);
        }
        void paint (juce::Graphics& g) override
        {
            ensure (Skin::wantsHiRes (g) ? 2 : 1);   // built from the 2x art on HiDPI screens / big windows
            if (scale == 1)
                g.drawImageAt (image, 0, 0);
            else
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (image, getLocalBounds().toFloat());
            }
            const auto& sk = Skin::get();
            drawIcon (g, type, { 16.0f, 12.0f, 30.0f, 22.0f });
            drawShadowText (g, km::cardTitle (type), { 54.0f, 9.0f, (float) getWidth() - 100.0f, 28.0f }, sk.bold (21.0f),
                            juce::Justification::centredLeft, juce::Colour (0xfffdf6ff), juce::Colour (0x8c000000));
            drawShadowText (g, knobText, { 4.0f + 62.0f - 40.0f, 4.0f + 90.0f, 80.0f, 18.0f }, sk.bold (17.0f),
                            juce::Justification::centred, juce::Colour (0xfffdf6ff), juce::Colour (0x8c000000));
            drawShadowText (g, "TYPE", { 4.0f + 104.0f, 4.0f + 40.0f, 66.0f, 24.0f }, sk.bold (17.0f),
                            juce::Justification::centredRight, juce::Colour (0xfffdf6ff), juce::Colour (0x8c000000));
            drawShadowText (g, valueText, { 4.0f + 104.0f, 4.0f + 72.0f, 66.0f, 24.0f }, sk.bold (17.0f),
                            juce::Justification::centredRight, juce::Colour (0xfffdf6ff), juce::Colour (0x8c000000));
        }
    private:
        void ensure (int k)
        {
            if (version != Skin::get().version() || ! image.isValid() || k != scale)
            {
                image = buildWideCard (getWidth() - 8, getHeight() - 8, hue, { 4 + 62, 4 + 66 }, k);
                version = Skin::get().version();
                scale = k;
            }
        }
        int scale = 1;
        MoveType type;
        float hue;
        juce::String knobText, valueText;
        juce::Image image;
        int version = -1;
    };

    /** A dropdown value box drawn in the style of the art's boxes. */
    class ArtBox : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ArtBox (juce::Rectangle<int> b) { setBounds (b); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        std::function<void()> onClick;
        std::function<void (float)> onWheel;
        void paint (juce::Graphics& g) override { drawValueBox (g, getLocalBounds().toFloat().reduced (1.0f), text, isMouseOver()); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (onWheel) onWheel (w.deltaY); }
        juce::String text;
    };

    /** Covers page 1's cards with the panel's own background when page 2 is showing. */
    class PagePanel : public juce::Component
    {
    public:
        PagePanel() { setBounds (area); setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            if (version != Skin::get().version())
            {
                patch = Skin::get().makePatch (area);
                version = Skin::get().version();
            }
            g.drawImageAt (patch, 0, 0);
        }
        const juce::Rectangle<int> area { 462, 620, 962, 250 };
        juce::Image patch;
        int version = -1;
    };

    /** LIVE PADS: eight pads you hold while the DAW plays (or trigger with MIDI notes C1-G1). */
    class LivePanel : public juce::Component, private juce::Timer
    {
    public:
        explicit LivePanel (KoolaidProcessor& p) : proc (p)
        {
            setBounds (462, 620, 962, 250);
            for (int i = 0; i < km::LiveEngine::numPads; ++i)
                pads[(size_t) i] = padBounds (i);
        }
        ~LivePanel() override { releaseHeld(); }   // closing the window mid-press must not leave a pad stuck on
        void visibilityChanged() override
        {
            if (isVisible()) startTimerHz (30);
            else { stopTimer(); releaseHeld(); }
        }
        void releaseHeld()
        {
            if (pressed >= 0) proc.setLivePad (pressed, false);
            pressed = -1;
        }

        void paint (juce::Graphics& g) override
        {
            const auto& sk = Skin::get();
            const bool light = sk.getTheme() == Theme::light;
            static const juce::uint32 colours[km::LiveEngine::numPads] = { 0xffff2d9b, 0xffb13cff, 0xff35e6f2, 0xffb4f53c,
                                                                            0xffff8a3d, 0xff3d7dff, 0xffa36bff, 0xffff4b4b };
            for (int i = 0; i < km::LiveEngine::numPads; ++i)
            {
                const auto r = pads[(size_t) i];
                const juce::Colour c (colours[i]);
                const float a = juce::jlimit (0.0f, 1.0f, shown[(size_t) i]);
                const bool held = proc.isLivePadHeld (i) || a > 0.3f;
                if (a > 0.02f)
                {
                    g.setColour (c.withAlpha (0.35f * a));   // glow
                    g.fillRoundedRectangle (r.expanded (5.0f), 18.0f);
                }
                juce::ColourGradient grad (c.withMultipliedBrightness (0.55f + 0.45f * a).withAlpha (light ? 0.75f + 0.25f * a : 0.45f + 0.55f * a), 0, r.getY(),
                                           c.darker (0.9f - 0.5f * a).withAlpha (light ? 0.85f : 0.6f + 0.4f * a), 0, r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, 14.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f + 0.25f * a));
                g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.42f).reduced (4.0f, 3.0f), 11.0f);   // 90s gloss
                g.setColour (held ? juce::Colours::white : c.brighter (0.4f));
                g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, held ? 2.5f : 2.0f);

                drawShadowText (g, km::LiveEngine::padName (i), r.reduced (12.0f, 0.0f).withTrimmedTop (r.getHeight() * 0.22f),
                                sk.heavyItalic (30.0f), juce::Justification::centred, juce::Colours::white, juce::Colour (0x99000000));
                // the MIDI note in a pill
                const juce::Rectangle<float> pill (r.getRight() - 50.0f, r.getY() + 8.0f, 42.0f, 20.0f);
                g.setColour (juce::Colour (0x8c000000));
                g.fillRoundedRectangle (pill, 10.0f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.setFont (sk.bold (14.0f));
                g.drawText (km::LiveEngine::noteName (i), pill, juce::Justification::centred);
            }
            drawShadowText (g, "HOLD A PAD WHILE YOUR DAW PLAYS   /   MIDI NOTES C1-G1 PLAY THEM TOO   /   OR AUTOMATE THE \"LIVE\" PARAMETERS",
                            { 8.0f, (float) getHeight() - 30.0f, (float) getWidth() - 16.0f, 22.0f }, sk.bold (15.0f),
                            juce::Justification::centred, col::textDim, col::shadow);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            pressed = padAt (e.position);
            if (pressed >= 0) proc.setLivePad (pressed, true);
        }
        void mouseUp (const juce::MouseEvent&) override { releaseHeld(); }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int p = padAt (e.position);
            setTooltipFor (p);
        }

        std::function<void (const juce::String&)> setTooltipText;

    private:
        juce::Rectangle<float> padBounds (int i) const
        {
            const int col = i % 4, row = i / 4;
            const float w = (950.0f - 3.0f * 12.0f) / 4.0f, h = 96.0f;
            return { 8.0f + col * (w + 12.0f), 12.0f + row * (h + 12.0f), w, h };
        }
        int padAt (juce::Point<float> p) const
        {
            for (int i = 0; i < km::LiveEngine::numPads; ++i)
                if (pads[(size_t) i].contains (p)) return i;
            return -1;
        }
        void setTooltipFor (int p)
        {
            if (p == hoverPad) return;
            hoverPad = p;
            if (setTooltipText) setTooltipText (p >= 0 ? juce::String (km::LiveEngine::padHint (p)) + "  (MIDI " + km::LiveEngine::noteName (p) + ")" : juce::String());
        }
        void timerCallback() override
        {
            bool changed = false;
            for (int i = 0; i < km::LiveEngine::numPads; ++i)
            {
                const float a = juce::jmax (proc.getLiveActivity (i), proc.isLivePadHeld (i) ? 1.0f : 0.0f);
                if (std::abs (a - shown[(size_t) i]) > 0.01f) { shown[(size_t) i] = a; changed = true; }
            }
            if (changed) repaint();
        }

        KoolaidProcessor& proc;
        std::array<juce::Rectangle<float>, km::LiveEngine::numPads> pads;
        std::array<float, km::LiveEngine::numPads> shown {};
        int pressed = -1, hoverPad = -2;
    };

    /** A LivePanel with a tooltip. */
    class LivePanelWithTip : public LivePanel, public juce::SettableTooltipClient
    {
    public:
        explicit LivePanelWithTip (KoolaidProcessor& p) : LivePanel (p)
        {
            setTooltipText = [this] (const juce::String& t) { setTooltip (t); };
            setTooltip ("Hold a pad while your DAW plays");
        }
    };

    /** MAIN MOVES / BUILD & DROP tabs in the panel's title bar. */
    class PageTab : public juce::Component
    {
    public:
        PageTab (juce::Rectangle<int> b, juce::String t) : text (std::move (t)) { setBounds (b); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void setActive (bool a) { if (a != active) { active = a; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (active)
            {
                juce::ColourGradient grad (col::hotPink, r.getX(), 0, juce::Colour (0xffb13cff), r.getRight(), 0, false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
            }
            else
            {
                g.setColour (isMouseOver() ? col::lavender.withAlpha (0.35f) : col::lavender.withAlpha (0.18f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (col::lavender.withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
            }
            drawShadowText (g, text, r, Skin::get().heavyItalic (17.0f), juce::Justification::centred,
                            active ? juce::Colours::white : col::text,
                            active ? juce::Colour (0x8c000000) : col::shadow);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
        juce::String text;
        bool active = false;
    };

    enum class ValueKind { none, division, semitones };

    /** Scroll-wheel step for a length / rate box: note values, or seconds when the move is in seconds. */
    void stepLength (km::Move& m, float dy)
    {
        if (m.inSeconds && km::moveHasLength (m.type))
        {
            const auto& c = km::secondsChoices();
            int idx = 0;
            for (int i = 0; i < (int) c.size(); ++i) if (std::abs (c[(size_t) i] - m.seconds) < std::abs (c[(size_t) idx] - m.seconds)) idx = i;
            m.seconds = c[(size_t) juce::jlimit (0, (int) c.size() - 1, idx + (dy > 0 ? 1 : -1))];
        }
        else
            m.division = juce::jlimit (0, 8, m.division + (dy > 0 ? 1 : -1));
    }

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
    // page 2
    int page = 0;
    std::unique_ptr<WideCard> wide;
    std::unique_ptr<ArtBox> artMode, artValue;

    std::vector<juce::Component*> components() const
    {
        std::vector<juce::Component*> v;
        for (juce::Component* c : { (juce::Component*) wide.get(), (juce::Component*) title.get(), (juce::Component*) knob.get(),
                                    (juce::Component*) knob2.get(), (juce::Component*) value.get(), (juce::Component*) meter.get(),
                                    (juce::Component*) artMode.get(), (juce::Component*) artValue.get(),
                                    (juce::Component*) shade.get(), (juce::Component*) power.get() })
            if (c != nullptr) v.push_back (c);
        for (auto& m : modes) v.push_back (m.get());
        return v;
    }
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
            c->value->setFixedDarkStyle();
            c->value->onClick = [this, card] { showValueMenu (*card); };
            c->value->onWheel = [this, card] (float dy)
            {
                auto& m = target (card->type);
                if (card->kind == ValueKind::division)
                    stepLength (m, dy);
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

    //---------------------------------------------------------------------- page 2
    auto panel = std::make_unique<PagePanel>();
    parent.addChildComponent (*panel);
    page2Panel = std::move (panel);

    struct WideDef { MoveType type; juce::Rectangle<int> card; float hue; const char* knob; const char* value; const char* tip; };
    const WideDef wides[] = {
        { MoveType::Riser,    { 466, 630, 306, 109 }, 0.43f, "LEVEL", "LENGTH", "White-noise riser / uplifter sweeping up into the drop" },
        { MoveType::RevCrash, { 790, 630, 306, 109 }, 0.12f, "LEVEL", "LENGTH", "A crash cymbal played backwards, swelling into the downbeat" },
        { MoveType::Pump,     { 1114, 630, 306, 109 }, 0.90f, "DEPTH", "RATE",  "Sidechain pump: ducks the build in time with the beat" },
        { MoveType::Gap,      { 466, 752, 306, 109 }, 0.63f, "DEPTH", "LENGTH", "The breath before the drop: silence or a filter right before it hits" },
        { MoveType::Impact,   { 790, 752, 306, 109 }, 0.03f, "LEVEL", "TAIL",  "Hit + crash on the drop's downbeat (plays after the drop)" },
        { MoveType::Boom,     { 1114, 752, 306, 109 }, 0.79f, "LEVEL", "LENGTH", "Sub boom on the drop's downbeat (plays after the drop)" },
        // page 3: fx moves
        { MoveType::DelayThrow, { 466, 630, 306, 109 }, 0.50f, "FEEDBACK", "TIME", "Tempo-synced echoes: throw the last beat into the delay, or build the echoes up" },
        { MoveType::Bitcrush, { 790, 630, 306, 109 }, 0.27f, "CRUSH", "LENGTH", "Crushes the bits / sample rate more and more into the drop" },
        { MoveType::Phaser,   { 1114, 630, 306, 109 }, 0.86f, "DEPTH", "LENGTH", "Barber-pole: a phaser that rises forever, speeding up into the drop (or a phaser / flanger sweep)" },
        { MoveType::Width,    { 466, 752, 306, 109 }, 0.62f, "AMOUNT", "LENGTH", "Squeezes the build to mono so the drop hits full width (or widens it)" },
        { MoveType::Scratch,  { 790, 752, 306, 109 }, 0.04f, "SPEED", "LENGTH", "Vinyl: baby scratches, a DJ rewind, or a turntable brake into the drop" },
        { MoveType::PreVerb,  { 1114, 752, 306, 109 }, 0.15f, "LEVEL", "LENGTH", "The drop's own downbeat, reverb-swelled in reverse so it rises into the drop" },
    };
    for (auto& d : wides)
    {
        auto c = std::make_unique<Card>();
        auto* card = c.get();
        c->type = d.type;
        c->bounds = d.card;
        c->kind = ValueKind::division;
        c->page = km::cardPage (d.type);
        const auto x = d.card.getX(), y = d.card.getY(), w = d.card.getWidth();

        c->wide = std::make_unique<WideCard> (d.card, d.type, d.hue, d.knob, d.value);
        parent.addChildComponent (*c->wide);

        c->title = std::make_unique<SkinButton> (juce::Rectangle<int> (x + 6, y + 4, w - 44, 30), 6.0f);
        c->title->onClick = [this, card] { showTitleMenu (*card); };
        c->title->setTooltip (juce::String (d.tip) + " - click for variations");
        parent.addChildComponent (*c->title);

        c->knob = std::make_unique<SkinKnob> (juce::Point<float> ((float) x + 62.0f, (float) y + 66.0f));
        c->knob->setTint (c->wide->faceTint());
        c->knob->setDefault (km::defaultMove (d.type).knob);
        c->knob->onBubble = [this] (SkinKnob& kn, const juce::String& t, bool show) { if (onBubble) onBubble (kn, t, show); };
        c->knob->valueText = [this, card] (float) { return km::knobName (card->type, 0).toUpperCase() + " " + km::knobValueText (target (card->type), 0); };
        c->knob->onChange = [this, card] (float v)
        {
            target (card->type).knob = v;
            if (onEdited) onEdited();
            if (onStackChanged) onStackChanged();
        };
        c->knob->setTooltip (juce::String (d.tip) + " (drag, double-click to reset)");
        parent.addChildComponent (*c->knob);

        c->artMode = std::make_unique<ArtBox> (juce::Rectangle<int> (x + 176, y + 43, 122, 24));
        c->artMode->onClick = [this, card] { showTitleMenu (*card); };
        c->artMode->onWheel = [this, card] (float dy)
        {
            auto& m = target (card->type);
            m.mode = juce::jlimit (0, km::modeNames (card->type).size() - 1, m.mode + (dy > 0 ? -1 : 1));
            if (onEdited) onEdited();
            if (onStackChanged) onStackChanged();
            refresh();
        };
        c->artMode->setTooltip ("Variation");
        parent.addChildComponent (*c->artMode);

        c->artValue = std::make_unique<ArtBox> (juce::Rectangle<int> (x + 176, y + 75, 122, 24));
        c->artValue->onClick = [this, card] { showValueMenu (*card, card->artValue.get()); };
        c->artValue->onWheel = [this, card] (float dy)
        {
            auto& m = target (card->type);
            stepLength (m, dy);
            if (onEdited) onEdited();
            if (onStackChanged) onStackChanged();
            refresh();
        };
        c->artValue->setTooltip (juce::String (d.value).toLowerCase().replaceFirstOccurrenceOf ("l", "L"));
        parent.addChildComponent (*c->artValue);

        c->shade = std::make_unique<CardShade> (d.card);
        parent.addChildComponent (*c->shade);

        c->power = std::make_unique<SkinButton> (juce::Rectangle<int> (x + w - 28, y + 6, 25, 22), 5.0f);
        c->power->setArtSource ({ 1390, 758, 25, 22 });
        c->power->onClick = [this, card] { togglePower (card->type); };
        c->power->setTooltip ("Turn " + juce::String (km::typeName (d.type)) + " on/off (adds it to the stack if it isn't there)");
        parent.addChildComponent (*c->power);

        cards.push_back (std::move (c));
    }

    // LIVE PADS page
    {
        auto lp = std::make_unique<LivePanelWithTip> (proc);
        parent.addChildComponent (*lp);
        livePanel = std::move (lp);
    }

    auto tab1 = std::make_unique<PageTab> (juce::Rectangle<int> (948, 589, 120, 23), "MAIN MOVES");
    auto tab2 = std::make_unique<PageTab> (juce::Rectangle<int> (1074, 589, 124, 23), "BUILD & DROP");
    auto tab3 = std::make_unique<PageTab> (juce::Rectangle<int> (1204, 589, 104, 23), "FX MOVES");
    auto tab4 = std::make_unique<PageTab> (juce::Rectangle<int> (1314, 589, 108, 23), "LIVE PADS");
    tab1->onClick = [this] { setPage (0); };
    tab2->onClick = [this] { setPage (1); };
    tab3->onClick = [this] { setPage (2); };
    tab4->onClick = [this] { setPage (3); };
    for (auto* t : { tab1.get(), tab2.get(), tab3.get(), tab4.get() })
        parent.addAndMakeVisible (*t);
    tabs.push_back (std::move (tab1));
    tabs.push_back (std::move (tab2));
    tabs.push_back (std::move (tab3));
    tabs.push_back (std::move (tab4));

    setPage (0);
    refresh();
}

void EffectCards::setPage (int p)
{
    page = juce::jlimit (0, 3, p);
    page2Panel->setVisible (page >= 1);
    if (page >= 1) page2Panel->toFront (false);
    livePanel->setVisible (page == 3);
    if (page == 3) livePanel->toFront (false);
    for (auto& c : cards)
        for (auto* comp : c->components())
        {
            comp->setVisible (c->page == page);
            if (c->page == page && page >= 1) comp->toFront (false);
        }
    for (size_t i = 0; i < tabs.size(); ++i)
        static_cast<PageTab*> (tabs[i].get())->setActive ((int) i == page);
    if (onPageChanged) onPageChanged();
}

void EffectCards::followSelection()
{
    const int sel = stack.selectedIndex();
    const auto& moves = proc.getModel().moves;
    if (sel >= 0 && sel < (int) moves.size())
    {
        const int want = km::cardPage (moves[(size_t) sel].type);
        if (want != page && page != 3) setPage (want);   // the LIVE PADS page stays put while you perform
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

km::MoveType EffectCards::cardTypeAt (juce::Point<int> p) const
{
    for (auto& c : cards)
        if (c->page == page && c->bounds.contains (p))
            return c->type;
    return MoveType::NumTypes;
}

int EffectCards::ensureInStack (MoveType t)
{
    if (! hasInstance (t))
        togglePower (t);
    else
        selectType (t);
    return target (t).uid;
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
    if (km::moveTakesSample (t))
    {
        const bool has = m.hasSample() && hasInstance (t);
        menu.addItem (5, has ? "Your Sample: " + m.sampleName + " (Change...)" : "Use Your Own Sample... (or drop a file here)", true, has);
        menu.addItem (6, "Built-In Sound", has);
        menu.addSeparator();
    }
    if (km::moveHasPattern (t))
    {
        menu.addItem (3, "Step Pattern - Draw It...", true, m.patternOn && hasInstance (t));
        menu.addItem (4, "Built-In Rhythm (No Pattern)", m.patternOn && hasInstance (t));
        menu.addSeparator();
    }
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
            if (hasInstance (t) && target (t).hasSample()) proc.clearMoveSample (target (t).uid);   // a built-in type replaces your sample
            target (t).mode = r - 100;
            if (! hasInstance (t)) { togglePower (t); return; }
        }
        else if (r == 5)
        {
            if (onLoadSample) onLoadSample (ensureInStack (t));
            return;
        }
        else if (r == 6)
        {
            if (hasInstance (t)) proc.clearMoveSample (target (t).uid);
            if (onStackChanged) onStackChanged();
            refresh();
            return;
        }
        else if (r == 3 || r == 4)
        {
            if (! hasInstance (t)) togglePower (t);          // a pattern needs a move in the stack
            auto& mv = target (t);
            mv.patternOn = r == 3;
            proc.modelChanged (true);
            if (onStackChanged) onStackChanged();
            if (r == 3 && onEditPattern) onEditPattern (mv.uid);
            refresh();
            return;
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

void EffectCards::showValueMenu (Card& card, juce::Component* anchor)
{
    selectType (card.type);
    auto& m = target (card.type);
    juce::PopupMenu menu;
    if (card.kind == ValueKind::division)
    {
        const auto ty = card.type;
        menu.addSectionHeader (ty == MoveType::DelayThrow ? "ECHO TIME"
                               : (ty == MoveType::Chops || ty == MoveType::Stutter || ty == MoveType::Pump ? "RATE" : "LENGTH"));
        const bool secs = m.inSeconds && km::moveHasLength (ty);
        for (int i = 0; i < (int) km::divisions().size(); ++i)
            menu.addItem (1 + i, km::divisions()[(size_t) i].longLabel, true, ! secs && m.division == i);
        if (km::moveHasLength (ty))
        {
            // tempo-free: the same length whatever the tempo (handy for intros and FX)
            menu.addSectionHeader ("SECONDS");
            const auto& c = km::secondsChoices();
            for (int i = 0; i < (int) c.size(); ++i)
            {
                const float v = c[(size_t) i];
                menu.addItem (500 + i, juce::String (v, v < 1.0f ? 2 : (v == std::floor (v) ? 0 : 1)) + " sec", true,
                              secs && std::abs (m.seconds - v) < 1.0e-3f);
            }
        }
    }
    else
    {
        menu.addSectionHeader ("SEMITONES");
        for (int i = 0; i < 16; ++i)
            menu.addItem (1000 + semitoneChoices[i] + 100, semiText (semitoneChoices[i]) + " st", true, m.semitones == semitoneChoices[i]);
    }
    const auto t = card.type;
    menu.showMenuAsync (menuOptionsFor (anchor != nullptr ? anchor : card.value.get()), [this, t] (int r)
    {
        if (r == 0) return;
        auto& mv = target (t);
        if (r >= 1000) mv.semitones = r - 1100;
        else if (r >= 500)
        {
            mv.inSeconds = true;
            mv.seconds = km::secondsChoices()[(size_t) juce::jlimit (0, (int) km::secondsChoices().size() - 1, r - 500)];
            mv.customRegion = false;
        }
        else
        {
            mv.inSeconds = false;
            mv.division = r - 1;
            // a new length means "use my natural spot" again (a new rate / echo time doesn't)
            if (t != MoveType::Chops && t != MoveType::Stutter && t != MoveType::Pump && t != MoveType::DelayThrow)
                mv.customRegion = false;
        }
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
            c->value->setText (c->kind == ValueKind::semitones ? semiText (m.semitones) : km::moveLengthText (m, false));
        if (c->artValue)
            c->artValue->setText (m.inSeconds && km::moveHasLength (m.type) ? km::moveLengthText (m, false)
                                                                            : juce::String (km::divisions()[(size_t) juce::jlimit (0, 8, m.division)].longLabel).toUpperCase());
        if (c->artMode)
        {
            const auto names = km::modeNames (c->type);
            c->artMode->setText (m.hasSample() && hasInstance (c->type) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + m.sampleName.toUpperCase()
                                                                        : names[juce::jlimit (0, names.size() - 1, m.mode)].toUpperCase());
        }
        if (c->page >= 1 && c->knob && ! c->knob->isDragging())
            c->knob->setValue (m.knob);
        for (size_t i = 0; i < c->modes.size(); ++i)
            c->modes[i]->setOn ((int) i == m.mode);

        const auto modes = km::modeNames (c->type);
        c->title->setTooltip (juce::String (km::cardTitle (c->type)) + " - " + modes[juce::jlimit (0, modes.size() - 1, m.mode)]
                              + " (click for variations)");
    }
}
