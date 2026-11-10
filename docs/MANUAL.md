# Welcome

**Koolaid Man** is a beat-drop creator. You give it the bars that lead into your drop, it builds the drop for you from a stack of **moves** (reverses, chops, stutters, pitch dives, tape stops, filter sweeps, snare fills, 808 slides, hat rolls and big reverbs), you hear it in time with your song, and then you drag the finished audio back into your DAW. The exported file is cut exactly on bar lines, so it lands right on the grid.

The four banners at the top of the window are the whole workflow, and you can click them:

1. **DRAG IN AUDIO**: load the bars before your drop (click for all load options).
2. **BUILD THE DROP**: click for an instant idea (same as ROLL).
3. **PREVIEW IN SYNC**: turns in-sync preview on and off.
4. **DRAG BACK TO DAW**: drag it into your project (click to just save the file).

Tip: hover over almost anything to see a tooltip that says what it does.

**Light or dark?** Click **Koolaid Man himself** (the mascot at the top left) to switch between the dark look and the light look, or use **Gear menu > Theme**. Your choice is remembered for next time.

# Quick Start (Cubase)

1. Put **Koolaid Man** on an **Insert** slot of the audio track that holds your song (your two-track). It's under **Tools**.
2. Get the bars before your drop into the plug-in. The easiest way: click the **CAPTURE** button (the little waveform button at the top right of the waveform display), then press play in Cubase a bar or two before the section you want. Recording starts on the next bar line. Press stop when you're done.
3. Drag the pink **PRE-DROP (BUILD ZONE)** so its right edge sits where the drop hits.
4. Hit **ROLL** until you like something, or switch moves on with the power buttons on the cards.
5. Make sure **SYNC TO SESSION** is lit and press play in Cubase. Inside the captured bars you now hear the new drop instead of the original audio.
6. Drag **EXPORT / DRAG TO DAW** onto an empty audio track at the bar it tells you, then bypass or remove the plug-in. Done!

# Getting Audio In

## Drag and drop

Drag a **WAV, AIFF, FLAC, MP3 or OGG** file onto the plug-in window. Files can come from Explorer/Finder or from Cubase's **MediaBay** or **Pool**. Drop it on the **AUDIO 2** tab to load it as a drum stem instead.

## Load button

Click the empty waveform display, or click the **1 DRAG IN AUDIO** banner and choose **Load Audio File**.

## Capture from your track

Click **CAPTURE** (the waveform icon at the top right of the display) so it glows pink, then press play in your DAW. Koolaid Man records the audio coming through the track, **starting exactly on the next bar line**, and stops when you stop. It trims the capture to whole bars, remembers which bar it started on, and picks up your project tempo automatically. Click CAPTURE again to cancel.

## Where the audio sits in your project

For in-sync preview (and so it can tell you where to drop the export), the plug-in needs to know which bar of your project the audio starts on. Captures set this automatically. When you load a file it asks you. To change it later, click the **BARS:** read-out above the waveform, or use **Gear menu > Clip Starts At Bar**.

## AUDIO 2 (Drums/808)

Optional. If you have a separate drum or 808 stem that lines up with your two-track, load it here. Detection then listens to the stem instead of the full mix, which gives cleaner results. The stem is only used for detection and for the drum sounds; it isn't played back.

## FX LAYER

This tab shows only the sounds the moves **add** (snare fills, 808s, hats and reverb), so you can see what's going on. You can also export just this layer.

# Tempo and Time Signature

When **SYNC** (next to TEMPO) is lit, Koolaid Man follows your DAW's tempo and time signature. That's what you want almost all the time.

Turn SYNC off to set them by hand: drag the tempo number up and down (hold **Shift** for fine steps), double-click it to type a value, and click the **BAR** box to choose a time signature. The audio you load should be at the tempo shown, so the bar lines match the music.

# The Build Zone

The pink **PRE-DROP (BUILD ZONE)** is the part of your audio that gets turned into the build. Its **right edge is the drop**: the moment the beat comes back in.

- **Drag the left or right handle** to resize it. **Drag the middle** to move it.
- It snaps to beats. Hold **Shift** while dragging for no snapping. Change the snap (bar, beat or 1/16) in **Gear menu > Zone Snap**.
- **LENGTH** sets how long the build is. **DROP AT** sets the bar where the drop hits.
- **Click outside the zone** to move the playhead there.
- **+ / –** zoom in and out, the square button zooms to fit. You can also hold **Ctrl** (Cmd on Mac) and use the mouse wheel to zoom, and use the plain wheel to scroll when zoomed in.

