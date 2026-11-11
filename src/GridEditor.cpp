#include "GridEditor.h"

using namespace skin;

namespace
{
    constexpr int numCells = 16;
    constexpr float headerH = 44.0f, footerH = 46.0f;
}

/** A small rounded button (preset shapes, Done). */
class GridEditor::Chip : public juce::Component
{
public:
    Chip (juce::String t, bool primary, std::function<void()> fn) : text (std::move (t)), isPrimary (primary), onClick (std::move (fn))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (isPrimary)
        {
            juce::ColourGradient grad (juce::Colour (0xffff8ad6), 0, r.getY(), col::hotPink, 0, r.getBottom(), false);
            g.setGradientFill (grad);
        }
        else
            g.setColour (isMouseOver() ? col::lavender.withAlpha (0.4f) : col::lavender.withAlpha (0.2f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (isPrimary ? juce::Colours::white.withAlpha (0.7f) : col::lavender.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
        drawShadowText (g, text, r, Skin::get().bold (16.0f), juce::Justification::centred,
                        isPrimary ? juce::Colour (0xff3a0726) : col::text, isPrimary ? juce::Colours::transparentBlack : col::shadow);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
    juce::String text;
    bool isPrimary;
    std::function<void()> onClick;
};

GridEditor::GridEditor()
{
    setBounds (470, 618, 948, 250);
    setWantsKeyboardFocus (true);
    setVisible (false);
}

GridEditor::~GridEditor() = default;

void GridEditor::layoutChips()
{
    chips.clear();
    auto add = [this] (const juce::String& t, bool primary, std::function<void()> fn)
    {
        chips.push_back (std::make_unique<Chip> (t, primary, std::move (fn)));
        addAndMakeVisible (*chips.back());
    };
    if (mode == Mode::curve)
    {
        auto preset = [this] (std::function<double (double)> f)
        {
            return [this, f]
            {
                for (int i = 0; i < numCells; ++i) values[(size_t) i] = (float) juce::jlimit (0.0, 1.0, f (i / 15.0));
                commit();
                repaint();
            };
        };
        add ("LINEAR", false, preset ([] (double t) { return t; }));
        add ("EASE IN", false, preset ([] (double t) { return std::pow (t, 2.2); }));
        add ("EASE OUT", false, preset ([] (double t) { return 1.0 - std::pow (1.0 - t, 2.2); }));
        add ("S-CURVE", false, preset ([] (double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }));
        add ("LATE RUSH", false, preset ([] (double t) { return std::pow (t, 5.0); }));
        add ("SWELL & DIP", false, preset ([] (double t) { return t < 0.8 ? std::pow (t / 0.8, 1.5) : 1.0 - (t - 0.8) * 2.5; }));
    }
    else
    {
        auto preset = [this] (juce::uint32 b) { return [this, b] { bits = b; commit(); repaint(); }; };
        add ("ALL ON", false, preset (0xffffu));
        add ("EVERY OTHER", false, preset (0x5555u));
        add ("OFFBEATS", false, preset (0xaaaau));
        add ("TRESILLO", false, preset (0x4949u));     // 3 + 3 + 2
        add ("GALLOP", false, preset (0xddddu));       // x . x x
        add ("BUILD", false, preset (0xf751u));        // sparse to dense
        add ("RANDOM", false, [this]
        {
            auto& r = juce::Random::getSystemRandom();
            juce::uint32 b = 1u;                          // always start on the one
            for (int i = 1; i < numSteps; ++i)
                if (r.nextFloat() < 0.6f) b |= 1u << i;
            bits = b;
            commit();
            repaint();
        });
        add ("CLEAR", false, preset (0u));
    }
    add ("DONE", true, [this] { close(); });
    resized();
}

void GridEditor::openCurve (const juce::String& t, const Values& v, std::function<void (const Values&)> onChange)
{
    mode = Mode::curve;
    title = t;
    values = v;
    curveChanged = std::move (onChange);
    patternChanged = nullptr;
    layoutChips();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    repaint();
}

void GridEditor::openPattern (const juce::String& t, juce::uint32 b, int steps, std::function<void (juce::uint32)> onChange)
{
    mode = Mode::pattern;
    title = t;
    bits = b;
    numSteps = juce::jlimit (1, numCells, steps);
    patternChanged = std::move (onChange);
    curveChanged = nullptr;
    layoutChips();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    repaint();
}

void GridEditor::close()
{
    setVisible (false);
    if (onClose) onClose();
}

bool GridEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey) { close(); return true; }
    return false;
}

void GridEditor::resized()
{
    const auto b = getLocalBounds();
    int x = 16;
    const int y = b.getBottom() - (int) footerH + 8;
    for (auto& c : chips)
    {
        const int w = c->isPrimary ? 110 : juce::jmax (84, (int) juce::GlyphArrangement::getStringWidth (Skin::get().bold (16.0f), c->text) + 28);
        if (c->isPrimary) c->setBounds (b.getRight() - w - 16, y, w, 30);
        else { c->setBounds (x, y, w, 30); x += w + 8; }
    }
}

juce::Rectangle<float> GridEditor::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (headerH).withTrimmedBottom (footerH).reduced (18.0f, 4.0f);
}

int GridEditor::cellAt (float x) const
{
    const auto a = gridArea();
    const int cells = mode == Mode::pattern ? numSteps : numCells;
    return juce::jlimit (0, cells - 1, (int) ((x - a.getX()) / a.getWidth() * cells));
}

