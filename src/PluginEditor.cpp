#include "PluginEditor.h"

using namespace skin;
using km::MoveType;

namespace
{
    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;wave;bwf");
    }
}

//==============================================================================
void KoolaidEditor::Root::paint (juce::Graphics& g)
{
    const auto& art = Skin::get().image();
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (phys > 0.0f && std::abs (phys - 1.0f) > 0.01f)
    {
        // pre-scale the artwork once per scale for a crisp, alias-free background
        if (std::abs (phys - scaledFor) > 0.001f || ! scaled.isValid() || scaledVersion != Skin::get().version())
        {
            scaled = art.rescaled (juce::roundToInt (designW * phys), juce::roundToInt (designH * phys), juce::Graphics::highResamplingQuality);
            scaledFor = phys;
            scaledVersion = Skin::get().version();
        }
        g.drawImageTransformed (scaled, juce::AffineTransform::scale ((float) designW / (float) scaled.getWidth(),
                                                                       (float) designH / (float) scaled.getHeight()));
    }
    else
    {
        g.drawImageAt (art, 0, 0);
    }
}

void KoolaidEditor::CodeTab::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::ColourGradient bg (juce::Colour (active ? 0xffff7ad0 : 0xff4a3d73), 0, r.getY(),
                             juce::Colour (active ? 0xffd6248f : 0xff2a2246), 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (active ? juce::Colours::white.withAlpha (0.8f) : juce::Colour (0xffa595d8));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.2f);
    if (isMouseOver() && ! active)
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    if (icon)   // little lanes icon
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX() + 12.0f + (float) (i % 2) * 5.0f, r.getY() + 7.0f + (float) i * 5.0f, 13.0f - (float) (i % 2) * 3.0f, 3.0f, 1.5f);
        }
    drawShadowText (g, text, r.withTrimmedLeft (icon ? 22.0f : 0.0f), Skin::get().bold (18.0f), juce::Justification::centred,
                    juce::Colours::white, juce::Colour (0x8c000000));
}

void KoolaidEditor::KeyBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool light = Skin::get().getTheme() == Theme::light;
    juce::ColourGradient bg (on ? juce::Colour (0xff2c6b7a) : juce::Colour (light ? 0xffd9d2ea : 0xff2a2442), 0, r.getY(),
                             on ? juce::Colour (0xff163a48) : juce::Colour (light ? 0xffc4bbdd : 0xff1a1530), 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? col::cyan : col::lavender.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.2f);
    if (isMouseOver())
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }
    auto tr = r.reduced (10.0f, 0.0f);
    drawShadowText (g, "KEY", tr, Skin::get().heavyItalic (15.0f), juce::Justification::centredLeft,
                    on ? col::cyan : (light ? col::text : col::lavender), on || ! light ? juce::Colour (0x8c000000) : col::shadow);
    drawShadowText (g, text, tr.withTrimmedLeft (30.0f), Skin::get().wideBold (14.5f), juce::Justification::centredLeft,
                    on || ! light ? juce::Colour (0xfff7f3ff) : col::text, on || ! light ? juce::Colour (0x8c000000) : col::shadow);
}

