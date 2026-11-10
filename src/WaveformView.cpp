#include "WaveformView.h"

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

WaveformView::Peaks WaveformView::makePeaks (const juce::AudioBuffer<float>& b, double sr)
{
    Peaks p;
    p.numSamples = b.getNumSamples();
    p.sampleRate = sr;
    const int n = b.getNumSamples();
    const int buckets = (n + p.bucket - 1) / p.bucket;
    p.mn.assign ((size_t) buckets, 0.0f);
    p.mx.assign ((size_t) buckets, 0.0f);
    for (int k = 0; k < buckets; ++k)
    {
        float lo = 0.0f, hi = 0.0f;
        const int s0 = k * p.bucket, s1 = juce::jmin (n, s0 + p.bucket);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto r = juce::FloatVectorOperations::findMinAndMax (b.getReadPointer (ch, s0), s1 - s0);
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }
        p.mn[(size_t) k] = lo;
        p.mx[(size_t) k] = hi;
    }
    return p;
}

double WaveformView::clipBeats() const
{
    return proc.getModel().clipLengthBeats();
}

void WaveformView::refresh()
{
    auto& m = proc.getModel();
    const auto result = proc.getResult();
    const juce::AudioBuffer<float>* buf = nullptr;
    double sr = 48000.0;
    const void* sourceKey = nullptr;

    if (tab == 1)
    {
        if (m.drums != nullptr) { buf = &m.drums->audio; sr = m.drums->sampleRate; sourceKey = m.drums.get(); }
    }
    else if (result != nullptr && result->source != nullptr && result->source == m.music)
    {
        buf = tab == 2 ? &result->fx : &result->mix;
        sr = result->sampleRate;
        sourceKey = result.get();
    }
    else if (m.music != nullptr)
    {
        buf = &m.music->audio;
        sr = m.music->sampleRate;
        sourceKey = m.music.get();
    }

    if (sourceKey != peaksSource || tab != peaksTab)
    {
        peaks = buf != nullptr ? makePeaks (*buf, sr) : Peaks();
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

    // waveform
    if (tab == 1)
        drawWave (g, peaks, juce::Colour (0xffa6ec5a), juce::Colour (0xffc8ff7a), waveCentre, waveHalf);
    else if (tab == 2)
        drawWave (g, peaks, juce::Colour (0xff38d8f0), juce::Colour (0xff6ef6ff), waveCentre, waveHalf);
    else
        drawWave (g, peaks, col::waveOut, col::waveIn, waveCentre, waveHalf);

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
    dragMode = Hit::none;
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
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
