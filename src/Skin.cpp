#include "Skin.h"
#include "BinaryData.h"

namespace skin
{

Skin& Skin::get()
{
    static Skin instance;
    return instance;
}

static juce::Image loadArt (const void* data, int size)
{
    auto img = juce::ImageCache::getFromMemory (data, size);
    if (! img.isValid())
        img = juce::Image (juce::Image::ARGB, designW, designH, true);
    return img.convertedToFormat (juce::Image::ARGB);
}

/** A greyed-out copy of the art for "off" states (darker in dark mode, paler in light mode). */
static juce::Image makeOff (const juce::Image& src, bool light)
{
    // straight per-pixel arithmetic (the art is opaque): mostly-grey, then darker (dark theme)
    // or washed toward the light panel colour (light theme). Fast enough for the 2x art too.
    auto off = src.createCopy();
    juce::Image::BitmapData bd (off, juce::Image::BitmapData::readWrite);
    const float wr = 0xea / 255.0f, wg = 0xe6 / 255.0f, wb = 0xf2 / 255.0f;
    for (int y = 0; y < bd.height; ++y)
    {
        auto* line = bd.getLinePointer (y);
        for (int x = 0; x < bd.width; ++x)
        {
            auto* p = reinterpret_cast<juce::PixelARGB*> (line + x * bd.pixelStride);
            const float r = p->getRed() / 255.0f, g = p->getGreen() / 255.0f, b = p->getBlue() / 255.0f;
            const float lum = 0.3f * r + 0.55f * g + 0.15f * b;
            float nr = r + (lum - r) * 0.88f, ng = g + (lum - g) * 0.88f, nb = b + (juce::jmin (1.0f, lum * 1.08f) - b) * 0.88f;
            if (light) { nr += (wr - nr) * 0.45f; ng += (wg - ng) * 0.45f; nb += (wb - nb) * 0.45f; }
            else       { nr *= 0.55f; ng *= 0.55f; nb *= 0.55f; }
            p->setARGB (p->getAlpha(), (juce::uint8) juce::roundToInt (nr * 255.0f), (juce::uint8) juce::roundToInt (ng * 255.0f),
                        (juce::uint8) juce::roundToInt (nb * 255.0f));
        }
    }
    return off;
}

const juce::Image& Skin::image2x() const
{
    const int t = theme == Theme::light ? 1 : 0;
    if (! hi[t].isValid())
    {
        hi[t] = t == 1 ? loadArt (KoolaidBinary::skin_light_2x_png, KoolaidBinary::skin_light_2x_pngSize)
                       : loadArt (KoolaidBinary::skin_2x_png, KoolaidBinary::skin_2x_pngSize);
        if (hi[t].getWidth() != designW * 2)   // missing: fall back to the 1x art, upscaled
            hi[t] = image().rescaled (designW * 2, designH * 2, juce::Graphics::highResamplingQuality);
    }
    return hi[t];
}

const juce::Image& Skin::offImage2x() const
{
    const int t = theme == Theme::light ? 1 : 0;
    if (! hiOff[t].isValid())
        hiOff[t] = makeOff (image2x(), t == 1);
    return hiOff[t];
}

void Skin::drawArt (juce::Graphics& g, juce::Rectangle<int> src, juce::Rectangle<int> dest, bool offState) const
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (wantsHiRes (g))
    {
        const auto& img = offState ? offImage2x() : image2x();
        g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                     src.getX() * 2, src.getY() * 2, src.getWidth() * 2, src.getHeight() * 2);
    }
    else
    {
        g.drawImage (offState ? offImage() : image(), dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                     src.getX(), src.getY(), src.getWidth(), src.getHeight());
    }
}