void KoolaidEditor::ArrowButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver() && available;
    g.setColour (over ? col::hotPink.withAlpha (0.35f) : col::lavender.withAlpha (0.16f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::lavender.withAlpha (available ? 0.8f : 0.3f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    // curved arrow
    juce::Path arc;
    const auto c = r.getCentre().translated (0.0f, 2.0f);
    arc.addCentredArc (c.x, c.y, 7.0f, 6.0f, 0.0f, redo ? -2.4f : 2.4f, redo ? 0.9f : -0.9f, true);
    const auto tip = arc.getPointAlongPath (0.0f);
    juce::Path head;
    head.addTriangle (tip.x - 4.0f, tip.y - 1.0f, tip.x + 4.0f, tip.y - 1.0f, tip.x, tip.y + 5.0f);
    head.applyTransform (juce::AffineTransform::rotation (redo ? -0.7f : 0.7f, tip.x, tip.y));
    const auto ink = available ? col::text : col::textDim.withAlpha (0.5f);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath (head);
}

void KoolaidEditor::Bubble::show (juce::Component& near, const juce::String& t)
{
    text = t;
    const auto f = Skin::get().bold (19.0f);
    const int w = (int) juce::GlyphArrangement::getStringWidth (f, text) + 22;
    const auto b = near.getBounds();
    setBounds (juce::jlimit (4, designW - w - 4, b.getCentreX() - w / 2), b.getY() - 30, w, 28);
    setVisible (true);
    toFront (false);
    repaint();
}

void KoolaidEditor::Bubble::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::menuBg.withAlpha (0.95f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::cyan);
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    drawShadowText (g, text, r, Skin::get().bold (19.0f));
}

//==============================================================================
KoolaidEditor::KoolaidEditor (KoolaidProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (root);
    root.setBounds (0, 0, designW, designH);

    for (auto* c : std::initializer_list<juce::Component*> {
             &step1, &step2, &step3, &step4, &presetPrev, &presetNext, &presetName, &saveBtn, &saveAsBtn, &browseBtn,
             &tempoBox, &syncBtn, &timeSigBox, &gearBtn, &tab1, &tab2, &tab3, &timelineTab, &abA, &abB, &abCopy,
             &undoBtn, &redoBtn, &barsBox, &zoomIn, &zoomOut, &zoomFit,
             &captureBtn, &waveform, &playBtn, &stopBtn, &loopBtn, &prevBtn, &nextBtn, &playbackBox, &lengthBox, &dropAtBox,
             &syncSessionBtn, &stack, &addMoveBtn, &duplicateBtn, &deleteBtn, &analyzeBtn, &rollBtn, &instantBtn,
             &previewBtn, &exportBtn, &exportRangeBox, &formatBox })
        root.addAndMakeVisible (c);

    cards = std::make_unique<EffectCards> (proc, stack, root);
    cards->onEdited = [this] { edited(); };
    cards->onStackChanged = [this] { stack.refresh(); };
    cards->onBubble = [this] (SkinKnob& k, const juce::String& t, bool show)
    {
        if (show) bubble.show (k, t);
        else bubble.setVisible (false);
    };

    for (int i = 0; i < 3; ++i)
    {
        detectChecks[i] = std::make_unique<Btn> (juce::Rectangle<int> (249, 946 + 34 * i, 26, 22), 4.0f);
        detectChecks[i]->setInterceptsMouseClicks (false, false);
        root.addAndMakeVisible (*detectChecks[i]);
        detectText[i] = std::make_unique<Box> (juce::Rectangle<int> (284, 946 + 34 * i, 158, 22),
                                               juce::Rectangle<int> (285, 947 + 34 * i, 152, 20), 19.0f, juce::Justification::centredLeft);
        detectText[i]->setFont (Skin::get().wide (18.0f));
        root.addAndMakeVisible (*detectText[i]);
    }
    root.addChildComponent (bubble);
    root.addAndMakeVisible (keyBox);
    keyBox.onClick = [this] { showKeyMenu(); };
    root.addAndMakeVisible (mascotBtn);
    mascotBtn.setShowHover (false);
    mascotBtn.setTooltip ("Click Koolaid Man to switch between the dark and light look");
    mascotBtn.onClick = [this] { toggleTheme(); };
    Skin::get().addChangeListener (this);
    root.addAndMakeVisible (manualHotspot);
    manualHotspot.setTooltip ("Open the Koolaid Man manual");
    manualHotspot.onClick = [this] { openManual(); };
    root.addChildComponent (manual);
    root.addChildComponent (gridEditor);
    stack.onEditCurve = [this] (int uid) { editCurve (uid); };

    //------------------------------------------------------------------ header steps
    step1.setTooltip ("1. DRAG IN AUDIO - load the bars before your drop, or capture them from your track");
    step1.onClick = [this] { showLoadMenu (&step1); };
    step2.setTooltip ("2. BUILD THE DROP - click for an instant idea (same as ROLL)");
    step2.onClick = [this] { doRoll(); };
    step3.setTooltip ("3. PREVIEW IN SYNC - hear the drop in time with your session when the DAW plays");
    step3.onClick = [this] { toggleSync(); };
    step4.setTooltip ("4. DRAG BACK TO DAW - drag this into your project (or click to save the file)");
    step4.onClick = [this] { doExport (false, nullptr); };
    step4.onDragOut = [this] (const juce::MouseEvent& e) { doExport (true, &e); };

    //------------------------------------------------------------------ presets
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetPrev.setTooltip ("Previous preset");
    presetNext.setTooltip ("Next preset");
    presetName.setFont (Skin::get().wide (21.0f));
    presetName.onClick = [this] { showBrowseMenu (&presetName); };
    presetName.setTooltip ("Click to browse presets");
    presetName.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    saveBtn.onClick = [this] { savePreset(); };
    saveBtn.setTooltip ("Save the moves stack over the current user preset");
    saveAsBtn.onClick = [this] { saveAs(); };
    saveAsBtn.setTooltip ("Save the moves stack as a new preset");
    browseBtn.onClick = [this] { showBrowseMenu (&browseBtn); };
    browseBtn.setTooltip ("Browse factory and user presets");

    //------------------------------------------------------------------ tempo / sync / bar
    tempoBox.setFont (Skin::get().wideBold (19.0f));
    tempoBox.onDrag = [this] (float dy)
    {
        auto& m = proc.getModel();
        if (m.tempoSync) return;
        m.bpm = juce::jlimit (40.0, 250.0, m.bpm + dy * (juce::ModifierKeys::currentModifiers.isShiftDown() ? 0.01 : 0.25));
        zoneEdited();
    };
    tempoBox.onDoubleClick = [this]
    {
        if (proc.getModel().tempoSync) return;
        askNumber ("TEMPO", "Tempo of the audio (BPM):", juce::String (proc.getModel().bpm, 2), [this] (const juce::String& s)
        {
            const double v = s.getDoubleValue();
            if (v >= 40.0 && v <= 250.0) { proc.getModel().bpm = v; zoneEdited(); }
        });
    };
    tempoBox.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    syncBtn.onClick = [this]
    {
        auto& m = proc.getModel();
        m.tempoSync = ! m.tempoSync;
        zoneEdited();
    };
    syncBtn.setTooltip ("SYNC: follow your DAW's tempo and time signature. Turn off to set the tempo by hand (drag / double-click the number).");
    timeSigBox.onClick = [this]
    {
        juce::PopupMenu menu;
        const std::pair<int, int> sigs[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
        auto& m = proc.getModel();
        menu.addSectionHeader (m.tempoSync ? "FOLLOWING DAW (TURN SYNC OFF TO CHANGE)" : "TIME SIGNATURE");
        for (int i = 0; i < 7; ++i)
            menu.addItem (i + 1, juce::String (sigs[i].first) + "/" + juce::String (sigs[i].second), ! m.tempoSync,
                          m.tsNum == sigs[i].first && m.tsDen == sigs[i].second);
        menu.showMenuAsync (menuOptionsFor (&timeSigBox), [this, sigs] (int r)
        {
            if (r <= 0) return;
            proc.getModel().tsNum = sigs[r - 1].first;
            proc.getModel().tsDen = sigs[r - 1].second;
            zoneEdited();
        });
    };
    timeSigBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    gearBtn.onClick = [this] { showGearMenu(); };
    gearBtn.setTooltip ("Settings & Manual");

    //------------------------------------------------------------------ tabs / zoom / capture
    tab1.onClick = [this] { if (proc.getSettings().activeTab == 0 && ! proc.hasMusic()) chooseFile (0); setTab (0); };
    tab2.onClick = [this] { if (proc.getSettings().activeTab == 1 && proc.getModel().drums == nullptr) chooseFile (1); setTab (1); };
    tab3.onClick = [this] { setTab (2); };
    timelineTab.onClick = [this] { setTab (3); };
    for (auto* t : { &abA, &abB, &abCopy }) t->icon = false;
    abA.setTooltip ("A/B: version A of your drop");
    abB.setTooltip ("A/B: version B of your drop - flip between them while listening");
    abCopy.setTooltip ("Copy this version into the other slot");
    abA.onClick = [this] { proc.switchAB (0); afterRestore(); };
    abB.onClick = [this] { proc.switchAB (1); afterRestore(); };
    abCopy.onClick = [this]
    {
        proc.copyABToOther();
        waveform.showMessage ("Copied to " + juce::String (proc.getABSlot() == 0 ? "B" : "A"), 1500);
    };
    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");
    undoBtn.onClick = [this] { proc.undo(); afterRestore(); };
    redoBtn.onClick = [this] { proc.redo(); afterRestore(); };
    timelineTab.setTooltip ("TIMELINE: one lane per move - drag to choose exactly when each move happens (double-click a lane for auto)");
    tab1.setTooltip ("AUDIO 1: your two-track (the bars before the drop)");
    tab2.setTooltip ("AUDIO 2: optional drum / 808 stem used to find the snare, 808 and hats");
    tab3.setTooltip ("FX LAYER: the sounds the moves add (fills, 808s, hats, reverb)");
    zoomIn.onClick = [this] { waveform.zoomBy (0.66); };
    zoomOut.onClick = [this] { waveform.zoomBy (1.5); };
    zoomFit.onClick = [this] { waveform.zoomFit(); };
    zoomIn.setTooltip ("Zoom in (or ctrl + mouse wheel)");
    zoomOut.setTooltip ("Zoom out");
    zoomFit.setTooltip ("Zoom to fit");
    barsBox.onClick = [this] { if (proc.hasMusic()) askClipBar(); };
    barsBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    barsBox.setTooltip ("Where this audio sits in your project - click to change the start bar");
    captureBtn.onClick = [this] { proc.armCapture (! proc.isCaptureArmed()); refreshTransport(); waveform.repaint(); };
    captureBtn.setTooltip ("CAPTURE: arm, then press play in your DAW. Records this track's audio from the next bar line until you stop.");

    waveform.onZoneEdited = [this] { zoneEdited(); };
    waveform.onLanesEdited = [this]
    {
        proc.modelChanged (true);
        presetName.setText (presetDisplayName());
        stack.refresh();
        if (cards) cards->refresh();
    };
    waveform.onLaneSelected = [this] (int uid) { stack.select (uid); };
    waveform.onTensionMenu = [this] { showTensionMenu(); };
    waveform.onEmptyClicked = [this] { chooseFile (proc.getSettings().activeTab == 1 ? 1 : 0); };

    //------------------------------------------------------------------ transport
    playBtn.onClick = [this] { proc.setPlaying (! proc.isPlaying()); refreshTransport(); };
    playBtn.setTooltip ("Play / pause the preview");
    stopBtn.onClick = [this] { proc.setPlaying (false); proc.jumpToStart(); refreshTransport(); };
    stopBtn.setTooltip ("Stop");
    loopBtn.onClick = [this] { proc.getSettings().loop = ! proc.getSettings().loop; zoneEdited(); };
    loopBtn.setTooltip ("Loop the preview");
    prevBtn.onClick = [this] { proc.jumpToStart(); };
    prevBtn.setTooltip ("Back to the start");
    nextBtn.onClick = [this] { proc.jumpToDrop(); };
    nextBtn.setTooltip ("Jump to one bar before the drop");

    playbackBox.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("PLAYBACK");
        const auto names = km::playbackModeNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, (int) proc.getSettings().playbackMode == i);
        menu.addSeparator();
        menu.addItem (100, "Loop", true, proc.getSettings().loop);
        menu.showMenuAsync (menuOptionsFor (&playbackBox), [this] (int r)
        {
            if (r == 100) proc.getSettings().loop = ! proc.getSettings().loop;
            else if (r > 0) proc.getSettings().playbackMode = (km::PlaybackMode) (r - 1);
            else return;
            zoneEdited();
        });
    };
    playbackBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    lengthBox.onClick = [this]
    {
        auto& m = proc.getModel();
        const double bpb = m.beatsPerBar();
        juce::PopupMenu menu;
        menu.addSectionHeader ("BUILD ZONE LENGTH");
        const double lens[] = { bpb * 0.5, bpb, bpb * 2, bpb * 4, bpb * 8, bpb * 16 };
        for (int i = 0; i < 6; ++i)
            menu.addItem (i + 1, km::formatLength (lens[i], bpb), lens[i] <= m.zoneEnd + 1.0e-6,
                          std::abs ((m.zoneEnd - m.zoneStart) - lens[i]) < 1.0e-6);
        menu.showMenuAsync (menuOptionsFor (&lengthBox), [this, lens] (int r)
        {
            if (r <= 0) return;
            auto& mm = proc.getModel();
            mm.zoneStart = juce::jmax (0.0, mm.zoneEnd - lens[r - 1]);
            zoneEdited();
        });
    };
    lengthBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    lengthBox.setTooltip ("How long the build is (or drag the zone's left handle)");

    dropAtBox.onClick = [this]
    {
        auto& m = proc.getModel();
        const double bpb = m.beatsPerBar();
        const int bars = (int) std::floor (m.clipLengthBeats() / bpb + 1.0e-6);
        juce::PopupMenu menu;
        menu.addSectionHeader ("THE DROP HITS AT");
        for (int b = 1; b <= bars; ++b)
            menu.addItem (b, km::formatBarPosition (b * bpb, m), true, std::abs (m.zoneEnd - b * bpb) < 1.0e-6);
        menu.showMenuAsync (menuOptionsFor (&dropAtBox), [this, bpb] (int r)
        {
            if (r <= 0) return;
            auto& mm = proc.getModel();
            const double len = mm.zoneEnd - mm.zoneStart;
            mm.zoneEnd = r * bpb;
            mm.zoneStart = juce::jmax (0.0, mm.zoneEnd - len);
            zoneEdited();
        });
    };
    dropAtBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    dropAtBox.setTooltip ("Where the drop lands (or drag the zone's right handle)");

    syncSessionBtn.setLitRegion ({ 1236, 531, 34, 30 });
    syncSessionBtn.onClick = [this] { toggleSync(); };
    syncSessionBtn.setTooltip ("SYNC TO SESSION: when your DAW plays, the drop plays in time at the right bars (replacing this track's audio there)");

    //------------------------------------------------------------------ stack
    stack.onStackEdited = [this] { edited(); };
    stack.onSelectionChanged = [this]
    {
        if (cards) cards->followSelection();
        waveform.setSelectedUid (stack.getSelectedUid());
    };
    addMoveBtn.setTooltip ("Add a move to the stack");
    addMoveBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("ADD MOVE");
        const char* desc[] = { "Reverse", "Chops", "Stutter", "Pitch", "Tape Stop", "Filter", "Drum Moves (snare fill / gate / roll)",
                               "808 Slide", "Hat Roll", "Atmos Reverb",
                               "Noise Riser", "Reverse Crash", "Sidechain Pump", "Drop Gap", "Impact (on the drop)", "Sub Boom (on the drop)" };
        static_assert (sizeof (desc) / sizeof (desc[0]) == km::numMoveTypes, "one name per move type");
        for (int t = 0; t < km::numMoveTypes; ++t)
        {
            if (t == (int) MoveType::Riser) menu.addSectionHeader ("BUILD & DROP");
            menu.addItem (t + 1, desc[t]);
        }
        menu.showMenuAsync (menuOptionsFor (&addMoveBtn), [this] (int r)
        {
            if (r <= 0) return;
            auto& m = proc.getModel();
            auto mv = m.templates[(size_t) (r - 1)];
            mv.uid = 0; mv.enabled = true;
            mv.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (1 << 30);
            m.moves.push_back (mv);
            edited();
            stack.select (m.moves.back().uid);
        });
    };
    duplicateBtn.setTooltip ("Duplicate the selected move");
    duplicateBtn.onClick = [this]
    {
        auto& m = proc.getModel();
        const int i = stack.selectedIndex();
        if (i < 0) return;
        auto copy = m.moves[(size_t) i];
        copy.uid = 0;
        m.moves.insert (m.moves.begin() + i + 1, copy);
        edited();
        stack.select (m.moves[(size_t) i + 1].uid);
    };
    deleteBtn.setTooltip ("Delete the selected move");
    deleteBtn.onClick = [this]
    {
        auto& m = proc.getModel();
        const int i = stack.selectedIndex();
        if (i < 0) return;
        m.moves.erase (m.moves.begin() + i);
        if (! m.moves.empty())
            stack.select (m.moves[(size_t) juce::jmin (i, (int) m.moves.size() - 1)].uid);
        edited();
    };

    //------------------------------------------------------------------ detection
    analyzeBtn.setTooltip ("Find the snare, 808 and hi-hats in your audio (uses AUDIO 2 if you loaded a drum stem)");
    analyzeBtn.onClick = [this]
    {
        proc.getModel().analysisRequest++;
        proc.modelChanged (false);
        waveform.showMessage ("Analyzing drums...", 1500);
        flashing = &analyzeBtn;
        analyzeBtn.setFlash (true);
        flashCountdown = 6;
    };

    //------------------------------------------------------------------ roll / preview / export
    rollBtn.setTooltip ("ROLL: an instant idea - a fresh stack of moves");
    rollBtn.onClick = [this] { doRoll(); };
    instantBtn.onClick = [this] { doRoll(); };
    previewBtn.setTooltip ("PREVIEW (IN SYNC): when lit, the drop plays in time with your session whenever the DAW is playing");
    previewBtn.onClick = [this] { toggleSync(); };
    exportBtn.setTooltip ("Drag this into your DAW's project window - it lands on the grid at the bar shown. Click to just save the file.");
    exportBtn.onClick = [this] { doExport (false, nullptr); };
    exportBtn.onDragOut = [this] (const juce::MouseEvent& e) { doExport (true, &e); };

    exportRangeBox.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("EXPORT");
        for (int i = 0; i < 5; ++i)
            menu.addItem (i + 1, proc.exportRangeLabel ((km::ExportRange) i), true, (int) proc.getSettings().exportRange == i);
        menu.showMenuAsync (menuOptionsFor (&exportRangeBox), [this] (int r)
        {
            if (r > 0) { proc.getSettings().exportRange = (km::ExportRange) (r - 1); refreshAll(); }
        });
    };
    exportRangeBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    formatBox.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("FORMAT");
        const auto names = km::exportFormatNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, (int) proc.getSettings().exportFormat == i);
        menu.showMenuAsync (menuOptionsFor (&formatBox), [this] (int r)
        {
            if (r > 0) { proc.getSettings().exportFormat = (km::ExportFormat) (r - 1); refreshAll(); }
        });
    };
    formatBox.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    proc.onModelReplaced = [this] { lastResultVersion = -1; refreshAll(); };

    const float scale = proc.getSettings().uiScale;
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) designW / (double) designH);
        c->setSizeLimits ((int) (designW * 0.45f), (int) (designH * 0.45f), (int) (designW * 1.5f), (int) (designH * 1.5f));
    }
    setSize (juce::roundToInt (designW * scale), juce::roundToInt (designH * scale));

    setTab (proc.getSettings().activeTab);
    refreshAll();
    startTimerHz (30);
}