Everything outside the zone stays exactly as it was. Some moves (like **Reverse > Pre-verse**) borrow audio from just after the drop, so include a bar or two after the drop in your audio if you want those.

# The Moves Stack

**BUILD THE DROP – MOVES STACK** is the list of moves applied to the build zone, **top to bottom**. Order matters. For example, a Tape Stop at the bottom also stops the snare fill above it.

- **Pink checkbox**: turns a move on or off without deleting it.
- **Drag a row** (grab it by the dots on the left) to change the order.
- **Drop-down arrow** on the right of a row (or right-click): pick the move's variation, move it up or down, duplicate it, randomize it, bypass it or delete it.
- **Click a row** to select it. The effect cards then edit that move.
- **+ ADD MOVE** adds any move to the bottom of the stack. **DUPLICATE** and **DELETE** act on the selected row.

You can use the same move more than once. For example, two filters, or a short reverse plus a long one.

# Drop Moves & Effects (the Cards)

Each card controls one kind of move. The **green power button** turns that move on or off; if the move isn't in the stack yet, it gets added. Dimmed cards are off. **Click a card's title** for its variations and to add or remove it. Knobs: drag up and down (hold **Shift** for fine control), double-click to reset, or use the mouse wheel. Click a value box (like **1/2** or **–12**) for a list, or scroll over it.

## Reverse