Skin::Skin()
{
    art = loadArt (KoolaidBinary::skin_png, KoolaidBinary::skin_pngSize);
    artLight = loadArt (KoolaidBinary::skin_light_png, KoolaidBinary::skin_light_pngSize);
    off = makeOff (art, false);
    offLight = makeOff (artLight, true);

    juce::PropertiesFile::Options o;
    o.applicationName = "Koolaid Man";
    o.folderName = "Koolaid Man";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    prefs = std::make_unique<juce::PropertiesFile> (o);
    theme = prefs->getIntValue ("theme", 0) == 1 ? Theme::light : Theme::dark;
    applyColours();

    using namespace KoolaidBinary;
    tfMedium      = juce::Typeface::createSystemTypefaceFor (BarlowCondensedMedium_ttf, BarlowCondensedMedium_ttfSize);
    tfSemi        = juce::Typeface::createSystemTypefaceFor (BarlowCondensedSemiBold_ttf, BarlowCondensedSemiBold_ttfSize);
    tfBold        = juce::Typeface::createSystemTypefaceFor (BarlowCondensedBold_ttf, BarlowCondensedBold_ttfSize);
    tfHeavyItalic = juce::Typeface::createSystemTypefaceFor (BarlowCondensedExtraBoldItalic_ttf, BarlowCondensedExtraBoldItalic_ttfSize);
    tfWide        = juce::Typeface::createSystemTypefaceFor (BarlowSemiBold_ttf, BarlowSemiBold_ttfSize);
    tfWideBold    = juce::Typeface::createSystemTypefaceFor (BarlowBold_ttf, BarlowBold_ttfSize);
}

void Skin::setTheme (Theme t)
{
    if (t == theme) return;
    theme = t;
    ++themeVersion;
    // free the other theme's 2x art (it reloads if you switch back)
    const int other = t == Theme::light ? 0 : 1;
    hi[other] = {};
    hiOff[other] = {};
    applyColours();
    if (prefs != nullptr)
    {
        prefs->setValue ("theme", (int) t);
        prefs->saveIfNeeded();
    }
    sendSynchronousChangeMessage();
}

void Skin::applyColours()
{
    using namespace col;
    if (theme == Theme::light)
    {
        text = juce::Colour (0xff2a1747);  textDim = juce::Colour (0xff7a6a99);  shadow = juce::Colour (0xb3ffffff);
        lavender = juce::Colour (0xff7d5fe0);
        menuBg = juce::Colour (0xfffff7fc); menuBg2 = juce::Colour (0xffe8f3ff);
        display = juce::Colour (0xfffdfaff); gridLine = juce::Colour (0x162a1747);
        waveOut = juce::Colour (0xff7d5fe0); waveIn = juce::Colour (0xffff3fa8);
        zoneTop = juce::Colour (0x66ff9ad6); zoneBot = juce::Colour (0x66ff6cc4);
        pill = juce::Colour (0xf2ffffff);
        rowTop = juce::Colour (0xffffffff); rowBot = juce::Colour (0xfff1eaff);
        rowSelTop = juce::Colour (0xffffeaf6); rowSelBot = juce::Colour (0xffffd3ee);
        rowBorder = juce::Colour (0xffc9b8ef); rowHover = juce::Colour (0xff9f84e0);
        grip = juce::Colour (0xff5a4785); boxOff = juce::Colour (0xffffffff);
        track = juce::Colour (0xffe6def7); thumb = juce::Colour (0xff9b86d6);
        cardShade = juce::Colour (0x70ffffff);
        panelTop = juce::Colour (0xfffffbfe); panelBot = juce::Colour (0xffeaf4ff);
    }
    else
    {
        text = juce::Colour (0xfff7f3ff);  textDim = juce::Colour (0xff9a90b8);  shadow = juce::Colour (0x8c000000);
        lavender = juce::Colour (0xff9687ea);
        menuBg = juce::Colour (0xff1c1233); menuBg2 = juce::Colour (0xff140d26);
        display = juce::Colour (0xff0e111e); gridLine = juce::Colour (0x0fffffff);
        waveOut = juce::Colour (0xff9887ea); waveIn = juce::Colour (0xffff6ccb);
        zoneTop = juce::Colour (0xc0781a5c); zoneBot = juce::Colour (0xc0501040);
        pill = juce::Colour (0xe0120a22);
        rowTop = juce::Colour (0xff342a4c); rowBot = juce::Colour (0xff231c35);
        rowSelTop = juce::Colour (0xff4a3170); rowSelBot = juce::Colour (0xff2f1d4a);
        rowBorder = juce::Colour (0xff5d4f80); rowHover = juce::Colour (0xff8d7bb8);
        grip = juce::Colour (0xebffffff); boxOff = juce::Colour (0xff1a1426);
        track = juce::Colour (0xff15111f); thumb = juce::Colour (0xffb2a6d6);
        cardShade = juce::Colour (0x4a0c0618);
        panelTop = juce::Colour (0xff23173f); panelBot = juce::Colour (0xff100a1f);
    }
}