KoolaidEditor::~KoolaidEditor()
{
    Skin::get().removeChangeListener (this);
    stopTimer();
    proc.onModelReplaced = nullptr;
    cards.reset();
    setLookAndFeel (nullptr);
}

void KoolaidEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    root.setTransform (juce::AffineTransform::scale (scale));
    proc.getSettings().uiScale = scale;
}

juce::Image KoolaidEditor::snapshot (float scale)
{
    setSize (juce::roundToInt (designW * scale), juce::roundToInt (designH * scale));
    return createComponentSnapshot (getLocalBounds(), true, 1.0f);
}

//==============================================================================
void KoolaidEditor::timerCallback()
{
    waveform.tick();

    if (proc.getResultVersion() != lastResultVersion)
    {
        lastResultVersion = proc.getResultVersion();
        waveform.refresh();
        refreshDetection();
        if (cards) cards->refresh();
    }

    if (proc.getModel().bpm != lastBpm)
    {
        lastBpm = proc.getModel().bpm;
        refreshAll();
    }

    if (proc.isPlaying() != lastPlaying || proc.isSyncPlayheadActive() != lastSync
        || proc.isCaptureArmed() != lastCapture || proc.isCapturing() != lastCapturing || proc.isCapturing())
        refreshTransport();

    undoBtn.setAvailable (proc.canUndo());
    redoBtn.setAvailable (proc.canRedo());

    if (flashCountdown > 0 && --flashCountdown == 0 && flashing != nullptr)
        flashing->setFlash (false);
}

