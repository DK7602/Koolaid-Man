# Koolaid Man: Competitor Research & Pro Audit

*November 2026*

**Short version:** Koolaid Man now passes the two industry validators used to judge plugins (**pluginval** at strictness 8 and **Apple's auval** in strict mode). The audit found and fixed **11 real problems**, two of them sound-quality bugs and one a 96 kHz detection failure. Against the competition, Koolaid Man's workflow is already unusual: it captures from your track, uses *your* snare/hats/808, and drops the result back on the grid. Its biggest gaps are **musical intelligence** (key-aware tuning, curves, a macro knob) and **content** (11 presets vs. hundreds).

> **Update (later the same day): all 10 features below are now built.** See [Part 3](#part-3-what-shipped-since-the-audit) for what shipped and how each one was checked.

---

## Part 1: What the best build/drop tools do

| Product | What it is | What it does best |
|---|---|---|
| **iZotope Stutter Edit 2** | Tempo-synced stutter/glitch "gestures" | Gestures fired from MIDI keys or one *Auto* button; a **curve editor** so every knob moves in sync with the song; tempo-locked filters |
| **Cableguys ShaperBox 3** | Modular shaper (volume, filter, time, drive, noise, width…) | **Audio-triggered** modulation: effects follow transients in your audio; ~600 presets; a dedicated *Builds & Risers* pack |
| **Baby Audio Transit 2** | 28-module transition multi-FX | **One Macro knob** sweeps every linked effect, each with its own ramp curve; six motion modes (LFO, follower, sidechain, gate, sequencer) |
| **Dada Life Endless Smile** | One-knob build-up | A single **Intensity** knob you automate over the build; turning it fully back gives the drop. Loved for speed, criticised for little control |
| **UJAM Finisher BOOST** | Multi-effect build tool | 50 modes, 2/4/8-bar sync, presets that end the build in **silence** before the drop |
| **BOOM liftFX** | Riser/drop **synth** | Rises and falls **tuned to a pitch**, beats-or-seconds length, **time lock** while browsing presets, 275 presets |
| **AIR The Riser** | Riser synth | Sweep + noise + **chord** generators (chords can follow your MIDI), a tempo-synced "Pumper", 300 presets |
| **NI Rise & Hit** | Kontakt rise + hit layers | Up to 4 layers of rise→hit, host-synced 1–32 beats |
| **Sugar Bytes Effectrix 2** | 32-step multi-FX sequencer | Paint effects per step (loop, stutter, scratch, granular); **smart randomise** |
| **Image-Line Gross Beat** | Time/volume envelope mangler | Drawn **time envelopes** for stops, scratches and reverses; scratch control |
| **Kreuzberg Uplifter** | Riser/drop generator | One note + duration → transition; **use your own WAV**; true-peak limiter |

**Where Koolaid Man is already ahead:** none of these work on a two-track the way Koolaid Man does. Capturing the bars before the drop, building from *your own* detected snare/hats/808, previewing in sync, and dragging a bar-aligned file back is a workflow most of them don't offer. The only near-overlap is ShaperBox's audio triggering. Stems, A/B, undo and a per-move timeline also put it ahead of the one-knob tools.

### Features worth adding (ranked by impact for Koolaid Man)

| # | Feature | Seen in | Why it matters | Effort |
|---|---|---|---|---|
| 1 | **Key-aware tuning**: detect the song's key and tune the 808 slide, Sub Boom, Uplifter and Impact to it | liftFX, The Riser | Out-of-key risers/subs are the #1 thing that makes a build sound amateur. We already detect the 808's pitch | Medium |
| 2 | **"Follow my kick" Sidechain Pump**: duck from the kicks found in your audio, not just the grid | ShaperBox 3 | Pumps that breathe with the real groove. Reuses our detection | Small |
| 3 | **Curve shape per move** (linear / ease-in / ease-out / S / step), later **drawable curves** in the timeline | Stutter Edit 2, Transit, Gross Beat | Right now every ramp uses a fixed shape; producers shape tension | Small → Medium |
| 4 | **Tension macro**: one automatable knob that drives *linked* parameters (filter, pump depth, riser level, stutter rate…) | Transit, Endless Smile, Finisher | Build Amount only crossfades today; a real macro makes one automation lane do the whole build | Medium |
| 5 | **Preset library by genre** (trap, drill, EDM, house, hip-hop, pop) + **ROLL with locks** (keep some moves, re-roll others) + roll "style" | everyone (250–600 presets); Effectrix, liftFX | 11 presets is thin; locks make ROLL a real idea generator | Medium (mostly content) |
| 6 | **New moves**: delay throw / echo-out, bitcrush ramp, phaser / barber-pole riser, stereo-width collapse → widen, vinyl scratch & brake, **pre-verb** (reversed reverb of the drop's downbeat) | Transit, Endless Smile, Gross Beat, vocal-riser tools | Classic transitions we don't have yet | Small each |
| 7 | **Step-pattern editor** for Chops/Stutter (16/32 steps) | Effectrix 2, Gross Beat | Custom rhythmic chops instead of fixed patterns | Medium |
| 8 | **Use your own riser/impact sample** (e.g., a reversed vocal) | Uplifter, Rise & Hit | Personal sound, not just synthesis | Small |
| 9 | **Live mode**: apply a build in real time to any track (no capture), plus **MIDI-triggered gestures** | Stutter Edit 2, Endless Smile, Transit | Performance use; a big architectural addition (today everything is rendered) | Large |
| 10 | **Seconds or beats** for lengths | liftFX, Rise & Hit | Useful for tempo-free intros | Small |

My suggested next batch: **#2, #3, #6** (quick wins), then **#1 and #4** (the big "pro sound" upgrades), then **#5** (content). *(Done: all ten were built in order, #1 to #10.)*

---

## Part 2: Pro-tier audit

### Scorecard

| Area | Status | Notes |
|---|---|---|
| Industry validators | ✅ | **pluginval strictness 8**: all suites passed, including Steinberg's VST3 validator, parameter thread safety, fuzzing, state restore, background-thread state, editor while processing. **auval -strict**: passed at 11 kHz–192 kHz, 64-sample slicing |
| Real-time (audio-thread) safety | ✅ | No allocation, locks or file access on the audio thread; render hand-off is now a proper hazard pointer (fixed today) |
| Thread safety | ✅ / ⚠️ | Rendering and analysis are on a background thread with newest-request-wins. State saving assumes the host saves on its main thread (Cubase does) |
| Sample rates | ✅ | Engine tests now run at **44.1, 48 and 96 kHz** in CI (96 kHz had a real bug, fixed) |
| Sound quality | ✅ | Spectral pitch shifting, oversampled filters, true-peak limiting that never touches your untouched audio, dithered 16-bit, stems that sum exactly |
| Performance & memory | ✅ | 4-min song: re-render ~0.4 s, analysis 181 MB peak (was 313 MB); renders store only what changes |
| Projects / state | ✅ | Audio saved inside the project; repeat saves of a 3-min song 85 ms (was 570 ms); A/B and undo state restored |
| Host behaviour | ✅ | Sample-accurate sync preview, bar-aligned capture, cycle-mode capture, click-free switching, ramped automation |
| UI | ⚠️ | Works at 50–150%, light/dark. Art is 1448×1086, so it softens on 4K screens at large sizes. No keyboard shortcuts or screen-reader support |
| Distribution | ⚠️ | Windows installer and macOS build in CI. Not code-signed (SmartScreen/Gatekeeper warnings), fine for personal use. VST3 + AU + Standalone (no AAX/CLAP; Cubase doesn't need them) |
| Real-world testing | ❌ (needs you) | Every check so far is automated. Nobody has *listened* in Cubase yet, and detection has only been tested on a synthetic beat |

### Fixed today (each covered by a new or extended automated test)

| Problem | Impact | Fix | Result |
|---|---|---|---|
| **Stems didn't add up to the mix** | Exported music + FX stems ≠ the full export (off by up to −15 dB right at the drop) | One limiter gain shared by all layers; the de-click fade covers the FX stem | Stems null exactly (12/12 random renders) |
| **Limiter shaved hot masters** | Where your track was already louder than −1 dBTP, original peaks got trimmed even where nothing changed | Limiter never pulls audio below its original level | Untouched audio stays bit-identical |
| **Snare not found at 96 kHz** | Drum Moves fell back to a synth snare in 96 kHz projects | Detection bands capped at 16 kHz | Same results at 44.1/48/96 kHz |
| **Whole-song memory and speed** | Each tweak copied a whole song 3×; 4-min song re-render 0.7 s, 370 MB | Renders store only the build zone onward | Flat memory; ~0.4 s re-render |
| **Triple true-peak analysis** | ~120 ms wasted per render | Analyse once | Faster on every tweak |
| **Export rendered twice** | Export waited for a second render | Reuses the render on screen | Instant when up to date |
| **Timing-based memory safety** | Old renders were freed after a 2 s guess, unsafe if the audio thread stalls | Hazard pointer | Freed only when provably unused |
| **Clicks at clip edges** | In-sync preview switched track ↔ drop instantly | 5 ms crossfades | Largest step 0.003 in test |
| **Preview Level zipper noise** | Automation stepped per block | Ramped gain | Smooth |
| **Capture in cycle mode** | Looping glued repeats into the capture | Capture ends at the jump | Keeps whole bars up to the loop |
| **Slow project saves** | Every save re-encoded the audio (0.57 s for 3 min) | Encode once, reuse | 85 ms |
| **Analysis memory** | Four filtered copies of the song | Measured on the fly | 313 → 181 MB peak |
| **Version always 1.0.0** | Cubase couldn't tell builds apart | Version = CI build number | Cubase, About box and installer match |

### Remaining gaps (not fixed, and why)

1. **Needs your ears and your Cubase.** Sound judgement, drag-and-drop into Cubase, and typing in the plugin's dialogs inside Cubase can only be confirmed by you.
2. **Detection on real, dense mixes.** The tests use a synthetic beat. **Best next step:** send me 3–5 of your two-tracks (or the exports you're happy/unhappy with) and I'll build a real-music test set and tune detection against it.
3. **Bouncing right after a tweak.** If you export a mixdown in Cubase before Koolaid Man finishes re-rendering (≈0.1–0.4 s), the previous version is bounced. Making offline bounces wait is possible but risks deadlocks in some hosts, so I left it. Just give it a beat after tweaking.
4. **Capture reserves memory while armed** (~92 MB at 48 kHz for up to 4 minutes; 184 MB at 96 kHz), released afterwards.
5. **HiDPI sharpness.** A 2× version of the artwork would keep it razor-sharp on 4K screens.
6. **Code signing**, **AAX/CLAP**, **accessibility**, **preset tags/search**: only matter for selling or for other DAWs.

---

## Part 3: What shipped since the audit

| # | Feature | What you get | How it was checked |
|---|---|---|---|
| 1 | **Key-aware tuning** | Song key detected (KEY box; Auto / pick / Off). Uplifter, Sub Boom, the Impact's thump and the backup 808 land on the root | 4/4 test chord progressions in different keys detected at 44.1/48/96 kHz; Sub Boom in A minor ends at 55 Hz |
| 2 | **Pump: Follow Kick** | Ducks on the kicks/808s found in your audio | Ducks after the detected kicks, not after the snares in between |
| 3 | **Curves** | Auto, Linear, Ease In/Out, S-Curve, Late Rush, Steps, or draw your own; drawn in the TIMELINE | A riser's energy moves later (Late Rush) or earlier (Ease Out) than Linear, as it should |
| 4 | **Tension macro** | Link moves to one automatable "Tension" knob; they come in along a curve you choose | Tension 0% equals the build without linked moves (engine and live preview) |
| 5 | **Genre presets + smarter ROLL** | 67 presets in 9 genres; ROLL styles; lock moves so ROLL keeps them | Every preset renders cleanly; each style favours its signature moves (300+ rolls each); locks survive rolls and saves |
| 6 | **Six FX moves** (new FX MOVES tab) | Delay Throw, Bitcrush, Phaser (incl. an endlessly rising barber-pole), Stereo Width, Vinyl Scratch/Rewind/Brake, Pre-Verb | Every variation renders cleanly with no level jumps; barber-pole notches measured rising on noise; width, throws and swells measured |
| 7 | **Step patterns** | 16-step grids for Chops and Stutter | Off steps exactly silent (Chops) / untouched (Stutter, Shuffle) |
| 8 | **Your own samples** | Drop a WAV on the Riser, Reverse Crash, Impact or Sub Boom card; auto-aligned to the drop; saved inside the project | Placement checked for all four; project reopens with the file deleted; presets reload from disk |
| 9 | **LIVE PADS** | Eight real-time, tempo-synced gestures from the screen, MIDI notes C1-G1 or automation | 25 checks incl. sample-exact stutter repeats, echo timing, 30 s random-pad fuzzing at two tempos, exact pass-through when idle; MIDI-driven host test |
| 10 | **Seconds or beats** | Any length can be set in seconds (tempo-free) | A 1 s tape stop lasts 1 s at 130 and 90 bpm |

### Hardening pass on the new features

- **All 22 move types stacked at once** render cleanly with no level jump (0.8 s at 48 kHz, 1.7 s at 96 kHz; normal 3-6 move stacks are a fraction of that).
- **Tempo extremes**: random rolls at 60 and 200 bpm render cleanly.
- **Live pads**: random pad hammering never produces a bad sample, and every pad held at once is stable. A pad can no longer stick on if the plug-in window is closed while you hold it.
- **Presets**: the arrows now step through one genre at a time.
- **Validators**: pluginval (strictness 8) and auval (strict) still pass with the new MIDI input. The VST3 exposes 13 parameters (MIDI-CC emulation is off, so Cubase's automation list stays clean).

### Still open

The gaps listed in Part 2 still apply. Above all, **nobody has listened yet**. The checks above prove timing, placement and levels, not taste. The new things most worth an ear are the barber-pole phaser, the vinyl moves, Pre-Verb and the live pads.

---

## Sources

- [iZotope: Learn Stutter Edit 2 in 10 minutes](https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes) · [Stutter Edit 2 features](https://www.izotope.com/en/products/stutter-edit/features) · [MusicTech](https://www.musictech.net/news/gear/izotope-stutter-edit-2/)
- [Sound On Sound: ShaperBox 3 review](https://www.soundonsound.com/reviews/cableguys-shaperbox-3) · [MusicRadar](https://www.musicradar.com/news/cableguys-shaperbox3-plugin)
- [Rekkerd: Transit 2 review](https://rekkerd.org/review-transit-2-motion-effects-playground-by-baby-audio/) · [Avid: Transit 2](https://www.avid.com/plugins/baby-audio-transit-2) · [Plugin Boutique: Transit 2](https://www.pluginboutique.com/products/13431-Transit-2)
- [MusicRadar: Endless Smile](https://www.musicradar.com/news/dada-lifes-endless-smile-lets-you-create-build-ups-risers-and-transitions-with-one-knob) · [Bonedo test](https://www.bonedo.de/artikel/dada-life-endless-smile-test)
- [UJAM: Finisher BOOST](https://www.ujam.com/finisher/boost/) · [UJAM: huge EDM transitions](https://www.ujam.com/tutorials/how-to-create-huge-edm-transitions/)
- [Gearnews: liftFX](https://www.gearnews.com/boom-library-liftfx-creates-playable-build-ups-and-risers-on-the-fly) · [KVR: liftFX](https://kvraudio.com/news/boom-library-releases-liftfx-for-mac-and-win-aax-vst-and-au-45975)
- [AIR: The Riser](https://www.airmusictech.com/virtual-instruments/the-riser/) · [MusicRadar review](https://www.musicradar.com/reviews/tech/air-music-technology-the-riser-613697)
- [Native Instruments: Rise & Hit](https://www.native-instruments.com/en/products/komplete/cinematic/rise-hit/) · [MusicRadar review](https://www.musicradar.com/reviews/tech/native-instruments-rise-hit-603137)
- [MusicRadar: Effectrix 2](https://musicradar.com/reviews/sugar-bytes-effectrix2-review) · [Plugin Boutique: Effectrix 2](https://www.pluginboutique.com/products/11389-Effectrix-2)
- [Image-Line: Gross Beat time settings](https://www.image-line.com/support/FLHelp/html/plugins/GrossBeat_TimeSettings.htm) · [envelope panel](https://image-line.com/support/FLHelp/html/plugins/GrossBeat_EnvelopePanel.htm)
- [KVR: Kreuzberg Audio Uplifter](https://www.kvraudio.com/product/uplifter-by-kreuzberg-audio)
- Roundups: [Integraudio: best riser/drop plugins](https://integraudio.com/8-best-sweep-riser-plugins/) · [Audiofu: best risers](https://audiofu.com/best/risers)
