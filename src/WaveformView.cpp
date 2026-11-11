#include "WaveformView.h"
#include "MoveProcessors.h"

using namespace skin;

namespace
{
    constexpr float rulerBaseline = 30.0f;   // local y of the bar numbers
    constexpr float zoneTop = 37.0f;
    constexpr float waveCentre = 99.0f;
    constexpr float waveHalf = 57.0f;
}

WaveformView::WaveformView (KoolaidProcessor& p) : proc (p)
{
    setBounds (18, 350, 1408, 168);
    refresh();
}

WaveformView::Peaks WaveformView::makePeaks (const std::vector<Segment>& segments, juce::int64 total, double sr)
{
    Peaks p;
    p.numSamples = total;
    p.sampleRate = sr;
    const auto buckets = (int) ((total + p.bucket - 1) / p.bucket);
    p.mn.assign ((size_t) buckets, 0.0f);
    p.mx.assign ((size_t) buckets, 0.0f);
    for (int k = 0; k < buckets; ++k)
    {
        float lo = 0.0f, hi = 0.0f;
        const juce::int64 s0 = (juce::int64) k * p.bucket, s1 = juce::jmin (total, s0 + p.bucket);
        for (auto& seg : segments)
        {
            const auto a = juce::jmax (s0, seg.start), b = juce::jmin (s1, seg.start + seg.length);
            if (b <= a || seg.buffer == nullptr) continue;
            for (int ch = 0; ch < seg.buffer->getNumChannels(); ++ch)
            {
                auto r = juce::FloatVectorOperations::findMinAndMax (seg.buffer->getReadPointer (ch, (int) (a - seg.start)), (int) (b - a));
                lo = juce::jmin (lo, r.getStart());
                hi = juce::jmax (hi, r.getEnd());
            }
        }
        p.mn[(size_t) k] = lo;
        p.mx[(size_t) k] = hi;
    }
    return p;
}

double WaveformView::clipBeats() const
{
    double beats = proc.getModel().clipLengthBeats();
    if (auto r = proc.getResult(); r != nullptr && r->source == proc.getModel().music && r->samplesPerBeat > 0)
        beats = juce::jmax (beats, r->length / r->samplesPerBeat);   // after-drop tail
    return beats;
}

void WaveformView::refresh()
{
    auto& m = proc.getModel();
    const auto result = proc.getResult();
    std::vector<Segment> segments;
    juce::int64 totalLen = 0;
    double sr = 48000.0;
    const void* sourceKey = nullptr;
    auto whole = [] (const juce::AudioBuffer<float>& b) { return Segment { &b, 0, b.getNumSamples() }; };

    if (tab == 1)
    {
        if (m.drums != nullptr) { segments = { whole (m.drums->audio) }; totalLen = m.drums->audio.getNumSamples(); sr = m.drums->sampleRate; sourceKey = m.drums.get(); }
    }
    else if (result != nullptr && result->source != nullptr && result->source == m.music)
    {
        // renders keep only the changed region; before it the output is the clip itself
        if (tab == 2)
            segments = { { &result->fx, result->regionStart, result->fx.getNumSamples() } };
        else
            segments = { { &result->source->audio, 0, result->regionStart },
                         { &result->mix, result->regionStart, result->mix.getNumSamples() } };
        totalLen = result->length;
        sr = result->sampleRate;
        sourceKey = result.get();
    }
    else if (m.music != nullptr)
    {
        segments = { whole (m.music->audio) };
        totalLen = m.music->audio.getNumSamples();
        sr = m.music->sampleRate;
        sourceKey = m.music.get();
    }

    if (sourceKey != peaksSource || tab != peaksTab)
    {
        peaks = ! segments.empty() ? makePeaks (segments, totalLen, sr) : Peaks();
        peaksSource = sourceKey;
        peaksTab = tab;
    }

    const double total = juce::jmax (1.0, clipBeats());
    if (fitted || viewStart + viewLen > total + 1.0e-6)
    {
        if (fitted) { viewStart = 0.0; viewLen = total; }
        viewLen = juce::jlimit (1.0, total, viewLen);
        viewStart = juce::jlimit (0.0, total - viewLen, viewStart);
    }
    repaint();
}