juce::Colour Skin::pixel (int x, int y) const
{
    const auto& art = image();
    return art.getPixelAt (juce::jlimit (0, art.getWidth() - 1, x), juce::jlimit (0, art.getHeight() - 1, y));
}

juce::Image Skin::makePatch (juce::Rectangle<int> r) const
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, r.getWidth()), juce::jmax (1, r.getHeight()), false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < r.getHeight(); ++y)
    {
        const auto l = pixel (r.getX() - 1, r.getY() + y);
        const auto rr = pixel (r.getRight(), r.getY() + y);
        for (int x = 0; x < r.getWidth(); ++x)
            bd.setPixelColour (x, y, l.interpolatedWith (rr, (float) x / (float) juce::jmax (1, r.getWidth() - 1)));
    }
    return img;
}

static juce::Font fontFor (const juce::Typeface::Ptr& tf, float h)
{
    if (tf == nullptr)
        return juce::Font (juce::FontOptions (h, juce::Font::bold));
    return juce::Font (juce::FontOptions (tf).withHeight (h));
}

juce::Font Skin::medium (float h) const      { return fontFor (tfMedium, h); }
juce::Font Skin::semibold (float h) const    { return fontFor (tfSemi, h); }
juce::Font Skin::bold (float h) const        { return fontFor (tfBold, h); }
juce::Font Skin::heavyItalic (float h) const { return fontFor (tfHeavyItalic, h); }
juce::Font Skin::wide (float h) const        { return fontFor (tfWide, h); }
juce::Font Skin::wideBold (float h) const    { return fontFor (tfWideBold, h); }

void drawShadowText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Font f,
                     juce::Justification j, juce::Colour c, juce::Colour shadowColour)
{
    g.setFont (f);
    g.setColour (shadowColour);
    g.drawText (s, r.translated (1.0f, 1.5f), j, false);
    g.setColour (c);
    g.drawText (s, r, j, false);
}

juce::PopupMenu::Options menuOptionsFor (juce::Component* target)
{
    return juce::PopupMenu::Options().withTargetComponent (target).withStandardItemHeight (26);
}

//==============================================================================
KoolLookAndFeel::KoolLookAndFeel()
{
    applyTheme();
}

void KoolLookAndFeel::applyTheme()
{
    const bool light = Skin::get().getTheme() == Theme::light;
    setColour (juce::PopupMenu::backgroundColourId, col::menuBg);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::hotPink);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, col::cyan);
    setColour (juce::TooltipWindow::backgroundColourId, col::menuBg);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::cyan);
    setColour (juce::AlertWindow::backgroundColourId, col::menuBg);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::hotPink);
    setColour (juce::TextEditor::backgroundColourId, light ? juce::Colours::white : juce::Colour (0xff0d0b18));
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::cyan);
    setColour (juce::TextEditor::focusedOutlineColourId, col::hotPink);
    setColour (juce::TextEditor::highlightColourId, col::hotPink.withAlpha (0.5f));
    setColour (juce::CaretComponent::caretColourId, col::cyan);
    setColour (juce::TextButton::buttonColourId, light ? juce::Colour (0xffe9defc) : juce::Colour (0xff3a2563));
    setColour (juce::TextButton::buttonOnColourId, col::hotPink);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::cyan);
    setColour (juce::ResizableWindow::backgroundColourId, col::menuBg);
    setColour (juce::ScrollBar::thumbColourId, col::thumb);
    setColour (juce::ScrollBar::trackColourId, col::track);
}