void KoolaidEditor::refreshTransport()
{
    lastPlaying = proc.isPlaying();
    lastSync = proc.isSyncPlayheadActive();
    lastCapture = proc.isCaptureArmed();
    lastCapturing = proc.isCapturing();
    const auto& s = proc.getSettings();
    playBtn.setGlow (lastPlaying ? col::lime : juce::Colours::transparentBlack);
    loopBtn.setGlow (s.loop ? col::cyan : juce::Colours::transparentBlack);
    const bool blinkOn = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    captureBtn.setGlow (lastCapturing ? (blinkOn ? juce::Colours::red : col::hotPink)
                                      : (lastCapture ? col::hotPink : juce::Colours::transparentBlack));
    const bool sync = proc.getSyncPreview();
    previewBtn.setLit (sync);
    syncSessionBtn.setLit (sync);
    previewBtn.setGlow (lastSync ? col::yellow : juce::Colours::transparentBlack);
}

void KoolaidEditor::showKeyMenu()
{
    auto& m = proc.getModel();
    const auto a = proc.getAnalysis();
    juce::PopupMenu menu, majors, minors;
    menu.addSectionHeader ("SONG KEY (TUNES RISERS, SUB BOOM, IMPACT)");
    juce::String detected = "not found yet";
    if (a != nullptr && a->key.found) detected = km::keyName (a->key.tonic, a->key.minor);
    menu.addItem (1, "Auto (detected: " + detected + ")", true, m.keyMode == -1);
    menu.addItem (2, "Off (don't tune)", true, m.keyMode == -2);
    for (int t = 0; t < 12; ++t)
    {
        majors.addItem (100 + t, km::keyName (t, false), true, m.keyMode == t);
        minors.addItem (112 + t, km::keyName (t, true), true, m.keyMode == 12 + t);
    }
    menu.addSeparator();
    menu.addSubMenu ("Major keys", majors);
    menu.addSubMenu ("Minor keys", minors);
    menu.showMenuAsync (menuOptionsFor (&keyBox), [this] (int r)
    {
        if (r <= 0) return;
        auto& mm = proc.getModel();
        mm.keyMode = r == 1 ? -1 : (r == 2 ? -2 : r - 100);
        proc.modelChanged (true);
        refreshDetection();
    });
}