void WaveformView::zoomBy (double factor)
{
    const double total = juce::jmax (1.0, clipBeats());
    const double centre = viewStart + viewLen * 0.5;
    viewLen = juce::jlimit (juce::jmin (total, 2.0), total, viewLen * factor);
    viewStart = juce::jlimit (0.0, total - viewLen, centre - viewLen * 0.5);
    fitted = std::abs (viewLen - total) < 1.0e-6;
    repaint();
}

void WaveformView::zoomToZone()
{
    const auto& m = proc.getModel();
    const double total = juce::jmax (1.0, clipBeats());
    const double bpb = m.beatsPerBar();
    double after = 0.5 * bpb;
    for (auto& mv : m.moves)
        if (km::playsAfterDrop (mv.type))
            after = juce::jmax (after, km::moveRegion (mv, m.zoneEnd - m.zoneStart, bpb).second + 0.25 * bpb);
    const double a = juce::jmax (0.0, m.zoneStart - 0.5 * bpb);
    const double b = juce::jmin (total, m.zoneEnd + after);
    if (b - a < 1.0) return;
    viewStart = a;
    viewLen = b - a;
    fitted = std::abs (viewLen - total) < 1.0e-6;
    repaint();
}

void WaveformView::zoomFit()
{
    fitted = true;
    refresh();
}

void WaveformView::showMessage (const juce::String& msg, int millis)
{
    message = msg;
    messageUntil = juce::Time::getMillisecondCounter() + (juce::uint32) millis;
    repaint();
}

void WaveformView::tick()
{
    ++blink;
    const bool active = proc.isPlaying() || proc.isSyncPlayheadActive();
    float x = -1.0f;
    if (active && proc.hasMusic())
    {
        const auto& m = proc.getModel();
        const double beat = (double) proc.getPlayheadSample() / m.samplesPerBeat (m.music->sampleRate);
        x = beatToX (beat);
    }
    const bool msgChanged = message.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil;
    if (msgChanged) message.clear();
    if (std::abs (x - lastPlayheadX) > 0.5f || msgChanged || proc.isCaptureArmed() || proc.isRendering())
    {
        lastPlayheadX = x;
        repaint();
    }
}

//==============================================================================
juce::Rectangle<float> WaveformView::contentArea() const
{
    return { 26.0f, 0.0f, (float) getWidth() - 52.0f, (float) getHeight() };
}

float WaveformView::beatToX (double beat) const
{
    const auto c = contentArea();
    return c.getX() + (float) ((beat - viewStart) / juce::jmax (1.0e-6, viewLen)) * c.getWidth();
}

double WaveformView::xToBeat (float x) const
{
    const auto c = contentArea();
    return viewStart + (double) ((x - c.getX()) / c.getWidth()) * viewLen;
}

double WaveformView::snap (double beat, const juce::ModifierKeys& mods) const
{
    if (mods.isShiftDown())
        return beat;
    const auto& m = proc.getModel();
    const int s = proc.getSettings().snap;
    const double grid = s == 0 ? m.beatsPerBar() : (s == 1 ? 4.0 / m.tsDen : 0.25);
    return std::round (beat / grid) * grid;
}

WaveformView::Hit WaveformView::hitTest (juce::Point<float> p) const
{
    if (! proc.hasMusic()) return Hit::none;
    const auto& m = proc.getModel();
    const float x0 = beatToX (m.zoneStart), x1 = beatToX (m.zoneEnd);
    if (std::abs (p.x - x0) < 9.0f) return Hit::leftEdge;
    if (std::abs (p.x - x1) < 9.0f) return Hit::rightEdge;
    if (p.x > x0 && p.x < x1) return Hit::inside;
    return Hit::outside;
}

