#include "MoveStackView.h"

using namespace skin;

MoveStackView::MoveStackView (KoolaidProcessor& p) : proc (p)
{
    setBounds (28, 622, 404, 202);
}

juce::Rectangle<float> MoveStackView::rowBounds (int visibleRow) const
{
    return { 4.0f, 7.0f + visibleRow * 31.25f, 374.0f, 26.0f };
}

int MoveStackView::maxScroll() const
{
    return juce::jmax (0, (int) proc.getModel().moves.size() - visibleRows);
}

int MoveStackView::rowAt (juce::Point<float> p) const
{
    for (int i = 0; i < visibleRows; ++i)
        if (rowBounds (i).expanded (0.0f, 2.5f).contains (p))
        {
            const int idx = i + scroll;
            return idx < (int) proc.getModel().moves.size() ? idx : -1;
        }
    return -1;
}

int MoveStackView::selectedIndex() const
{
    const auto& moves = proc.getModel().moves;
    for (size_t i = 0; i < moves.size(); ++i)
        if (moves[i].uid == selectedUid)
            return (int) i;
    return -1;
}

void MoveStackView::select (int uid)
{
    selectedUid = uid;
    const int idx = selectedIndex();
    if (idx >= 0)
    {
        if (idx < scroll) scroll = idx;
        if (idx >= scroll + visibleRows) scroll = idx - visibleRows + 1;
    }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void MoveStackView::refresh()
{
    scroll = juce::jlimit (0, maxScroll(), scroll);
    if (selectedIndex() < 0)
    {
        const auto& moves = proc.getModel().moves;
        selectedUid = moves.empty() ? 0 : moves.front().uid;
    }
    repaint();
}

void MoveStackView::paint (juce::Graphics& g)
{
    if (patchVersion != Skin::get().version())
    {
        patch = Skin::get().makePatch ({ 31, 624, 380, 199 });
        patchVersion = Skin::get().version();
    }
    g.drawImageAt (patch, 3, 2);
    const auto& moves = proc.getModel().moves;
    const auto& sk = Skin::get();

    if (moves.empty())
    {
        drawShadowText (g, "No moves yet - hit + ADD MOVE or ROLL", getLocalBounds().toFloat().withTrimmedRight (30.0f),
                        sk.semibold (18.0f), juce::Justification::centred, col::textDim);
    }

    for (int i = 0; i < visibleRows; ++i)
    {
        const int idx = i + scroll;
        if (idx >= (int) moves.size()) break;
        const auto& m = moves[(size_t) idx];
        auto r = rowBounds (i);
        if (dragging && idx == dragFrom)
            r = r.withY (juce::jlimit (0.0f, (float) getHeight() - r.getHeight(), dragY - 13.0f));

        const bool selected = m.uid == selectedUid;
        const bool hovered = idx == hoverRow;

        juce::ColourGradient bg (selected ? col::rowSelTop : col::rowTop, 0, r.getY(),
                                 selected ? col::rowSelBot : col::rowBot, 0, r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (selected ? col::hotPink : (hovered ? col::rowHover : col::rowBorder));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, selected ? 2.0f : 1.0f);
        if (selected)
        {
            g.setColour (col::hotPink.withAlpha (0.25f));
            g.drawRoundedRectangle (r.expanded (1.5f), 6.5f, 2.0f);
        }

        // grip
        g.setColour (col::grip);
        for (int gy = 0; gy < 3; ++gy)
            for (int gx = 0; gx < 2; ++gx)
                g.fillEllipse (r.getX() + 13.0f + gx * 6.5f, r.getY() + 6.5f + gy * 5.5f, 3.6f, 3.6f);

        // checkbox
        juce::Rectangle<float> box (r.getX() + 42.0f, r.getY() + 1.0f, 25.0f, 24.0f);
        if (m.enabled)
        {
            juce::ColourGradient cg (juce::Colour (0xffff9ae0), 0, box.getY(), juce::Colour (0xffe452b8), 0, box.getBottom(), false);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (box, 4.0f);
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 6.0f, box.getCentreY());
            tick.lineTo (box.getCentreX() - 1.0f, box.getBottom() - 6.0f);
            tick.lineTo (box.getRight() - 5.0f, box.getY() + 5.0f);
            g.setColour (juce::Colour (0xff12081c));
            g.strokePath (tick, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (col::boxOff);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (col::hotPink.withAlpha (0.7f));
            g.drawRoundedRectangle (box.reduced (1.0f), 4.0f, 1.5f);
        }

        // label + arrow
        drawShadowText (g, m.stackLabel(), { r.getX() + 85.0f, r.getY(), 250.0f, r.getHeight() }, sk.wideBold (17.5f),
                        juce::Justification::centredLeft, m.enabled ? col::text : col::textDim);
        float tagRight = r.getRight() - 32.0f;
        auto badge = [&] (const juce::String& text, float w, juce::Colour fill, juce::Colour ink)
        {
            juce::Rectangle<float> tag (tagRight - w, r.getY() + 5.0f, w, r.getHeight() - 10.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (tag, tag.getHeight() * 0.5f);
            g.setColour (ink);
            g.setFont (sk.bold (13.0f));
            g.drawText (text, tag, juce::Justification::centred);
            tagRight -= w + 4.0f;
        };
        if (m.customRegion) badge ("TIMED", 52.0f, col::cyan.withAlpha (0.9f), juce::Colour (0xff0b2a33));
        if (m.tensionLink)  badge ("TENSION", 62.0f, col::yellow.withAlpha (0.92f), juce::Colour (0xff3a2a00));
        juce::Path tri;
        const float ax = r.getRight() - 18.0f, ay = r.getCentreY();
        tri.addTriangle (ax - 6.0f, ay - 3.0f, ax + 6.0f, ay - 3.0f, ax, ay + 4.0f);
        g.setColour (col::text);
        g.fillPath (tri);
    }

    if (dragging && dragTo >= 0)
    {
        const float y = 7.0f + (float) (dragTo - scroll) * 31.25f - 3.0f;
        g.setColour (col::cyan);
        g.fillRoundedRectangle (4.0f, y, 374.0f, 3.0f, 1.5f);
    }

    // scrollbar
    juce::Rectangle<float> track (389.0f, 6.0f, 11.0f, 192.0f);
    g.setColour (col::track);
    g.fillRoundedRectangle (track, 5.0f);
    const int count = juce::jmax (1, (int) moves.size());
    const float frac = juce::jmin (1.0f, (float) visibleRows / (float) count);
    const float thumbH = juce::jmax (24.0f, track.getHeight() * frac);
    const float pos = maxScroll() > 0 ? (float) scroll / (float) maxScroll() : 0.0f;
    juce::Rectangle<float> thumb (track.getX() + 1.5f, track.getY() + 1.5f + pos * (track.getHeight() - thumbH - 3.0f), 8.0f, thumbH);
    g.setColour (col::thumb);
    g.fillRoundedRectangle (thumb, 4.0f);
}

void MoveStackView::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.position);
    if (r != hoverRow) { hoverRow = r; repaint(); }
}