void KoolaidEditor::refreshDetection()
{
    const auto a = proc.getAnalysis();
    {
        const auto& m = proc.getModel();
        const auto key = m.effectiveKey (a != nullptr ? a->key : km::KeyInfo {});
        juce::String text;
        if (m.keyMode == -2) text = "OFF";
        else if (key.found) text = km::keyName (key.tonic, key.minor).toUpperCase().replace ("MAJOR", "MAJ").replace ("MINOR", "MIN")
                                   + (m.keyMode >= 0 ? " *" : "");
        else text = proc.hasMusic() ? (a == nullptr ? "..." : "?") : "--";
        keyBox.setText (text, key.found);
        keyBox.setTooltip (m.keyMode == -2 ? "Key tuning is off - click to turn it on"
                           : key.found ? "Your song's key" + juce::String (m.keyMode >= 0 ? " (set by hand)" : " (detected)")
                                         + ": " + km::keyName (key.tonic, key.minor) + ". Risers, Sub Boom, Impact and the backup 808 are tuned to it. Click to change."
                                       : "The song's key couldn't be detected - click to set it by hand");
    }
    const char* names[] = { "Snare / Drum", "808", "Hi-Hats" };
    for (int i = 0; i < 3; ++i)
    {
        const km::DrumHits* h = a == nullptr ? nullptr : (i == 0 ? &a->snare : (i == 1 ? &a->bass : &a->hats));
        const bool found = h != nullptr && h->found;
        detectChecks[i]->setLit (found);
        juce::String status = ! proc.hasMusic() && proc.getModel().drums == nullptr ? "(No audio)"
                            : (a == nullptr ? "(Analyzing)" : (found ? "(Found)" : "(Synth)"));
        detectText[i]->setText (juce::String (names[i]) + "  " + status);
        detectText[i]->setTextColour (found ? col::text : col::textDim);
        juce::String tip = found ? juce::String (h->onsets.size()) + " hits found" : "Not found - moves use a built-in sound instead";
        if (i == 1 && found && h->pitchHz > 0) tip << ", pitch ~" << juce::String (h->pitchHz, 1) << " Hz";
        detectText[i]->setTooltip (tip + (a != nullptr && a->fromDrumStem ? " (from AUDIO 2)" : ""));
    }
}

