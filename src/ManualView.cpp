#include "ManualView.h"
#include "BinaryData.h"

using namespace skin;

namespace
{
    constexpr float headerH = 66.0f;
    constexpr int tocWidth = 286;

    /** Removes markdown escapes ("\*" -> "*"). */
    juce::String unescape (const juce::String& s)
    {
        juce::String out;
        for (int i = 0; i < s.length(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.length()) { out << s[i + 1]; ++i; }
            else out << s[i];
        }
        return out;
    }

    /** Builds styled text from a line with **bold** spans. */
    juce::AttributedString styled (const juce::String& text, juce::Font regular, juce::Font boldFont,
                                   juce::Colour colour, juce::Colour boldColour)
    {
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (3.0f);
        bool bold = false;
        juce::String run;
        auto flush = [&]
        {
            if (run.isNotEmpty())
                as.append (unescape (run), bold ? boldFont : regular, bold ? boldColour : colour);
            run.clear();
        };
        for (int i = 0; i < text.length(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.length()) { run << text[i] << text[i + 1]; ++i; continue; }
            if (text[i] == '*' && i + 1 < text.length() && text[i + 1] == '*')
            {
                flush();
                bold = ! bold;
                ++i;
                continue;
            }
            run << text[i];
        }
        flush();
        return as;
    }
}

//==============================================================================
class ManualView::Content : public juce::Component
{
public:
    explicit Content (ManualView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto clip = g.getClipBounds().toFloat();
        for (auto& b : owner.blocks)
        {
            if (b.y > clip.getBottom() || b.y + b.h < clip.getY())
                continue;
            if (b.kind == Block::h1)
            {
                // neon underline
                juce::ColourGradient grad (col::hotPink, 0, 0, col::cyan, (float) getWidth(), 0, false);
                g.setGradientFill (grad);
                g.fillRect (0.0f, b.y + b.h - 4.0f, (float) getWidth() - 10.0f, 2.5f);
            }
            if (b.kind == Block::bullet)
            {
                g.setColour (col::cyan);
                g.fillEllipse (b.indent - 15.0f, b.y + 10.0f, 7.0f, 7.0f);
            }
            if (b.kind == Block::number)
            {
                g.setFont (Skin::get().heavyItalic (22.0f));
                g.setColour (col::hotPink);
                g.drawText (b.marker, juce::Rectangle<float> (b.indent - 34.0f, b.y - 1.0f, 28.0f, 26.0f), juce::Justification::centredRight);
            }
            b.layout.draw (g, { b.indent, b.y, (float) getWidth() - b.indent - 14.0f, b.h });
        }
    }

private:
    ManualView& owner;
};

class ManualView::Scroller : public juce::Viewport
{
public:
    explicit Scroller (ManualView& o) : owner (o) {}
    void visibleAreaChanged (const juce::Rectangle<int>&) override;
private:
    ManualView& owner;
};