juce::Font KoolLookAndFeel::getPopupMenuFont() { return Skin::get().semibold (18.0f); }
juce::Font KoolLookAndFeel::getAlertWindowTitleFont() { return Skin::get().heavyItalic (24.0f); }
juce::Font KoolLookAndFeel::getAlertWindowMessageFont() { return Skin::get().medium (17.0f); }
juce::Font KoolLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return Skin::get().bold (juce::jmin (18.0f, h * 0.6f)); }

void KoolLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    juce::ColourGradient grad (col::menuBg, 0, 0, col::menuBg2, 0, (float) height, false);
    g.setGradientFill (grad);
    g.fillAll();
    g.setColour (col::cyan.withAlpha (0.8f));
    g.drawRect (0, 0, width, height, 2);
    g.setColour (col::hotPink.withAlpha (0.5f));
    g.drawRect (2, 2, width - 4, height - 4, 1);
}

void KoolLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                         const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto r = area.reduced (8, 0).toFloat();
        g.setColour (col::lavender.withAlpha (0.35f));
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 1.0f));
        return;
    }
    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        juce::ColourGradient grad (col::hotPink, (float) r.getX(), 0, juce::Colour (0xffb13cff), (float) r.getRight(), 0, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }
    const auto colour = ! isActive ? col::textDim.withAlpha (0.6f)
                                   : (textColour != nullptr ? *textColour : col::text);
    auto textArea = r.withTrimmedLeft (26).withTrimmedRight (hasSubMenu ? 20 : 8);
    if (isTicked)
    {
        auto tick = juce::Rectangle<float> ((float) r.getX() + 7.0f, (float) r.getCentreY() - 6.0f, 12.0f, 12.0f);
        g.setColour (isHighlighted ? juce::Colours::white : col::lime);
        g.fillRoundedRectangle (tick, 3.0f);
        juce::Path p;
        p.startNewSubPath (tick.getX() + 2.5f, tick.getCentreY());
        p.lineTo (tick.getCentreX() - 0.5f, tick.getBottom() - 2.5f);
        p.lineTo (tick.getRight() - 2.0f, tick.getY() + 2.5f);
        g.setColour (juce::Colours::black);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
    else if (icon != nullptr)
    {
        icon->drawWithin (g, juce::Rectangle<float> ((float) r.getX() + 5.0f, (float) r.getY() + 3.0f, 16.0f, (float) r.getHeight() - 6.0f),
                          juce::RectanglePlacement::centred, 1.0f);
    }
    g.setColour (colour);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (Skin::get().medium (15.0f));
        g.setColour (col::textDim);
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight);
    }
    if (hasSubMenu)
    {
        juce::Path tri;
        const float cx = (float) r.getRight() - 11.0f, cy = (float) r.getCentreY();
        tri.addTriangle (cx - 3.0f, cy - 5.0f, cx - 3.0f, cy + 5.0f, cx + 4.0f, cy);
        g.setColour (colour);
        g.fillPath (tri);
    }
}

void KoolLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setFont (Skin::get().heavyItalic (17.0f));
    g.setColour (Skin::get().getTheme() == Theme::light ? juce::Colour (0xffe0258f) : col::cyan);
    g.drawFittedText (name, area.withTrimmedLeft (10).withTrimmedTop (2), juce::Justification::centredLeft, 1);
}