void KoolaidEditor::refreshAll()
{
    auto& m = proc.getModel();
    auto& s = proc.getSettings();

    presetName.setText (presetDisplayName());
    tempoBox.setText (juce::String (m.bpm, 2));
    tempoBox.setTooltip (m.tempoSync ? "Following your DAW's tempo (turn SYNC off to set it by hand)" : "Drag up/down or double-click to type the tempo");
    tempoBox.setMouseCursor (m.tempoSync ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
    syncBtn.setLit (m.tempoSync);
    timeSigBox.setText (juce::String (m.tsNum) + "/" + juce::String (m.tsDen));

    tab1.setGlow (s.activeTab == 0 ? col::cyan : juce::Colours::transparentBlack);
    tab2.setGlow (s.activeTab == 1 ? col::lavender : juce::Colours::transparentBlack);
    tab3.setGlow (s.activeTab == 2 ? col::hotPink : juce::Colours::transparentBlack);
    timelineTab.setActive (s.activeTab == 3);
    abA.setActive (proc.getABSlot() == 0);
    abB.setActive (proc.getABSlot() == 1);
    abCopy.text = proc.getABSlot() == 0 ? "COPY TO B" : "COPY TO A";
    abCopy.repaint();
    undoBtn.setAvailable (proc.canUndo());
    redoBtn.setAvailable (proc.canRedo());
    waveform.setSelectedUid (stack.getSelectedUid());


    if (proc.hasMusic())
    {
        const double total = m.clipLengthBeats();
        barsBox.setText ("BARS: " + km::formatBarPosition (0.0, m) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 "))
                         + km::formatBarPosition (total, m) + "  (" + km::formatLength (total, m.beatsPerBar()) + ")");
        lengthBox.setText (km::formatLength (m.zoneEnd - m.zoneStart, m.beatsPerBar()));
        dropAtBox.setText (km::formatBarPosition (m.zoneEnd, m));
    }
    else
    {
        barsBox.setText ("BARS: --");
        lengthBox.setText ("--");
        dropAtBox.setText ("--");
    }
    playbackBox.setText (km::playbackModeNames()[(int) s.playbackMode] + (s.loop ? " (LOOP)" : ""));
    exportRangeBox.setText (proc.exportRangeLabel (s.exportRange));
    formatBox.setText (km::exportFormatNames()[(int) s.exportFormat]);
    exportBtn.setTooltip ("Drag into your project - place it at bar " + proc.exportLandingBar()
                          + " and it lands right on the grid. Click to just save the file.");

    stack.refresh();
    if (cards) cards->refresh();
    waveform.refresh();
    refreshTransport();
    refreshDetection();
}

void KoolaidEditor::edited()
{
    proc.modelChanged (true);
    presetName.setText (presetDisplayName());
    stack.refresh();
    if (cards) cards->refresh();
}

void KoolaidEditor::zoneEdited()
{
    proc.modelChanged (false);
    refreshAll();
}

//==============================================================================
juce::String KoolaidEditor::presetDisplayName() const
{
    const auto& s = proc.getSettings();
    return s.presetName + (s.presetDirty ? " *" : "");
}

std::vector<km::Preset> KoolaidEditor::allPresets() const
{
    auto list = km::factoryPresets();
    for (auto& p : km::loadUserPresets())
        list.push_back (p);
    return list;
}

void KoolaidEditor::stepPreset (int delta)
{
    const auto list = allPresets();
    if (list.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == proc.getSettings().presetName) idx = (int) i;
    idx = (idx + delta + (int) list.size()) % (int) list.size();
    proc.applyPreset (list[(size_t) idx]);
    stack.select (proc.getModel().moves.empty() ? 0 : proc.getModel().moves.front().uid);
    refreshAll();
}

void KoolaidEditor::showBrowseMenu (juce::Component* target)
{
    const auto list = allPresets();
    juce::PopupMenu menu;
    menu.addSectionHeader ("FACTORY");
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i > 0 && list[i].factory != list[i - 1].factory)
            menu.addSectionHeader ("YOUR PRESETS");
        menu.addItem ((int) i + 1, list[i].name, true, list[i].name == proc.getSettings().presetName);
    }
    menu.addSeparator();
    menu.addItem (10000, "Open Presets Folder");
    menu.showMenuAsync (menuOptionsFor (target), [this, list] (int r)
    {
        if (r == 10000) { km::userPresetFolder().startAsProcess(); return; }
        if (r <= 0 || r > (int) list.size()) return;
        proc.applyPreset (list[(size_t) r - 1]);
        stack.select (proc.getModel().moves.empty() ? 0 : proc.getModel().moves.front().uid);
        refreshAll();
    });
}

void KoolaidEditor::savePreset()
{
    for (auto& p : km::loadUserPresets())
        if (p.name == proc.getSettings().presetName)
        {
            km::saveUserPreset (p.name, proc.getModel().moves);
            proc.getSettings().presetDirty = false;
            presetName.setText (presetDisplayName());
            waveform.showMessage ("Saved preset \"" + p.name + "\"");
            return;
        }
    saveAs();
}

void KoolaidEditor::askNumber (const juce::String& title, const juce::String& message, const juce::String& initial,
                               std::function<void (const juce::String&)> onOk)
{
    auto* aw = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, this);
    aw->setLookAndFeel (&laf);
    aw->addTextEditor ("value", initial);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, onOk] (int r)
    {
        if (r == 1 && onOk) onOk (aw->getTextEditorContents ("value").trim());
    }), true);
}

void KoolaidEditor::toggleTheme()
{
    Skin::get().setTheme (Skin::get().getTheme() == Theme::light ? Theme::dark : Theme::light);
}

void KoolaidEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    laf.applyTheme();
    manual.themeChanged();
    refreshAll();
    root.repaint();
}

void KoolaidEditor::editCurve (int uid)
{
    auto* mv = [this, uid]() -> km::Move*
    {
        for (auto& m : proc.getModel().moves) if (m.uid == uid) return &m;
        return nullptr;
    }();
    if (mv == nullptr) return;
    auto start = mv->curvePoints;
    if (mv->curve != km::curveCustom)   // start from the shape it has now
        for (int i = 0; i < 16; ++i) start[(size_t) i] = (float) km::shapeCurve (*mv, i / 15.0, 1.4);
    gridEditor.openCurve ("CURVE: " + mv->stackLabel().toUpperCase(), start, [this, uid] (const GridEditor::Values& v)
    {
        for (auto& m : proc.getModel().moves)
            if (m.uid == uid)
            {
                m.curvePoints = v;
                m.curve = km::curveCustom;
            }
        proc.modelChanged (true);
        stack.refresh();
        waveform.repaint();
        presetName.setText (presetDisplayName());
    });
}