Plays audio backwards into the drop. **Knob = Mix**, **Length** = how much of the end of the zone is reversed. Variations: **Into The Drop** (the end of the build backwards), **Pre-verse** (the drop's own downbeat played backwards just before it hits, the classic swell; needs audio after the drop), **Whole Zone**.

## Chops

Cuts the build up rhythmically. **Knob = Depth**, **Rate** = chop size. Variations: **Gate** (on/off chopping), **Shuffle** (rearranges slices within each bar), **Tremolo** (smooth pulsing).

## Stutter

Repeats a tiny slice of audio, like a DJ stutter. **Knob = Span** (how much of the end of the zone stutters, from 1 beat up to 4 bars), **Rate** = slice size. Variations: **Repeat**, **Accelerate** (the repeats get faster and faster into the drop), **Beat Repeat** (each beat stutters its own first slice).

## Pitch

Bends the pitch of the build without changing its timing. **Semitones** = where the pitch ends up (for example –12 is an octave dive, +12 an octave riser). **Knob = Curve** (low = steady glide, high = most of the bend happens right at the end). Variations: **Ramp**, **Static**.

## Tape Stop

The classic slow-down. **Time** = how long the stop takes, **Knob = Brake** (how hard it slams). Variations: **Stop**, **Spin-Up** (starts from nothing and speeds up into the drop).

## Filters

Sweeps a filter across the build. **Cutoff** = where the sweep ends, **Res** = resonance. Variations: **LP Sweep** (muffles down, the classic), **HP Sweep** (thins out the low end, great before a big bass drop), **BP Sweep**, **LP Static**, **HP Static**.

## Drum Moves

Uses the **snare found in your audio**. **FILL** plays it in 1/4s, then 1/8s, 1/16s and 1/32s into the drop. **GATE** chops the build in time with the drum hits found in your audio. **ROLL** plays a steady 1/16 snare roll that gets louder and higher.

## 808 Detect

Finds the 808/kick in your audio (the meter shows how sure it is) and adds an **808 slide** tuned to it at the end of the build. **SENS** = detection sensitivity. Variations (card title or stack drop-down): **Slide Down**, **Slide Up**, **Sub Swell**.

## Hat Detect

Finds the hi-hats in your audio and **rolls** them into the drop. **SENS** = detection sensitivity. Variations: **Roll** (1/16 then triplets then 1/32), **Triplet Roll**, **Open Hats** (off-beat open hats).

## Atmos Reverb

Seven big atmospheric reverbs that swell up across the build: **Hall, Cathedral, Shimmer, Cloud, Plate, Deep Space** and **Reverse Swell**. **Size** = how big and long, **Mix** = how loud the wash gets. Click the card title to choose the reverb.

# Detection

Koolaid Man listens to your audio (or to AUDIO 2, if you loaded a stem) and finds the **snare**, the **808/kick** and the **hi-hats**. Their exact hits are used by Drum Moves, 808 Detect and Hat Detect.

- A **green check** means it was found. **(Synth)** means it wasn't, so that move uses a built-in sound instead.
- **ANALYZE DRUMS** runs the detection again. It also runs automatically whenever the audio changes.
- If something isn't found, turn up **SENS** on the 808 or Hat card, or load a drum stem into AUDIO 2.
- Click the **AUDIO 2** tab to see where each hit was found (pink = snare, yellow = 808, cyan = hats).

# ROLL and Presets

**ROLL** (or **INSTANT IDEA**, or the **2 BUILD THE DROP** banner) builds a brand new stack of moves: a couple of moves that shape the build, sometimes an 808, and a move into the drop. Keep rolling until something hits, then tweak it.

- The **arrow buttons** either side of the preset name step through presets. **Click the preset name** or **BROWSE** to pick one from the list.
- **SAVE** overwrites your current preset (or works like Save As for factory presets and rolls). **SAVE AS** saves a new one.
- A **\*** after the name means you've changed something since loading it.
- Presets store the moves stack only, not the audio. Your own presets live in **Documents/Koolaid Man/Presets**.

Your whole session (audio, zone, moves and settings) is saved with your DAW project automatically.

# Previewing

## Local preview (no DAW needed)

The buttons under the waveform, left to right: **Play/Pause**, **Stop** (and back to the start), **Loop**, **Back to start**, and **Jump** (to one bar before the drop).

**PLAYBACK** chooses what plays: **PRE-DROP** (just the build zone), **FULL CLIP**, **INTO THE DROP** (a bar before the zone through two bars after the drop) or **ORIGINAL (A/B)** (the untouched audio, for comparing).

## In sync with your session

When **SYNC TO SESSION** / **PREVIEW (IN SYNC)** is lit and your DAW is playing, Koolaid Man plays the finished drop **at the exact bars** where the audio sits in your project, sample-accurate, and replaces the track's own audio there. Outside those bars your track plays normally. The playhead turns yellow while it's following the DAW.

- Want to hear it on top of the original instead? **Gear menu > In-Sync Preview > Layer On Top**.
- If the drop plays in the wrong place, check the clip's start bar (click the **BARS:** read-out).
- The **Preview Level** parameter (in your DAW's automation/parameter list) sets the preview volume.

# Exporting (Drag Back To DAW)

**Drag EXPORT / DRAG TO DAW** (or the **4** banner) and drop it onto an audio track in your project. The plug-in tells you which bar to drop it on (also shown in the button's tooltip). The file starts exactly on that bar line, so with snap on it lands right on the grid.

**Click** the button instead to just save the file. Every export is saved in **Documents/Koolaid Man/Exports** (open it from **Gear menu > Open Export Folder**).

## What to export

- **FULL CLIP**: the whole audio with the new drop. Drop it at the clip's start bar.
- **BUILD ZONE**: just the build, starting at the zone's start.
- **ZONE + DROP**: from the start of the build to the end of the audio.
- **FX LAYER**: only the added sounds, to layer on top of your original.

**Formats:** WAV 16-bit, WAV 24-bit, WAV 32-bit float or AIFF 24-bit, at your project's sample rate. After placing the export, bypass or remove Koolaid Man on the track (or turn SYNC TO SESSION off) so you don't hear the preview on top.

# Settings (Gear Menu)

- **Manual (How To Use)**: opens this manual. You can also click the small **KOOLAID MAN** label next to PRESET.
- **Clip Starts At Bar**: where the audio sits in your project.
- **Zone Snap**: bar, beat or 1/16.
- **In-Sync Preview**: replace the track's audio, or layer on top of it.
- **Window Size**: 50% to 125%. You can also drag the window's corner.
- **Theme**: Dark or Light. Clicking the Koolaid Man mascot does the same. Remembered for all your projects.
- **Open Export Folder / Open Presets Folder**.
- **Clear AUDIO 1 / AUDIO 2**.

# Troubleshooting

## The plug-in doesn't show up in Cubase

Make sure the whole **Koolaid Man.vst3** folder is in **C:\\Program Files\\Common Files\\VST3** (Mac: ~/Library/Audio/Plug-Ins/VST3), then rescan with **Studio > VST Plug-in Manager**. Look under **Tools**.

## I hear nothing when I press play in Cubase

Check that **SYNC TO SESSION** is lit, that the playhead is inside the bars shown in **BARS:**, and that the clip start bar is right. Outside the clip, the track just plays normally.

## The bars don't line up with the music

The tempo or time signature doesn't match the audio. Keep **SYNC** on (so it follows Cubase), or turn it off and set the tempo of the file by hand.

## Dragging the export into Cubase doesn't work

Click **EXPORT** to save the file, then open **Gear menu > Open Export Folder** and drag the newest file from there into Cubase.

## Detection says (Synth)

The drum wasn't found clearly in a busy mix. Turn up **SENS**, press **ANALYZE DRUMS**, or load a drum stem into **AUDIO 2**. The moves still work with built-in sounds.

## The window is too big or too small

Use **Gear menu > Window Size**, or drag the bottom-right corner of the plug-in window.