juce::Rectangle<int> KoolLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto f = Skin::get().medium (16.0f);
    const int w = juce::jmin (420, (int) juce::GlyphArrangement::getStringWidth (f, tipText) + 22);
    const int lines = juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, tipText) / 400.0f));
    const int h = 10 + lines * 19;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void KoolLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.setColour (col::menuBg);
    g.fillRoundedRectangle (0, 0, (float) width, (float) height, 5.0f);
    g.setColour (col::cyan);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 5.0f, 1.5f);
    g.setColour (col::text);
    g.setFont (Skin::get().medium (16.0f));
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (10, 4), juce::Justification::centredLeft, 4);
}

//==============================================================================
SkinButton::SkinButton (juce::Rectangle<int> b, float r) : radius (r)
{
    setBounds (b);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SkinButton::setLit (bool b)
{
    if (b != lit) { lit = b; repaint(); }
}

void SkinButton::paint (juce::Graphics& g)
{
    auto local = getLocalBounds().toFloat();
    if (! artSource.isEmpty())
    {
        Skin::get().drawArt (g, artSource, getLocalBounds(), ! lit);
    }
    else if (! lit)
    {
        auto region = litRegion.isEmpty() ? getBounds() : litRegion;
        auto dest = region.translated (-getX(), -getY());
        Skin::get().drawArt (g, region, dest, true);
    }
    if (! glow.isTransparent())
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (glow.withAlpha (0.18f * (float) (4 - i)));
            g.drawRoundedRectangle (local.reduced (1.5f).expanded ((float) i * 0.0f), radius, (float) i * 2.0f);
        }
        g.setColour (glow);
        g.drawRoundedRectangle (local.reduced (1.5f), radius, 2.0f);
    }
    const bool over = isMouseOver (true), down = isMouseButtonDown();
    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (local.reduced (2.0f), radius);
    }
    else if (over && showHover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillRoundedRectangle (local.reduced (2.0f), radius);
    }
    if (flash)
    {
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.fillRoundedRectangle (local.reduced (2.0f), radius);
    }
}

void SkinButton::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    popupClick = onRightClick != nullptr && e.mods.isPopupMenu();
    if (popupClick)
        onRightClick();
    repaint();
}

void SkinButton::mouseDrag (const juce::MouseEvent& e)
{
    if (onDragOut && ! dragging && ! popupClick && e.getDistanceFromDragStart() > 8)
    {
        dragging = true;
        onDragOut (e);
    }
}

void SkinButton::mouseUp (const juce::MouseEvent& e)
{
    repaint();
    if (! dragging && ! popupClick && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
    dragging = false;
    popupClick = false;
}

//==============================================================================
SkinValueBox::SkinValueBox (juce::Rectangle<int> b, juce::Rectangle<int> eraseArea, float fontHeight, juce::Justification j)
    : justification (j)
{
    setBounds (b);
    erase = eraseArea.translated (-b.getX(), -b.getY());
    eraseDesign = eraseArea;
    font = Skin::get().wideBold (fontHeight);
}

void SkinValueBox::paint (juce::Graphics& g)
{
    if (patchVersion != Skin::get().version())
    {
        patch = Skin::get().makePatch (eraseDesign);
        patchVersion = Skin::get().version();
    }
    g.drawImageAt (patch, erase.getX(), erase.getY());
    const bool light = Skin::get().getTheme() == Theme::light && ! fixedDark;
    if ((onClick || onDrag) && isMouseOver (true))
    {
        g.setColour (light ? juce::Colour (0x14ff2d9b) : juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);
    }
    auto r = erase.toFloat().reduced (2.0f, 0.0f);
    g.setFont (font);
    g.setColour (fixedDark ? juce::Colour (0x8c000000) : col::shadow);
    g.drawFittedText (text, r.translated (1.0f, 1.5f).toNearestInt(), justification, 1, 0.72f);
    g.setColour (customColour ? colour : col::text);
    g.drawFittedText (text, r.toNearestInt(), justification, 1, 0.72f);
}

void SkinValueBox::mouseDown (const juce::MouseEvent& e)
{
    lastDragY = (float) e.position.y;
    dragged = false;
}

void SkinValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! onDrag) return;
    if (e.getDistanceFromDragStart() > 3) dragged = true;
    const float dy = lastDragY - e.position.y;
    lastDragY = e.position.y;
    if (dragged) onDrag (dy);
}