void KoolaidEditor::showTensionMenu()
{
    auto& m = proc.getModel();
    juce::PopupMenu menu, shapes;
    menu.addSectionHeader ("TENSION MACRO");
    int linked = 0;
    for (auto& mv : m.moves) linked += mv.tensionLink ? 1 : 0;
    menu.addItem (1, "Link All Moves", linked < (int) m.moves.size());
    menu.addItem (2, "Unlink All Moves", linked > 0);
    menu.addSeparator();
    const auto& names = km::curveNames();
    for (int i = 1; i < names.size() - 1; ++i)   // no "Auto" for the macro itself
        shapes.addItem (200 + i, names[i], true, m.tension.curve == i);
    shapes.addSeparator();
    shapes.addItem (200 + km::curveCustom, "Custom - Draw It...", true, m.tension.curve == km::curveCustom);
    menu.addSubMenu ("Tension Curve (how linked moves come in)", shapes);
    menu.addSeparator();
    for (int v : { 100, 75, 50, 25, 0 })
        menu.addItem (300 + v, "Tension " + juce::String (v) + "%", true, std::abs (proc.getTension() * 100.0f - (float) v) < 0.5f);
    menu.addSeparator();
    menu.addItem (3, "Tip: drag the TENSION lane up/down, or automate \"Tension\" in your DAW", false);
    menu.showMenuAsync (menuOptionsFor (&waveform), [this] (int r)
    {
        auto& mm = proc.getModel();
        if (r == 1 || r == 2)
        {
            for (auto& mv : mm.moves) mv.tensionLink = r == 1;
            proc.modelChanged (true);
        }
        else if (r == 200 + km::curveCustom)
        {
            auto start = mm.tension.curvePoints;
            if (mm.tension.curve != km::curveCustom)
                for (int i = 0; i < 16; ++i) start[(size_t) i] = (float) km::shapeCurve (mm.tension, i / 15.0, 1.0);
            gridEditor.openCurve ("TENSION CURVE", start, [this] (const GridEditor::Values& v)
            {
                proc.getModel().tension.curvePoints = v;
                proc.getModel().tension.curve = km::curveCustom;
                proc.modelChanged (true);
                waveform.repaint();
            });
            return;
        }
        else if (r >= 300) { proc.setTension ((float) (r - 300) / 100.0f, true, true); }
        else if (r > 200) { mm.tension.curve = r - 200; proc.modelChanged (true); }
        else return;
        stack.refresh();
        waveform.repaint();
        presetName.setText (presetDisplayName());
    });
}

void KoolaidEditor::openManual()
{
    bubble.setVisible (false);
    manual.open();
}

void KoolaidEditor::askClipBar()
{
    askNumber ("CLIP POSITION", "Which bar of your project does this audio start on?\n(Used to play the drop in sync and to tell you where to drop the export.)",
               juce::String (proc.getModel().clipStartBar), [this] (const juce::String& v)
    {
        const int bar = v.getIntValue();
        if (bar >= 1 && bar <= 9999) { proc.getModel().clipStartBar = bar; zoneEdited(); }
    });
}

void KoolaidEditor::saveAs()
{
    juce::String initial = proc.getSettings().presetName;
    if (initial.startsWith ("Roll #")) initial = "My Drop";
    askNumber ("SAVE PRESET", "Name your preset:", initial, [this] (const juce::String& name)
    {
        if (name.isEmpty()) return;
        if (km::saveUserPreset (name, proc.getModel().moves))
        {
            proc.getSettings().presetName = name;
            proc.getSettings().presetDirty = false;
            presetName.setText (presetDisplayName());
            waveform.showMessage ("Saved preset \"" + name + "\"");
        }
        else waveform.showMessage ("Couldn't save the preset");
    });
}

void KoolaidEditor::showGearMenu()
{
    auto& s = proc.getSettings();
    juce::PopupMenu menu, snapMenu, previewMenu, sizeMenu;
    menu.addSectionHeader ("KOOLAID MAN");
    menu.addItem (2, "Manual (How To Use)");
    menu.addSeparator();
    menu.addItem (1, "Clip Starts At Bar " + juce::String (proc.getModel().clipStartBar) + "...");
    snapMenu.addItem (10, "Bar", true, s.snap == 0);
    snapMenu.addItem (11, "Beat", true, s.snap == 1);
    snapMenu.addItem (12, "1/16", true, s.snap == 2);
    menu.addSubMenu ("Zone Snap (hold Shift for free)", snapMenu);
    previewMenu.addItem (20, "Replace Track Audio In The Zone", true, s.previewReplaces);
    previewMenu.addItem (21, "Layer On Top Of Track Audio", true, ! s.previewReplaces);
    menu.addSubMenu ("In-Sync Preview", previewMenu);
    const float sizes[] = { 0.5f, 0.6f, 0.72f, 0.85f, 1.0f, 1.25f };
    for (int i = 0; i < 6; ++i)
        sizeMenu.addItem (30 + i, juce::String (juce::roundToInt (sizes[i] * 100)) + "%", true, std::abs (s.uiScale - sizes[i]) < 0.01f);
    menu.addSubMenu ("Window Size", sizeMenu);
    juce::PopupMenu themeMenu;
    themeMenu.addItem (70, "Dark", true, Skin::get().getTheme() == Theme::dark);
    themeMenu.addItem (71, "Light", true, Skin::get().getTheme() == Theme::light);
    menu.addSubMenu ("Theme (or click Koolaid Man)", themeMenu);
    menu.addSeparator();
    menu.addItem (40, "Open Export Folder");
    menu.addItem (41, "Open Presets Folder");
    menu.addSeparator();
    menu.addItem (50, "Clear AUDIO 1", proc.hasMusic());
    menu.addItem (51, "Clear AUDIO 2", proc.getModel().drums != nullptr);
    menu.addSeparator();
    menu.addItem (60, "About Koolaid Man");

    menu.showMenuAsync (menuOptionsFor (&gearBtn), [this, sizes] (int r)
    {
        auto& st = proc.getSettings();
        if (r == 1)
            askClipBar();
        else if (r == 2)
            openManual();
        else if (r == 70 || r == 71)
            Skin::get().setTheme (r == 71 ? Theme::light : Theme::dark);
        else if (r >= 10 && r <= 12) { st.snap = r - 10; }
        else if (r == 20 || r == 21) { st.previewReplaces = r == 20; zoneEdited(); }
        else if (r >= 30 && r < 36)  { setSize (juce::roundToInt (designW * sizes[r - 30]), juce::roundToInt (designH * sizes[r - 30])); }
        else if (r == 40) km::exportFolder().startAsProcess();
        else if (r == 41) km::userPresetFolder().startAsProcess();
        else if (r == 50) { proc.clearSlot (0); refreshAll(); }
        else if (r == 51) { proc.clearSlot (1); refreshAll(); }
        else if (r == 60)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "KOOLAID MAN",
                "Beat-Drop Creator v" JucePlugin_VersionString "\n\n"
                "1. Drag in the bars before your drop (or CAPTURE them).\n"
                "2. Build the drop from the moves stack - or hit ROLL.\n"
                "3. Preview in sync with your session.\n"
                "4. Drag the result back to your DAW at the bar shown.\n\n"
                "Full manual: Settings (gear) > Manual, or click KOOLAID MAN next to PRESET.\n\n"
                "Exports: " + km::exportFolder().getFullPathName(), "OH YEAH!", this);
    });
}