void MoveStackView::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragFrom = rowAt (e.position);
    dragTo = -1;
    if (dragFrom < 0) return;

    auto& moves = proc.getModel().moves;
    const auto r = rowBounds (dragFrom - scroll);
    const float x = e.position.x - r.getX();
    select (moves[(size_t) dragFrom].uid);

    if (x >= 40.0f && x <= 70.0f)          // checkbox
    {
        moves[(size_t) dragFrom].enabled = ! moves[(size_t) dragFrom].enabled;
        dragFrom = -1;
        if (onStackEdited) onStackEdited();
        repaint();
    }
    else if (x >= r.getWidth() - 34.0f)    // dropdown arrow
    {
        const int idx = dragFrom;
        dragFrom = -1;
        showRowMenu (idx);
    }
    else if (e.mods.isPopupMenu())
    {
        const int idx = dragFrom;
        dragFrom = -1;
        showRowMenu (idx);
    }
}

void MoveStackView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStartY() != 0 && std::abs (e.getDistanceFromDragStartY()) > 4)
        dragging = true;
    if (! dragging) return;
    dragY = e.position.y;
    const int n = (int) proc.getModel().moves.size();
    dragTo = juce::jlimit (0, n, scroll + (int) std::round ((dragY - 7.0f) / 31.25f));
    if (dragY < 4.0f && scroll > 0) --scroll;
    if (dragY > (float) getHeight() - 4.0f && scroll < maxScroll()) ++scroll;
    repaint();
}