void SkinValueBox::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && e.mouseWasClicked() && onClick)
        onClick();
}

void SkinValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick) onDoubleClick();
}

void SkinValueBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onWheel) onWheel (w.deltaY);
    else Component::mouseWheelMove (e, w);
}

//==============================================================================
SkinKnob::SkinKnob (juce::Point<float> c, float faceRadius) : faceR (faceRadius)
{
    const int half = (int) std::ceil (faceRadius + 9.0f);
    setBounds (juce::roundToInt (c.x) - half, juce::roundToInt (c.y) - half, half * 2, half * 2);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    // tint the face like the knob painted in the art (median of the face pixels)
    std::vector<juce::Colour> px;
    const auto& sk = Skin::get();
    for (int dy = -10; dy <= 10; dy += 2)
        for (int dx = -10; dx <= 10; dx += 2)
            if (dx * dx + dy * dy <= 100)
                px.push_back (sk.pixel (juce::roundToInt (c.x) + dx, juce::roundToInt (c.y) + dy));
    std::sort (px.begin(), px.end(), [] (juce::Colour a, juce::Colour b) { return a.getPerceivedBrightness() < b.getPerceivedBrightness(); });
    tint = px.empty() ? juce::Colour (0xffc8c0f0) : px[px.size() * 2 / 3];
}

void SkinKnob::setValue (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void SkinKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = faceR;

    if (isMouseOver (true) || dragging)
    {
        g.setColour (juce::Colours::white.withAlpha (dragging ? 0.28f : 0.14f));
        g.drawEllipse (c.x - r - 2.5f, c.y - r - 2.5f, (r + 2.5f) * 2.0f, (r + 2.5f) * 2.0f, 2.0f);
    }

    // face
    juce::ColourGradient grad (tint.brighter (0.55f), c.x - r * 0.45f, c.y - r * 0.6f,
                               tint.darker (0.45f), c.x + r * 0.6f, c.y + r * 0.8f, true);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (tint.darker (1.4f).withAlpha (0.85f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.6f);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r - 1.8f, r - 1.8f, 0.0f, -2.2f, 0.6f, true);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.strokePath (arc, juce::PathStrokeType (1.2f));

    // pointer
    const float ang = juce::degreesToRadians (-135.0f + 270.0f * value);
    const juce::Point<float> dir (std::sin (ang), -std::cos (ang));
    g.setColour (juce::Colour (0xff1a0f2a));
    g.drawLine ({ c + dir * (r * 0.18f), c + dir * (r * 0.82f) }, 3.4f);

    if (dimmed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }
}

void SkinKnob::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragStartValue = value;
    if (onBubble && valueText) onBubble (*this, valueText (value), true);
    repaint();
}

void SkinKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float sens = e.mods.isShiftDown() ? 600.0f : 160.0f;
    setValue (dragStartValue + (float) (-e.getDistanceFromDragStartY()) / sens, true);
    if (onBubble && valueText) onBubble (*this, valueText (value), true);
}

void SkinKnob::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (onBubble) onBubble (*this, {}, false);
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void SkinKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    setValue (defaultValue, true);
    if (onGestureEnd) onGestureEnd();
}

void SkinKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setValue (value + (w.deltaY > 0 ? 0.02f : -0.02f) * (w.isReversed ? -1.0f : 1.0f), true);
    if (onGestureEnd) onGestureEnd();
}

} // namespace skin