//==============================================================================
void WaveformView::drawWave (juce::Graphics& g, const Peaks& pk, juce::Colour outside, juce::Colour inside, float cy, float hh)
{
    if (pk.mx.empty()) return;
    const auto& m = proc.getModel();
    const double spb = m.samplesPerBeat (pk.sampleRate);
    const auto c = contentArea();
    const float zx0 = beatToX (m.zoneStart), zx1 = beatToX (m.zoneEnd);
    const int xStart = (int) c.getX(), xEnd = (int) c.getRight();
    const double samplesPerPx = viewLen * spb / c.getWidth();

    juce::RectangleList<float> outRects, inRects;
    for (int x = xStart; x < xEnd; ++x)
    {
        const double s0 = (viewStart * spb) + (x - c.getX()) * samplesPerPx;
        const double s1 = s0 + samplesPerPx;
        if (s1 <= 0 || s0 >= (double) pk.numSamples) continue;
        int b0 = (int) (s0 / pk.bucket), b1 = (int) (s1 / pk.bucket);
        b0 = juce::jlimit (0, (int) pk.mx.size() - 1, b0);
        b1 = juce::jlimit (b0, (int) pk.mx.size() - 1, b1);
        float lo = 0, hi = 0;
        for (int b = b0; b <= b1; ++b) { lo = juce::jmin (lo, pk.mn[(size_t) b]); hi = juce::jmax (hi, pk.mx[(size_t) b]); }
        lo = juce::jmax (-1.0f, lo); hi = juce::jmin (1.0f, hi);
        const float top = cy - hi * hh, bot = cy - lo * hh;
        juce::Rectangle<float> r ((float) x, top, 1.0f, juce::jmax (1.0f, bot - top));
        ((float) x >= zx0 && (float) x < zx1 ? inRects : outRects).addWithoutMerging (r);
    }
    g.setColour (outside);
    g.fillRectList (outRects);
    g.setColour (inside);
    g.fillRectList (inRects);
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto& m = proc.getModel();
    const auto& s = proc.getSettings();
    auto bg = juce::Rectangle<float> (2.0f, 5.0f, (float) getWidth() - 4.0f, (float) getHeight() - 7.0f);
    if (stripVersion != Skin::get().version())
    {
        topStrip = Skin::get().makePatch ({ 680, 350, 526, 7 });
        bottomStrip = Skin::get().makePatch ({ 680, 513, 526, 6 });
        stripVersion = Skin::get().version();
    }
    g.drawImageAt (topStrip, 680 - getX(), 0);
    g.drawImageAt (bottomStrip, 680 - getX(), 513 - getY());
    g.setColour (col::display);
    g.fillRoundedRectangle (bg, 7.0f);

    if (! proc.hasMusic() && tab != 1)
    {
        auto r = bg.reduced (20.0f);
        drawShadowText (g, proc.isCaptureArmed() ? "CAPTURE ARMED" : "DRAG AUDIO HERE", r.withTrimmedBottom (54.0f).withTrimmedTop (20.0f),
                        Skin::get().heavyItalic (34.0f), juce::Justification::centred,
                        proc.isCaptureArmed() ? (blink / 8 % 2 ? col::hotPink : col::text) : col::lavender);
        drawShadowText (g, proc.isCaptureArmed() ? "Press play in your DAW - recording starts on the next bar line and stops when you stop."
                                                 : "Drop the bars before your drop here (WAV / AIFF / FLAC / MP3)  -  or click to LOAD  -  or arm CAPTURE and play your session",
                        r.withTrimmedTop (64.0f), Skin::get().semibold (18.0f), juce::Justification::centredTop, col::textDim);
        if (! proc.isCaptureArmed())
            drawShadowText (g, "New here? Open the MANUAL from the gear menu, or click KOOLAID MAN next to PRESET.",
                            r.withTrimmedTop (92.0f), Skin::get().semibold (17.0f), juce::Justification::centredTop, col::cyan.withAlpha (0.8f));
        return;
    }

    const double total = clipBeats();
    const double bpb = m.beatsPerBar();
    const auto c = contentArea();
    g.saveState();
    g.reduceClipRegion (bg.toNearestInt());

    // bar grid + ruler
    if (total > 0)
    {
        const double pxPerBar = c.getWidth() / viewLen * bpb;
        const int labelEvery = pxPerBar > 60 ? 1 : (pxPerBar > 30 ? 2 : (pxPerBar > 14 ? 4 : 8));
        const int firstBar = (int) std::floor (viewStart / bpb);
        for (int bar = firstBar; bar * bpb <= viewStart + viewLen + 1.0e-6; ++bar)
        {
            const float x = beatToX (bar * bpb);
            g.setColour (col::gridLine);
            g.fillRect (x, zoneTop, 1.0f, (float) getHeight() - zoneTop - 4.0f);
            if (bar % labelEvery == 0)
                drawShadowText (g, juce::String (m.clipStartBar + bar) + ".1", { x + 2.0f, rulerBaseline - 14.0f, 60.0f, 18.0f },
                                Skin::get().bold (17.0f), juce::Justification::centredLeft);
            if (pxPerBar > 120)
                for (int b = 1; b < (int) std::round (bpb); ++b)
                {
                    const float bx = beatToX (bar * bpb + b);
                    g.setColour (col::gridLine.withMultipliedAlpha (2.2f));
                    g.fillRect (bx, rulerBaseline - 3.0f, 1.0f, 5.0f);
                }
        }
    }

    // zone body
    const float zx0 = beatToX (m.zoneStart), zx1 = beatToX (m.zoneEnd);
    const juce::Rectangle<float> zone (zx0, zoneTop, zx1 - zx0, (float) getHeight() - zoneTop - 3.0f);
    if (tab != 1)
    {
        juce::ColourGradient zg (col::zoneTop, 0, zone.getY(), col::zoneBot, 0, zone.getBottom(), false);
        g.setGradientFill (zg);
        g.fillRect (zone);
    }

    // waveform (a slim strip above the lanes on the timeline)
    const float wc = isTimeline() ? 54.0f : waveCentre;
    const float wh = isTimeline() ? 14.0f : waveHalf;
    if (tab == 1)
        drawWave (g, peaks, juce::Colour (0xffa6ec5a), juce::Colour (0xffc8ff7a), wc, wh);
    else if (tab == 2)
        drawWave (g, peaks, juce::Colour (0xff38d8f0), juce::Colour (0xff6ef6ff), wc, wh);
    else
        drawWave (g, peaks, col::waveOut, col::waveIn, wc, wh);
    if (isTimeline())
        drawLanes (g);

    // detected drum hits (drum tab, or music tab when analysing the mix)
    if (auto a = proc.getAnalysis(); a != nullptr && total > 0 && (tab == 1 || (tab == 0 && ! a->fromDrumStem)))
    {
        const double spbClip = m.samplesPerBeat (m.music != nullptr ? m.music->sampleRate : 48000.0);
        auto marks = [&] (const km::DrumHits& h, juce::Colour col, float y)
        {
            g.setColour (col);
            for (auto smp : h.onsets)
                g.fillRect (beatToX ((double) smp / spbClip) - 1.0f, y, 2.0f, 5.0f);
        };
        if (tab == 1)
        {
            marks (a->snare, col::hotPink, (float) getHeight() - 22.0f);
            marks (a->bass, col::yellow, (float) getHeight() - 16.0f);
            marks (a->hats, col::cyan, (float) getHeight() - 10.0f);
        }
    }

    // zone frame, handles and tab
    {
        g.setColour (col::hotPink.withAlpha (0.35f));
        g.drawRect (zone.expanded (1.5f), 3.0f);
        g.setColour (juce::Colour (0xffff5cc3));
        g.drawRect (zone, 2.0f);

        auto handle = [&] (float x, bool left, bool hot)
        {
            juce::Path tri;
            const float sz = hot ? 17.0f : 14.0f;
            if (left) tri.addTriangle (x, zoneTop - 4.0f, x + sz, zoneTop - 4.0f, x, zoneTop - 4.0f + sz);
            else      tri.addTriangle (x, zoneTop - 4.0f, x - sz, zoneTop - 4.0f, x, zoneTop - 4.0f + sz);
            g.setColour (juce::Colours::white);
            g.fillPath (tri);
            g.setColour (col::hotPink);
            g.strokePath (tri, juce::PathStrokeType (1.5f));
        };
        handle (zx0, true, hover == Hit::leftEdge || dragMode == Hit::leftEdge);
        handle (zx1, false, hover == Hit::rightEdge || dragMode == Hit::rightEdge);

        // "PRE-DROP (BUILD ZONE)" tab
        const juce::String label = zone.getWidth() > 300 ? "PRE-DROP (BUILD ZONE)" : (zone.getWidth() > 120 ? "BUILD ZONE" : "");
        if (label.isNotEmpty())
        {
            const auto f = Skin::get().heavyItalic (21.0f);
            const float tw = juce::jmin (zone.getWidth() - 40.0f, juce::GlyphArrangement::getStringWidth (f, label) + 70.0f);
            const float tx = zone.getCentreX() - tw * 0.5f;
            juce::Path tabPath;
            tabPath.startNewSubPath (tx, zoneTop);
            tabPath.lineTo (tx + 12.0f, 2.0f);
            tabPath.lineTo (tx + tw - 12.0f, 2.0f);
            tabPath.lineTo (tx + tw, zoneTop);
            tabPath.closeSubPath();
            juce::ColourGradient tg (juce::Colour (0xffff8ad6), 0, 2.0f, juce::Colour (0xffff3fae), 0, zoneTop, false);
            g.setGradientFill (tg);
            g.fillPath (tabPath);
            g.setColour (juce::Colour (0xff3a0726));
            g.setFont (f);
            g.drawText (label, juce::Rectangle<float> (tx, 3.0f, tw, zoneTop - 4.0f), juce::Justification::centred);
        }
    }

    // playhead
    if (lastPlayheadX >= c.getX() && lastPlayheadX <= c.getRight())
    {
        const auto phCol = proc.isSyncPlayheadActive() ? col::yellow : juce::Colours::white;
        g.setColour (phCol.withAlpha (0.35f));
        g.fillRect (lastPlayheadX - 2.0f, zoneTop - 2.0f, 5.0f, (float) getHeight() - zoneTop);
        g.setColour (phCol);
        g.fillRect (lastPlayheadX - 0.75f, zoneTop - 2.0f, 1.5f, (float) getHeight() - zoneTop);
    }
    g.restoreState();

    // status overlays
    juce::String status;
    juce::Colour statusCol = col::text;
    if (proc.isCapturing())          { status = "REC  -  capturing your session"; statusCol = (blink / 8) % 2 ? col::hotPink : col::text; }
    else if (proc.isCaptureArmed())  { status = "CAPTURE ARMED  -  press play in your DAW"; statusCol = (blink / 8) % 2 ? col::hotPink : col::text; }
    else if (message.isNotEmpty())   { status = message; }
    else if (tab == 1 && m.drums == nullptr) { status = "AUDIO 2: drop a drum / 808 stem here to steer the detection (optional)"; statusCol = col::textDim; }
    if (status.isNotEmpty())
    {
        const auto f = Skin::get().semibold (18.0f);
        const float w = juce::jmin (bg.getWidth() - 40.0f, juce::GlyphArrangement::getStringWidth (f, status) + 36.0f);
        juce::Rectangle<float> pill (bg.getCentreX() - w * 0.5f, bg.getBottom() - 34.0f, w, 26.0f);
        g.setColour (col::pill);
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (col::cyan.withAlpha (0.7f));
        g.drawRoundedRectangle (pill, 13.0f, 1.2f);
        drawShadowText (g, status, pill, f, juce::Justification::centred, statusCol);
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (isTimeline() && laneArea().contains (e.position))
    {
        const auto lh = laneHitTest (e.position);
        if (lh.index != hoverLane) { hoverLane = lh.index; repaint(); }
        setMouseCursor (lh.part == LanePart::start || lh.part == LanePart::end ? juce::MouseCursor::LeftRightResizeCursor
                        : (lh.part == LanePart::body ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
        if (hover != Hit::none) { hover = Hit::none; repaint(); }
        return;
    }
    if (hoverLane >= 0) { hoverLane = -1; repaint(); }
    const auto h = hitTest (e.position);
    if (h != hover) { hover = h; repaint(); }
    switch (h)
    {
        case Hit::leftEdge:
        case Hit::rightEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Hit::inside:    setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case Hit::none:      setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case Hit::outside:   setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.hasMusic())
    {
        if (onEmptyClicked) onEmptyClicked();
        return;
    }
    auto& m = proc.getModel();
    laneDrag = {};
    if (isTimeline() && laneArea().contains (e.position))
    {
        dragMode = Hit::none;
        laneDrag = laneHitTest (e.position);
        laneDragged = false;
        if (laneDrag.index >= 0)
        {
            const auto& mv = m.moves[(size_t) laneDrag.index];
            const auto reg = km::moveRegion (mv, m.zoneEnd - m.zoneStart, m.beatsPerBar());
            laneDragStartBeat = xToBeat (e.position.x);
            laneDragStart = reg.first;
            laneDragEnd = reg.second;
            if (onLaneSelected) onLaneSelected (mv.uid);
        }
        return;
    }
    dragMode = hitTest (e.position);
    dragStartBeat = xToBeat (e.position.x);
    dragZoneStart = m.zoneStart;
    dragZoneEnd = m.zoneEnd;
    if (dragMode == Hit::outside)
    {
        const double spb = m.samplesPerBeat (m.music->sampleRate);
        proc.seekTo ((juce::int64) (juce::jmax (0.0, dragStartBeat) * spb));
        lastPlayheadX = -2.0f;
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! proc.hasMusic()) return;
    auto& m = proc.getModel();
    const double total = clipBeats();
    const double beat = xToBeat (e.position.x);
    const double minLen = 0.25;

    if (laneDrag.index >= 0 && laneDrag.index < (int) m.moves.size() && laneDrag.part != LanePart::none)
    {
        if (! laneDragged && e.getDistanceFromDragStart() < 3) return;
        laneDragged = true;
        auto& mv = m.moves[(size_t) laneDrag.index];
        const bool after = km::playsAfterDrop (mv.type);
        const double Z = m.zoneEnd - m.zoneStart;
        const double lo = after ? 0.0 : -Z, hi = after ? 8.0 * m.beatsPerBar() : 0.0;
        const double drop = m.zoneEnd;
        double s = laneDragStart, en = laneDragEnd;
        const double delta = beat - laneDragStartBeat;
        if (laneDrag.part == LanePart::body)
        {
            const double len = en - s;
            s = juce::jlimit (lo, hi - len, snap (drop + laneDragStart + delta, e.mods) - drop);
            en = s + len;
        }
        else if (laneDrag.part == LanePart::start)
            s = juce::jlimit (lo, en - minLen, snap (drop + laneDragStart + delta, e.mods) - drop);
        else
            en = juce::jlimit (s + minLen, hi, snap (drop + laneDragEnd + delta, e.mods) - drop);
        mv.customRegion = true;
        mv.regionStart = s;
        mv.regionEnd = en;
        if (onLanesEdited) onLanesEdited();
        repaint();
        return;
    }
    switch (dragMode)
    {
        case Hit::leftEdge:
            m.zoneStart = juce::jlimit (0.0, m.zoneEnd - minLen, snap (beat, e.mods));
            break;
        case Hit::rightEdge:
            m.zoneEnd = juce::jlimit (m.zoneStart + minLen, total, snap (beat, e.mods));
            break;
        case Hit::inside:
        {
            const double len = dragZoneEnd - dragZoneStart;
            double ns = snap (dragZoneStart + (beat - dragStartBeat), e.mods);
            ns = juce::jlimit (0.0, juce::jmax (0.0, total - len), ns);
            m.zoneStart = ns;
            m.zoneEnd = ns + len;
            break;
        }
        case Hit::outside:
        {
            const double spb = m.samplesPerBeat (m.music->sampleRate);
            proc.seekTo ((juce::int64) (juce::jlimit (0.0, total, beat) * spb));
            return;
        }
        case Hit::none: return;
    }
    if (onZoneEdited) onZoneEdited();
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    laneDrag = {};
    dragMode = Hit::none;
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (isTimeline() && laneArea().contains (e.position) && ! (e.mods.isCommandDown() || e.mods.isCtrlDown()))
    {
        const int n = (int) proc.getModel().moves.size();
        const int visible = (int) (laneArea().getHeight() / laneHeight());
        const int ns = juce::jlimit (0, juce::jmax (0, n - visible), laneScroll + (w.deltaY < 0 ? 1 : -1));
        if (ns != laneScroll) { laneScroll = ns; repaint(); }
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (w.deltaY > 0 ? 0.8 : 1.25);
        return;
    }
    if (fitted) return;
    const double total = juce::jmax (1.0, clipBeats());
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    viewStart = juce::jlimit (0.0, total - viewLen, viewStart - d * viewLen * 0.5);
    repaint();
}

//==============================================================================
// Timeline lanes
//==============================================================================
juce::Colour WaveformView::moveColour (km::MoveType t)
{
    using T = km::MoveType;
    switch (t)
    {
        case T::Reverse:   return juce::Colour (0xffff8a3d);
        case T::Chops:     return juce::Colour (0xff22c7b8);
        case T::Stutter:   return juce::Colour (0xffe24bd2);
        case T::Pitch:     return juce::Colour (0xffe8c22c);
        case T::TapeStop:  return juce::Colour (0xff3d7dff);
        case T::Filter:    return juce::Colour (0xff7fd13b);
        case T::DrumMoves: return juce::Colour (0xffd8379b);
        case T::Bass808:   return juce::Colour (0xffff4b4b);
        case T::Hats:      return juce::Colour (0xff3fc2f0);
        case T::Atmos:     return juce::Colour (0xffa36bff);
        case T::Riser:     return juce::Colour (0xff35e09a);
        case T::RevCrash:  return juce::Colour (0xffffc23d);
        case T::Pump:      return juce::Colour (0xffff4fc4);
        case T::Gap:       return juce::Colour (0xff6c6cff);
        case T::Impact:    return juce::Colour (0xffff6a2b);
        case T::Boom:      return juce::Colour (0xffc05cff);
        default: break;
    }
    return col::lavender;
}

juce::Rectangle<float> WaveformView::laneArea() const
{
    return { contentArea().getX() - 20.0f, 72.0f, contentArea().getWidth() + 40.0f, (float) getHeight() - 77.0f };
}

float WaveformView::laneHeight() const
{
    const int n = juce::jmax (1, (int) proc.getModel().moves.size());
    return juce::jlimit (12.0f, 22.0f, laneArea().getHeight() / (float) n);
}

juce::Rectangle<float> WaveformView::laneBar (int index) const
{
    const auto& m = proc.getModel();
    const auto& mv = m.moves[(size_t) index];
    const auto reg = km::moveRegion (mv, m.zoneEnd - m.zoneStart, m.beatsPerBar());
    const float lh = laneHeight();
    const float y = laneArea().getY() + (float) (index - laneScroll) * lh;
    const float x0 = beatToX (m.zoneEnd + reg.first), x1 = beatToX (m.zoneEnd + reg.second);
    return { x0, y + 1.0f, juce::jmax (4.0f, x1 - x0), lh - 2.0f };
}

WaveformView::LaneHit WaveformView::laneHitTest (juce::Point<float> p) const
{
    const auto& moves = proc.getModel().moves;
    const float lh = laneHeight();
    const int index = laneScroll + (int) std::floor ((p.y - laneArea().getY()) / lh);
    if (index < 0 || index >= (int) moves.size()) return {};
    const auto bar = laneBar (index);
    LaneHit h;
    h.index = index;
    if (std::abs (p.x - bar.getX()) < 6.0f) h.part = LanePart::start;
    else if (std::abs (p.x - bar.getRight()) < 6.0f) h.part = LanePart::end;
    else if (bar.contains (p)) h.part = LanePart::body;
    return h;
}

void WaveformView::drawLanes (juce::Graphics& g)
{
    const auto& m = proc.getModel();
    const auto area = laneArea();
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    // divider between the waveform strip and the lanes
    g.setColour (col::gridLine.withMultipliedAlpha (3.0f));
    g.fillRect (area.getX(), area.getY() - 3.0f, area.getWidth(), 1.0f);

    if (m.moves.empty())
        drawShadowText (g, "No moves yet - add some, then drag them here to choose when they happen", area,
                        Skin::get().semibold (17.0f), juce::Justification::centred, col::textDim);

    const float lh = laneHeight();
    const auto font = Skin::get().bold (juce::jmin (16.0f, lh - 3.0f));
    for (int i = laneScroll; i < (int) m.moves.size(); ++i)
    {
        const auto bar = laneBar (i);
        if (bar.getY() > area.getBottom()) break;
        const auto& mv = m.moves[(size_t) i];
        auto c = moveColour (mv.type);
        if (! mv.enabled) c = c.withSaturation (0.15f).withAlpha (0.55f);
        const bool sel = mv.uid == selectedUid, hot = i == hoverLane;

        if (mv.customRegion)
        {
            juce::ColourGradient grad (c.brighter (0.25f), 0, bar.getY(), c.darker (0.25f), 0, bar.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (c.brighter (0.6f));
            g.drawRoundedRectangle (bar.reduced (0.5f), 4.0f, 1.0f);
            // grab handles
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRoundedRectangle (bar.getX() + 2.0f, bar.getY() + 3.0f, 2.5f, bar.getHeight() - 6.0f, 1.0f);
            g.fillRoundedRectangle (bar.getRight() - 4.5f, bar.getY() + 3.0f, 2.5f, bar.getHeight() - 6.0f, 1.0f);
        }
        else
        {
            // auto: translucent with a dashed outline
            g.setColour (c.withMultipliedAlpha (0.28f));
            g.fillRoundedRectangle (bar, 4.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (bar.reduced (0.5f), 4.0f);
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (c);
            g.fillPath (dashed);
        }
        if (sel || hot)
        {
            g.setColour (sel ? juce::Colours::white : juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (bar.expanded (1.0f), 5.0f, sel ? 1.8f : 1.0f);
        }
        juce::String label = juce::String (km::typeName (mv.type));
        if (! mv.customRegion) label << "  (auto)";
        const auto textCol = mv.customRegion ? juce::Colours::white : (Skin::get().getTheme() == skin::Theme::light ? col::text : juce::Colours::white);
        auto tr = bar.reduced (7.0f, 0.0f);
        if (juce::GlyphArrangement::getStringWidth (font, label) > tr.getWidth())
            tr = tr.withWidth (400.0f);   // spill past short bars so the name stays readable
        drawShadowText (g, label, tr, font, juce::Justification::centredLeft, textCol,
                        mv.customRegion ? juce::Colour (0x8c000000) : col::shadow);
    }
    // more lanes than fit: hint at scrolling
    const int visible = (int) (area.getHeight() / lh);
    if ((int) m.moves.size() > visible)
        drawShadowText (g, juce::String (laneScroll + 1) + "-" + juce::String (juce::jmin ((int) m.moves.size(), laneScroll + visible))
                        + " of " + juce::String (m.moves.size()) + "  (scroll)",
                        area.withTrimmedRight (6.0f), Skin::get().semibold (14.0f), juce::Justification::bottomRight, col::textDim);
    g.restoreState();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isTimeline() || ! laneArea().contains (e.position)) return;
    const auto h = laneHitTest (e.position);
    auto& m = proc.getModel();
    if (h.index >= 0 && h.index < (int) m.moves.size() && m.moves[(size_t) h.index].customRegion)
    {
        m.moves[(size_t) h.index].customRegion = false;   // back to its natural spot
        if (onLanesEdited) onLanesEdited();
        repaint();
    }
}