class ManualView::Toc : public juce::Component
{
public:
    explicit Toc (ManualView& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    static constexpr float rowH = 33.0f;

    void paint (juce::Graphics& g) override
    {
        const int current = owner.currentSection();
        for (int i = 0; i < (int) owner.sections.size(); ++i)
        {
            juce::Rectangle<float> r (0.0f, i * rowH, (float) getWidth(), rowH - 3.0f);
            if (i == current)
            {
                juce::ColourGradient grad (col::hotPink, r.getX(), 0, juce::Colour (0xffb13cff), r.getRight(), 0, false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, 6.0f);
            }
            else if (i == hover)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            drawShadowText (g, owner.sections[(size_t) i].first.toUpperCase(), r.withTrimmedLeft (12.0f),
                            Skin::get().heavyItalic (21.0f), juce::Justification::centredLeft,
                            i == current ? juce::Colours::white : col::text.withAlpha (0.85f));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover ((int) (e.position.y / rowH)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = (int) (e.position.y / rowH);
        if (e.mouseWasClicked() && i >= 0 && i < (int) owner.sections.size())
            owner.scrollToSection (i);
    }

private:
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }
    ManualView& owner;
    int hover = -1;
};

void ManualView::Scroller::visibleAreaChanged (const juce::Rectangle<int>&)
{
    if (owner.toc != nullptr) owner.toc->repaint();
}

class ManualView::CloseButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    CloseButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Close the manual"); }
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        juce::ColourGradient grad (juce::Colour (0xffff8ad6), 0, r.getY(), col::hotPink, 0, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 9.0f);
        if (isMouseOver()) { g.setColour (juce::Colours::white.withAlpha (0.2f)); g.fillRoundedRectangle (r, 9.0f); }
        g.setColour (juce::Colour (0xff3a0726));
        g.setFont (Skin::get().heavyItalic (22.0f));
        g.drawText ("CLOSE  X", r, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
};

//==============================================================================
std::vector<ManualView::Block> ManualView::parse (const juce::String& markdown)
{
    std::vector<Block> out;
    juce::StringArray lines;
    lines.addLines (markdown);
    int section = -1;
    juce::String paragraph;

    auto flushPara = [&]
    {
        if (paragraph.trim().isNotEmpty())
        {
            Block b; b.kind = Block::para; b.text = paragraph.trim(); b.section = juce::jmax (0, section);
            out.push_back (b);
        }
        paragraph.clear();
    };

    for (auto line : lines)
    {
        line = line.trimEnd();
        if (line.trim().isEmpty()) { flushPara(); continue; }
        if (line.startsWith ("# "))
        {
            flushPara();
            ++section;
            Block b; b.kind = Block::h1; b.text = line.substring (2).trim(); b.section = section;
            out.push_back (b);
        }
        else if (line.startsWith ("## "))
        {
            flushPara();
            Block b; b.kind = Block::h2; b.text = line.substring (3).trim(); b.section = juce::jmax (0, section);
            out.push_back (b);
        }
        else if (line.startsWith ("- "))
        {
            flushPara();
            Block b; b.kind = Block::bullet; b.text = line.substring (2).trim(); b.section = juce::jmax (0, section);
            out.push_back (b);
        }
        else if (line.containsChar ('.') && line.upToFirstOccurrenceOf (".", false, false).containsOnly ("0123456789")
                 && line.upToFirstOccurrenceOf (".", false, false).isNotEmpty()
                 && line.fromFirstOccurrenceOf (".", false, false).startsWithChar (' '))
        {
            flushPara();
            Block b; b.kind = Block::number;
            b.marker = line.upToFirstOccurrenceOf (".", true, false);
            b.text = line.fromFirstOccurrenceOf (".", false, false).trim();
            b.section = juce::jmax (0, section);
            out.push_back (b);
        }
        else
        {
            paragraph << (paragraph.isEmpty() ? "" : " ") << line.trim();
        }
    }
    flushPara();
    return out;
}

ManualView::ManualView()
{
    setBounds (22, 248, 1404, 830);
    setWantsKeyboardFocus (true);

    const auto md = juce::String::fromUTF8 (KoolaidBinary::MANUAL_md, KoolaidBinary::MANUAL_mdSize);
    blocks = parse (md);

    content = std::make_unique<Content> (*this);
    toc = std::make_unique<Toc> (*this);
    viewport = std::make_unique<Scroller> (*this);
    viewport->setViewedComponent (content.get(), false);
    viewport->setScrollBarsShown (true, false);
    viewport->setScrollBarThickness (12);
    viewport->setSingleStepSizes (40, 40);
    closeButton = std::make_unique<CloseButton>();
    closeButton->onClick = [this] { close(); };

    addAndMakeVisible (*toc);
    addAndMakeVisible (*viewport);
    addAndMakeVisible (*closeButton);
    setVisible (false);
    resized();
}

ManualView::~ManualView()
{
    viewport->setViewedComponent (nullptr, false);
}

void ManualView::open()
{
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void ManualView::close()
{
    setVisible (false);
    if (onClose) onClose();
}

bool ManualView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

void ManualView::resized()
{
    if (viewport == nullptr || closeButton == nullptr)
        return;   // called by setBounds() before the children exist
    const auto b = getLocalBounds();
    closeButton->setBounds (b.getRight() - 150, 14, 132, 38);
    toc->setBounds (18, (int) headerH + 12, tocWidth, (int) sections.size() * (int) Toc::rowH + 8);
    viewport->setBounds (tocWidth + 40, (int) headerH + 8, b.getWidth() - tocWidth - 52, b.getHeight() - (int) headerH - 20);
    layoutBlocks (viewport->getMaximumVisibleWidth());
    toc->setSize (tocWidth, (int) sections.size() * (int) Toc::rowH + 8);
}

void ManualView::layoutBlocks (int width)
{
    if (width <= 0 || width == laidOutWidth) return;
    laidOutWidth = width;
    const auto& sk = Skin::get();
    sections.clear();
    float y = 4.0f;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        auto& b = blocks[i];
        juce::AttributedString as;
        float before = 0.0f, after = 10.0f;
        switch (b.kind)
        {
            case Block::h1:
                as = styled (b.text, sk.heavyItalic (36.0f), sk.heavyItalic (36.0f), col::yellow, col::yellow);
                before = i == 0 ? 0.0f : 26.0f; after = 14.0f; b.indent = 0.0f;
                break;
            case Block::h2:
                as = styled (b.text, sk.heavyItalic (25.0f), sk.heavyItalic (25.0f), col::cyan, col::cyan);
                before = 10.0f; after = 6.0f; b.indent = 0.0f;
                break;
            case Block::para:
                as = styled (b.text, sk.medium (20.0f), sk.bold (20.0f), col::text.withAlpha (0.92f), juce::Colours::white);
                after = 14.0f; b.indent = 0.0f;
                break;
            case Block::bullet:
            case Block::number:
                as = styled (b.text, sk.medium (20.0f), sk.bold (20.0f), col::text.withAlpha (0.92f), juce::Colours::white);
                after = 8.0f; b.indent = b.kind == Block::bullet ? 26.0f : 42.0f;
                break;
        }
        y += before;
        const float w = (float) width - b.indent - 14.0f;
        b.layout.createLayout (as, juce::jmax (100.0f, w));
        b.y = y;
        b.h = b.layout.getHeight() + (b.kind == Block::h1 ? 8.0f : 0.0f);
        if (b.kind == Block::h1)
            sections.push_back ({ b.text, b.y });
        y += b.h + after;
    }
    content->setSize (width, (int) std::ceil (y + 40.0f));
}

int ManualView::currentSection() const
{
    const float top = (float) viewport->getViewPositionY() + 30.0f;
    int cur = 0;
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sections[(size_t) i].second <= top) cur = i;
    // at the very bottom, highlight the last section
    if (viewport->getViewPositionY() + viewport->getViewHeight() >= content->getHeight() - 2)
        cur = (int) sections.size() - 1;
    return cur;
}

void ManualView::scrollToSection (int index)
{
    if (index < 0 || index >= (int) sections.size()) return;
    viewport->setViewPosition (0, (int) sections[(size_t) index].second);
    toc->repaint();
}

void ManualView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 5.0f), 18.0f);
    juce::ColourGradient bg (juce::Colour (0xff23173f), 0, r.getY(), juce::Colour (0xff100a1f), 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 18.0f);

    // neon frame
    g.setColour (col::cyan.withAlpha (0.25f));
    g.drawRoundedRectangle (r.reduced (1.0f), 18.0f, 7.0f);
    g.setColour (col::cyan);
    g.drawRoundedRectangle (r.reduced (2.5f), 17.0f, 2.5f);
    g.setColour (col::hotPink.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (7.0f), 13.0f, 1.2f);

    // title bar
    juce::Rectangle<float> title (22.0f, 10.0f, 700.0f, headerH - 14.0f);
    g.setFont (Skin::get().heavyItalic (40.0f));
    g.setColour (juce::Colour (0xff3a0726));
    g.drawText ("KOOLAID MAN  MANUAL", title.translated (3.0f, 3.0f), juce::Justification::centredLeft);
    juce::ColourGradient tg (col::yellow, 0, title.getY() + 8.0f, col::hotPink, 0, title.getBottom() - 4.0f, false);
    g.setGradientFill (tg);
    g.drawText ("KOOLAID MAN  MANUAL", title, juce::Justification::centredLeft);

    drawShadowText (g, "Esc or CLOSE to get back to the drop", { 520.0f, 10.0f, 560.0f, headerH - 14.0f },
                    Skin::get().semibold (19.0f), juce::Justification::centredRight, col::textDim);

    // divider between contents and text
    g.setColour (col::lavender.withAlpha (0.35f));
    g.fillRect ((float) tocWidth + 28.0f, headerH + 12.0f, 1.5f, (float) getHeight() - headerH - 30.0f);
    g.setColour (col::lavender.withAlpha (0.25f));
    g.fillRect (18.0f, headerH, (float) getWidth() - 36.0f, 1.5f);
}