void KoolaidEditor::showLoadMenu (juce::Component* target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("DRAG IN AUDIO");
    menu.addItem (1, "Load Audio File (AUDIO 1)...");
    menu.addItem (2, proc.isCaptureArmed() ? "Disarm Capture" : "Capture From This Track (arm, then press play)");
    menu.addItem (3, "Load Drum / 808 Stem (AUDIO 2)...");
    menu.addSeparator();
    menu.addItem (4, "Clear AUDIO 1", proc.hasMusic());
    menu.addItem (5, "Clear AUDIO 2", proc.getModel().drums != nullptr);
    menu.showMenuAsync (menuOptionsFor (target), [this] (int r)
    {
        if (r == 1) chooseFile (0);
        else if (r == 2) { proc.armCapture (! proc.isCaptureArmed()); refreshTransport(); waveform.repaint(); }
        else if (r == 3) chooseFile (1);
        else if (r == 4) { proc.clearSlot (0); refreshAll(); }
        else if (r == 5) { proc.clearSlot (1); refreshAll(); }
    });
}

void KoolaidEditor::chooseFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (slot == 1 ? "Load a drum / 808 stem" : "Load the bars before your drop",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) loadFile (f, slot);
    });
}

void KoolaidEditor::loadFile (const juce::File& f, int slot)
{
    juce::String err;
    if (proc.loadAudioFile (f, slot, err))
    {
        setTab (slot);
        waveform.zoomFit();
        waveform.showMessage ("Loaded " + f.getFileName() + (slot == 1 ? " as the drum stem" : ""));
        if (slot == 0)
            askClipBar();   // sync preview needs to know where the audio sits in the project
    }
    else
        waveform.showMessage (err, 5000);
    refreshAll();
}

void KoolaidEditor::setTab (int t)
{
    proc.getSettings().activeTab = t;
    waveform.setTab (t);
    refreshAll();
}

void KoolaidEditor::toggleSync()
{
    proc.setSyncPreview (! proc.getSyncPreview());
    refreshTransport();
    waveform.showMessage (proc.getSyncPreview() ? "Sync preview ON - press play in your DAW" : "Sync preview OFF", 2000);
}

void KoolaidEditor::doRoll()
{
    proc.roll();
    if (! proc.getModel().moves.empty())
        stack.select (proc.getModel().moves.front().uid);
    refreshAll();
    for (auto* b : { &rollBtn, &step2 })
        b->setFlash (true);
    flashing = &rollBtn;
    flashCountdown = 5;
    juce::Timer::callAfterDelay (160, [safe = juce::Component::SafePointer<Btn> (&step2)] { if (safe) safe->setFlash (false); });
    waveform.showMessage ("New idea: " + proc.getSettings().presetName + " - Save As to keep it", 2500);
}

void KoolaidEditor::afterRestore()
{
    const auto& moves = proc.getModel().moves;
    if (stack.selectedIndex() < 0 && ! moves.empty())
        stack.select (moves.front().uid);
    refreshAll();
}

void KoolaidEditor::doExport (bool forDrag, const juce::MouseEvent*)
{
    juce::String err;
    auto files = proc.exportAudio (err);
    if (files.isEmpty())
    {
        waveform.showMessage (err, 4000);
        return;
    }
    lastExport = files.getFirst();
    if (forDrag)
    {
        juce::StringArray paths;
        for (auto& f : files) paths.add (f.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this);
        waveform.showMessage ("Drop " + juce::String (files.size() > 1 ? "them" : "it") + " at bar " + proc.exportLandingBar()
                              + " - " + (files.size() > 1 ? "they land" : "it lands") + " right on the grid", 5000);
    }
    else
    {
        waveform.showMessage ("Saved " + (files.size() > 1 ? juce::String (files.size()) + " stems" : files.getFirst().getFileName())
                              + "  -  drag EXPORT into your DAW, or find it in Documents/Koolaid Man/Exports", 6000);
    }
}

//==============================================================================
bool KoolaidEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f)) return true;
    return false;
}

void KoolaidEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    waveform.showMessage ("Drop to load (drop on the AUDIO 2 tab for a drum stem)", 60000);
}

void KoolaidEditor::fileDragExit (const juce::StringArray&)
{
    waveform.showMessage ({}, 0);
}

void KoolaidEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto p = root.getLocalPoint (this, juce::Point<int> (x, y));
    const bool toDrums = tab2.getBounds().expanded (4).contains (p)
                      || (proc.getSettings().activeTab == 1 && waveform.getBounds().contains (p));
    for (auto& f : files)
        if (isAudioFile (f))
        {
            loadFile (juce::File (f), toDrums ? 1 : 0);
            break;
        }
}
