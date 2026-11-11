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
- **Drop-down arrow** on the right of a row (or right-click): pick the move's variation, move it up or down, duplicate it, randomize it, bypass it, lock it so ROLL keeps it, or delete it.
- **Click a row** to select it. The effect cards then edit that move.
- **+ ADD MOVE** adds any move to the bottom of the stack. **DUPLICATE** and **DELETE** act on the selected row.

**Curves.** Moves that build over time (pitch rise, filter sweep, reverb wash, riser, rising pump, snare fill/roll, hat roll, 808 slide/swell) have a **Curve** in their drop-down menu: **Auto** (the move's own shape), **Linear**, **Ease In** (slow start, fast finish), **Ease Out**, **S-Curve**, **Late Rush** (almost everything in the last moments) or **Steps**. Pick **Custom - Draw It...** to draw your own curve with the mouse (presets in the editor give you a starting point; **Done** or Esc closes it). The TIMELINE draws each move's curve inside its lane.

You can use the same move more than once. For example, two filters, or a short reverse plus a long one.

# The Timeline

Click the **TIMELINE** tab above the waveform to see **one colored lane per move**, in the move's card color. The view zooms to the build zone. Each lane shows *when* that move happens.

- A **dashed, see-through lane** is on **auto**: the move uses its natural spot (a Tape Stop over the last 1/2 bar, a filter sweep over the whole build, and so on).
- **Drag a lane** to move it, or **drag its left or right edge** to change where it starts and ends. It turns solid, and the move now works exactly inside that range. For example, stutter only the second bar, put the riser on the last bar, or land the Impact a beat after the drop. Lanes snap to the grid; hold **Shift** for free movement.
- **Double-click a lane** (or choose **Reset Timing (Auto)** from the move's drop-down in the stack) to put it back on auto. Picking a new length on the move's card also puts it back on auto.
- Timed moves show a **TIMED** tag in the stack. Clicking a lane selects that move, so its card is ready to tweak.
- Timing is stored relative to the drop, so moving the drop takes your timed moves with it. It's saved in presets and in your project.

## The Tension macro

**Tension** brings several moves in together, like one big "build it up" knob.

- **Link moves to it:** open a move's drop-down in the stack and choose **Link to Tension** (linked moves show a yellow **TENSION** badge), or click the yellow **TENSION** lane at the top of the TIMELINE and choose **Link All Moves**.
- **The tension curve** (drawn in the TENSION lane) sets how linked moves come in across the build: pick a shape from the lane's menu, or **Custom - Draw It...**.
- **The Tension knob** sets how much of the linked moves you hear, live: drag the TENSION lane up or down, or **automate the "Tension" parameter in Cubase** to ride the whole build with one lane. At 0% the linked moves are gone; at 100% they're fully in. (**Build Amount** still blends the whole thing against your original audio.)

# Drop Moves & Effects (the Cards)

Each card controls one kind of move. The **green power button** turns that move on or off; if the move isn't in the stack yet, it gets added. Dimmed cards are off. **Click a card's title** for its variations and to add or remove it. Knobs: drag up and down (hold **Shift** for fine control), double-click to reset, or use the mouse wheel. Click a value box (like **1/2** or **–12**) for a list, or scroll over it.

## Reverse

Plays audio backwards into the drop. **Knob = Mix**, **Length** = how much of the end of the zone is reversed. Variations: **Into The Drop** (the end of the build backwards), **Pre-verse** (the drop's own downbeat played backwards just before it hits, the classic swell; needs audio after the drop), **Whole Zone**.

## Chops

Cuts the build up rhythmically. **Knob = Depth**, **Rate** = chop size. Variations: **Gate** (on/off chopping), **Shuffle** (rearranges slices within each bar), **Tremolo** (smooth pulsing).

## Stutter

Repeats a tiny slice of audio, like a DJ stutter. **Knob = Span** (how much of the end of the zone stutters, from 1 beat up to 4 bars), **Rate** = slice size. Variations: **Repeat**, **Accelerate** (the repeats get faster and faster into the drop), **Beat Repeat** (each beat stutters its own first slice).

## Pitch

Bends the pitch of the build without changing its timing, using a studio-grade spectral pitch shifter. **Semitones** = where the pitch ends up (for example –12 is an octave dive, +12 an octave riser). **Knob = Curve** (low = steady glide, high = most of the bend happens right at the end). Variations: **Ramp**, **Static**.

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

# Build & Drop Moves (Page 2)

Click **BUILD & DROP** in the title bar of the cards panel to flip to the second page of cards (**MAIN MOVES** flips back). Selecting a move in the stack also flips to its page. These are the classic tools producers reach for in almost every build. Each card has a **TYPE** box for its variation and a length or rate box; click either for a list, or scroll over them.

## Noise Riser

White noise that sweeps up into the drop. **Level** sets how loud it gets, **Length** sets how long it rises. Types: **Noise Sweep**, **Uplifter** (noise plus a rising synth tone) and **Snare Wash** (1/16 noise bursts that blur into a wall of noise).

## Reverse Crash

A crash cymbal played backwards, so it swells right into the downbeat. Types: **Crash**, **Splash** (short and bright) and **Dark Wash**.

## Sidechain Pump

Ducks the build in time with the beat, like a kick-triggered sidechain compressor. **Depth** sets how hard it ducks, **Rate** sets how often. Types: **Smooth**, **Hard**, **Rising** (starts gentle and gets deeper toward the drop) and **Follow Kick**, which ducks every time *your* kick or 808 hits (found by Detection), so the pump breathes with the real groove instead of a fixed grid. With Follow Kick, **Rate** sets how long it takes to recover.

## Drop Gap

The breath right before the drop hits. **Length** sets how long (one beat is the classic), **Depth** sets how much. Types: **Silence**, **Low-pass** and **High-pass**. Risers and other added sounds keep playing through the gap.

## Impact

A hit plus a crash on the **downbeat of the drop**. **Tail** sets how long it rings. Types: **Hit + Crash**, **Hit Only** and **Crash + Downlifter** (a noise sweep falling away from the drop).

## Sub Boom

A deep sub drop on the downbeat. Types: **Deep**, **Punchy** and **Long Dive**.

**Impact** and **Sub Boom** play *after* the drop. If your audio ends right at the drop, Koolaid Man adds a tail (rounded up to whole bars) so you still hear them, and the export includes it.

# FX Moves (Page 3)

Click **FX MOVES** in the title bar of the cards panel for the third page of cards. These are the mix moves engineers use to make a drop feel bigger: echo throws, crunch, motion, width and turntable tricks. Each card works like the other pages: the **power button** switches it on (adding it to the stack), **TYPE** picks the variation and the second box sets the length (or the echo time).

## Delay Throw

Tempo-synced echoes that bounce left and right. **Time** sets the echo time (1/8, 1/4 and so on), **Feedback** sets how long the echoes last. Types:

- **Throw**: the classic. The first beat of the last bar goes into the delay, then the track drops out and only the echoes carry you into the drop.
- **Ping-Pong Build**: more and more of the build is fed into ping-pong echoes as the drop gets closer.
- **Tape Echo**: the same idea, but each repeat gets darker and a little saturated, with a bit of tape wobble.

## Bitcrush

Crushes the audio harder and harder into the drop. **Crush** sets how far it goes. Types: **Bit Ramp** (fewer and fewer bits, down to a gritty 2-bit fizz), **Downsample** (the sample rate drops until it sounds like an old sampler) and **Full Crush** (both, plus some drive).

## Phaser

- **Barber-Pole Rise**: a phaser whose notches glide **up forever**. As one leaves the top, a new one enters at the bottom, so the build sounds like it keeps rising without ever resetting. It speeds up as the drop gets closer.
- **Phaser Sweep**: a classic six-stage phaser, sweeping faster and faster.
- **Flanger**: a jet-plane flanger, sweeping faster and faster.

**Depth** sets how strong it is.

## Stereo Width

- **Collapse To Mono**: squeezes the build down to mono, so when the drop hits at full width it feels huge. This is one of the most effective drop tricks there is.
- **Widen Out**: makes the build wider and wider. Your bass stays in the middle, and the level is kept steady.
- **Mono Snap**: switches to mono for the last stretch (set by Length).

**Amount** sets how far it goes.

## Vinyl Scratch

Turntable moves on the end of the build. **Speed** changes each one:

- **Baby Scratch**: quick forward-and-back scratches on the downbeat of that stretch, locked to the beat. Higher speed = faster scratches (1/8 up to 1/32).
- **Rewind**: the DJ grabs the record and spins it backwards into the drop. Higher speed = a faster spin.
- **Brake**: the turntable's power is cut and the record slows to a stop, with a little needle crackle. Higher speed = it stops sooner, leaving a silent beat before the drop.

## Pre-Verb

The **drop's own first hit**, played through a big reverb and then backwards, so it swells up and sucks right into the drop. Because it's made from your drop, it always fits the song. Types: **Downbeat** (the first beat of the drop), **Drop Bar** (the drop's whole first bar, a softer, bigger swell) and **Snare** (the snare found by Detection). **Level** sets how loud it gets, **Length** sets how long it swells. If your audio ends right at the drop, it uses the last downbeat of the build instead.

# Detection

Koolaid Man listens to your audio (or to AUDIO 2, if you loaded a stem) and finds the **snare**, the **808/kick** and the **hi-hats**. Their exact hits are used by Drum Moves, 808 Detect and Hat Detect.

- A **green check** means it was found. **(Synth)** means it wasn't, so that move uses a built-in sound instead.
- **ANALYZE DRUMS** runs the detection again. It also runs automatically whenever the audio changes.
- If something isn't found, turn up **SENS** on the 808 or Hat card, or load a drum stem into AUDIO 2.
- **KEY** (top right of the Detection panel) shows your song's key. Risers (Uplifter), **Sub Boom**, the **Impact**'s low thump and the backup 808 are tuned to it, so they land on your root note instead of clashing. Click it to pick the key by hand, go back to **Auto**, or turn tuning **Off**. Your own detected 808 always keeps its real pitch.
- Click the **AUDIO 2** tab to see where each hit was found (pink = snare, yellow = 808, cyan = hats).

# ROLL and Presets

**ROLL** (or **INSTANT IDEA**, or the **2 BUILD THE DROP** banner) builds a brand new stack of moves: a couple of moves that shape the build, sometimes an 808, and a move into the drop. Keep rolling until something hits, then tweak it.

## Roll styles

**Right-click ROLL** (or INSTANT IDEA) to pick a style: **Any**, **Trap**, **Drill**, **Hip-Hop**, **EDM**, **House**, **Bass**, **Pop** or **Chill**. Picking a style rolls straight away, and every ROLL after that uses it until you change it. You can also set it under **ROLL Style** in the gear menu. Each style leans on the moves that genre is known for:

- **Trap**: 808 slides, hat rolls, snare rolls and a silent gap before the drop.
- **Drill**: triplet hats, sliding 808s, chops and gaps.
- **Hip-Hop**: filter dips, tape stops, reverses and gentle chops.
- **EDM**: snare builds, risers, pitch going up and big impacts.
- **House**: filter sweeps, sidechain pumping and crash swells.
- **Bass**: speeding-up stutters, pitch dives, gaps and sub booms.
- **Pop**: shimmer and cloud reverbs, chopped lifts and crash swells.
- **Chill**: muffled filters, reverb washes and slow tape stops.

Rolls are named after their style (for example "Trap Roll #4821"). The style is saved with your project.

## Locking moves

Found a move you love but want to keep rolling the rest? Open the row's drop-down and pick **Lock (Keep on ROLL)**. A pink **padlock** appears on the row. ROLL keeps locked moves exactly as they are (settings, curve, timing and all) and only rolls new moves around them. It never adds a second move of a type you've locked. Pick the menu item again to unlock it.

## Presets

- **Click the preset name** or **BROWSE** to open the list. Factory presets are grouped by genre: **Classic** (the originals) plus six each for **Trap**, **Drill**, **Hip-Hop**, **EDM**, **House**, **Bass**, **Pop** and **Chill**. Your own presets are listed underneath under **YOUR PRESETS**.
- The **arrow buttons** either side of the preset name step through every preset in order.
- **SAVE** overwrites your current preset (or works like Save As for factory presets and rolls). **SAVE AS** saves a new one.
- A **\*** after the name means you've changed something since loading it.
- Presets store the moves stack only, not the audio. Your own presets live in **Documents/Koolaid Man/Presets**.

Your whole session (audio, zone, moves and settings) is saved with your DAW project automatically.

# Undo, A/B and Build Amount

## Undo and Redo

The two arrow buttons at the right of the **MOVES STACK** title bar undo and redo your changes to the moves, their timing and the build zone. A knob drag counts as one step, so you don't have to click undo fifty times.

## A/B compare

The **A** and **B** buttons next to the TIMELINE tab hold two completely separate versions of your drop. Click **B** to start a second version (it starts as a copy of A), change whatever you like, then flip between **A** and **B** while it plays to hear which hits harder. **COPY TO B** (or **COPY TO A**) copies the version you're on into the other slot. Both versions are saved with your project.

## Build Amount (automate it!)

**Build Amount** is a parameter you can automate in your DAW (in Cubase, show the track's automation and pick *Build Amount*). At **100%** you hear the full drop; at **0%** you hear your original audio. In between, it blends smoothly, live while it plays. Ride it to bring the build in gradually or to pull it back in places. Exports use the current Build Amount.

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
- **STEMS: MUSIC + FX**: two files at once, the processed music and the added sounds, so you can mix them separately. Drag the button and both come along.

**Formats:** WAV 16-bit, WAV 24-bit, WAV 32-bit float or AIFF 24-bit, at your project's sample rate. 16-bit exports get proper **TPDF dither with noise shaping**, so quiet tails don't turn grainy.

**Levels are handled for you.** Everything a move adds (808s, drum hits, reverb) is matched to your track's own level, no beat of the build can jump more than about 3.5 dB above your original audio, and a **true-peak limiter** keeps the build under -1 dBTP (or under your track's own peak if it's already mastered hotter). The rest of your audio is never touched. After placing the export, bypass or remove Koolaid Man on the track (or turn SYNC TO SESSION off) so you don't hear the preview on top.

# Settings (Gear Menu)

- **Manual (How To Use)**: opens this manual. You can also click the small **KOOLAID MAN** label next to PRESET.
- **Clip Starts At Bar**: where the audio sits in your project.
- **Zone Snap**: bar, beat or 1/16.
- **In-Sync Preview**: replace the track's audio, or layer on top of it.
- **Window Size**: 50% to 125%. You can also drag the window's corner.
- **Theme**: Dark or Light. Clicking the Koolaid Man mascot does the same. Remembered for all your projects.
- **ROLL Style**: which genre ROLL builds for (same as right-clicking ROLL).
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