void GridEditor::applyAt (juce::Point<float> p, bool firstClick)
{
    const auto a = gridArea();
    const int cell = cellAt (p.x);
    if (mode == Mode::curve)
    {
        const float v = juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
        // fill any cells skipped by a fast drag
        if (lastCell >= 0 && std::abs (cell - lastCell) > 1)
        {
            const float v0 = values[(size_t) lastCell];
            const int dir = cell > lastCell ? 1 : -1;
            for (int c = lastCell + dir; c != cell; c += dir)
                values[(size_t) c] = v0 + (v - v0) * (float) (c - lastCell) / (float) (cell - lastCell);
        }
        values[(size_t) cell] = v;
    }
    else
    {
        if (firstClick) paintOn = ((bits >> cell) & 1u) == 0;   // click toggles; dragging paints the same state
        const juce::uint32 mask = 1u << cell;
        bits = paintOn ? (bits | mask) : (bits & ~mask);
        // keep the steps beyond numSteps in sync (patterns repeat per bar)
    }
    lastCell = cell;
    repaint();
}

void GridEditor::mouseDown (const juce::MouseEvent& e)
{
    lastCell = -1;
    if (gridArea().expanded (0.0f, 6.0f).contains (e.position))
        applyAt (e.position, true);
}

void GridEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (gridArea().expanded (0.0f, 40.0f).contains (e.position.withY (juce::jlimit (gridArea().getY(), gridArea().getBottom(), e.position.y))))
        applyAt (e.position.withY (juce::jlimit (gridArea().getY() - 10.0f, gridArea().getBottom() + 10.0f, e.position.y)), false);
}

void GridEditor::mouseUp (const juce::MouseEvent&)
{
    if (lastCell >= 0) commit();
    lastCell = -1;
}

void GridEditor::commit()
{
    if (mode == Mode::curve && curveChanged) curveChanged (values);
    if (mode == Mode::pattern && patternChanged) patternChanged (bits);
}

void GridEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 4.0f), 16.0f);
    juce::ColourGradient bg (col::panelTop, 0, r.getY(), col::panelBot, 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (col::cyan);
    g.drawRoundedRectangle (r.reduced (1.5f), 15.0f, 2.2f);
    g.setColour (col::hotPink.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (5.5f), 12.0f, 1.0f);

    drawShadowText (g, title, { 20.0f, 8.0f, (float) getWidth() - 40.0f, headerH - 12.0f }, Skin::get().heavyItalic (24.0f),
                    juce::Justification::centredLeft, col::text);
    drawShadowText (g, mode == Mode::curve ? "drag to draw the curve (start of the move on the left, the end on the right)"
                                           : "click steps on/off, drag to paint - the pattern repeats",
                    { 20.0f, 8.0f, (float) getWidth() - 40.0f, headerH - 12.0f }, Skin::get().semibold (16.0f),
                    juce::Justification::centredRight, col::textDim);

    const auto a = gridArea();
    g.setColour (juce::Colour (Skin::get().getTheme() == Theme::light ? 0xfff6f0ff : 0xff0c0a16));
    g.fillRoundedRectangle (a.expanded (6.0f, 4.0f), 8.0f);

    const int cells = mode == Mode::pattern ? numSteps : numCells;
    const float cw = a.getWidth() / (float) cells;
    for (int i = 0; i < cells; ++i)
    {
        auto cell = juce::Rectangle<float> (a.getX() + i * cw, a.getY(), cw, a.getHeight()).reduced (2.0f, 0.0f);
        // beat shading
        if ((i / juce::jmax (1, cells / 4)) % 2 == 0)
        {
            g.setColour (col::lavender.withAlpha (0.08f));
            g.fillRect (cell);
        }
        if (mode == Mode::curve)
        {
            const float v = values[(size_t) i];
            auto bar = cell.withTop (a.getBottom() - v * a.getHeight()).reduced (3.0f, 0.0f);
            juce::ColourGradient grad (juce::Colour (0xffff8ad6), 0, bar.getY(), col::hotPink.withAlpha (0.6f), 0, a.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, 3.0f);
        }
        else
        {
            const bool on = ((bits >> i) & 1u) != 0;
            auto pad = cell.reduced (3.0f, a.getHeight() * 0.18f);
            if (on)
            {
                juce::ColourGradient grad (juce::Colour (0xffdcff6a), 0, pad.getY(), juce::Colour (0xff7ad61e), 0, pad.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (pad, 6.0f);
            }
            else
            {
                g.setColour (col::lavender.withAlpha (0.25f));
                g.drawRoundedRectangle (pad, 6.0f, 1.5f);
            }
            drawShadowText (g, juce::String (i + 1), pad.withHeight (18.0f).translated (0.0f, pad.getHeight() + 4.0f),
                            Skin::get().semibold (13.0f), juce::Justification::centred, col::textDim);
        }
    }
    if (mode == Mode::curve)
    {
        juce::Path p;
        for (int i = 0; i < numCells; ++i)
        {
            const juce::Point<float> pt (a.getX() + (i + 0.5f) * cw, a.getBottom() - values[(size_t) i] * a.getHeight());
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (col::cyan);
        g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i < numCells; ++i)
            g.fillEllipse (a.getX() + (i + 0.5f) * cw - 3.5f, a.getBottom() - values[(size_t) i] * a.getHeight() - 3.5f, 7.0f, 7.0f);
    }
}