void MoveStackView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragFrom >= 0 && dragTo >= 0)
    {
        auto& moves = proc.getModel().moves;
        const auto m = moves[(size_t) dragFrom];
        int to = dragTo > dragFrom ? dragTo - 1 : dragTo;
        if (to != dragFrom)
        {
            moves.erase (moves.begin() + dragFrom);
            moves.insert (moves.begin() + juce::jlimit (0, (int) moves.size(), to), m);
            if (onStackEdited) onStackEdited();
        }
    }
    dragging = false;
    dragFrom = dragTo = -1;
    repaint();
}

void MoveStackView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int ns = juce::jlimit (0, maxScroll(), scroll + (w.deltaY < 0 ? 1 : -1));
    if (ns != scroll) { scroll = ns; repaint(); }
}

void MoveStackView::showRowMenu (int index)
{
    auto& moves = proc.getModel().moves;
    if (index < 0 || index >= (int) moves.size()) return;
    const auto m = moves[(size_t) index];
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (km::typeName (m.type)).toUpperCase());
    const auto modes = km::modeNames (m.type);
    for (int i = 0; i < modes.size(); ++i)
        menu.addItem (100 + i, modes[i], true, m.mode == i);
    menu.addSeparator();
    menu.addItem (1, "Move Up", index > 0);
    menu.addItem (2, "Move Down", index < (int) moves.size() - 1);
    menu.addItem (3, "Duplicate");
    menu.addItem (4, "Randomize This Move");
    menu.addItem (5, m.enabled ? "Bypass" : "Enable");
    menu.addItem (7, "Reset Timing (Auto)", m.customRegion);
    menu.addItem (8, "Link to Tension", true, m.tensionLink);
    {
        juce::PopupMenu curves;
        const auto& names = km::curveNames();
        for (int i = 0; i < names.size() - 1; ++i)
            curves.addItem (200 + i, names[i], true, m.curve == i);
        curves.addSeparator();
        curves.addItem (200 + km::curveCustom, "Custom - Draw It...", true, m.curve == km::curveCustom);
        menu.addSubMenu ("Curve (how it ramps)", curves, km::moveHasRamp (m));
    }
    menu.addSeparator();
    menu.addItem (6, "Delete");

    const int uid = m.uid;
    menu.showMenuAsync (menuOptionsFor (this), [this, uid] (int result)
    {
        auto& mv = proc.getModel().moves;
        int idx = -1;
        for (size_t i = 0; i < mv.size(); ++i) if (mv[i].uid == uid) idx = (int) i;
        if (idx < 0 || result == 0) return;
        auto& m = mv[(size_t) idx];
        if (result >= 200)
        {
            if (result - 200 == km::curveCustom) { if (onEditCurve) onEditCurve (uid); return; }
            m.curve = result - 200;
        }
        else if (result >= 100) m.mode = result - 100;
        else if (result == 1 && idx > 0) std::swap (mv[(size_t) idx], mv[(size_t) idx - 1]);
        else if (result == 2 && idx + 1 < (int) mv.size()) std::swap (mv[(size_t) idx], mv[(size_t) idx + 1]);
        else if (result == 3) { auto copy = m; copy.uid = 0; mv.insert (mv.begin() + idx + 1, copy); }
        else if (result == 4)
        {
            juce::Random r;
            m.knob = 0.25f + 0.7f * r.nextFloat();
            m.knob2 = 0.25f + 0.6f * r.nextFloat();
            m.mode = r.nextInt (juce::jmax (1, km::modeNames (m.type).size()));
            m.division = juce::jlimit (1, 7, m.division + r.nextInt (3) - 1);
            m.seed = (juce::uint32) r.nextInt (1 << 30);
            if (m.type == km::MoveType::Pitch)
            {
                const int choices[] = { -24, -12, -7, -5, 5, 7, 12 };
                m.semitones = choices[r.nextInt (7)];
            }
        }
        else if (result == 5) m.enabled = ! m.enabled;
        else if (result == 6) mv.erase (mv.begin() + idx);
        else if (result == 7) m.customRegion = false;
        else if (result == 8) m.tensionLink = ! m.tensionLink;
        if (onStackEdited) onStackEdited();
        refresh();
    });
}
