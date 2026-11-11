#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DrumDetector.h"
#include "Mastering.h"
#include "KeyDetector.h"

using namespace km;

namespace
{
    int nextUid = 1;
    void assignUids (std::vector<Move>& moves)
    {
        for (auto& m : moves)
            if (m.uid == 0)
                m.uid = nextUid++;
    }

    juce::MemoryBlock encodeFlac (const SourceAudio& src)
    {
        juce::MemoryBlock block;
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (src.sampleRate)
                                                     .withNumChannels (src.audio.getNumChannels())
                                                     .withBitsPerSample (24);
        if (auto writer = flac.createWriterFor (stream, opts))
        {
            writer->writeFromAudioSampleBuffer (src.audio, 0, src.audio.getNumSamples());
            writer.reset();
        }
        return block;
    }

    std::shared_ptr<SourceAudio> decodeFlac (const juce::MemoryBlock& block, const juce::String& name)
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, false), true));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;
        auto s = std::make_shared<SourceAudio>();
        s->sampleRate = reader->sampleRate;
        s->name = name;
        s->audio.setSize (2, (int) reader->lengthInSamples);
        reader->read (&s->audio, 0, (int) reader->lengthInSamples, 0, true, true);
        if (reader->numChannels == 1)
            s->audio.copyFrom (1, 0, s->audio, 0, 0, s->audio.getNumSamples());
        return s;
    }
}

//==============================================================================
KoolaidProcessor::KoolaidProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (outputParam = new juce::AudioParameterFloat (juce::ParameterID { "previewLevel", 1 }, "Preview Level",
                                                               juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                               juce::AudioParameterFloatAttributes().withLabel ("dB")));
    addParameter (syncParam = new juce::AudioParameterBool (juce::ParameterID { "syncPreview", 1 }, "Sync To Session", true));
    addParameter (buildParam = new juce::AudioParameterFloat (juce::ParameterID { "buildAmount", 1 }, "Build Amount",
                                                              juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel ("%")));
    addParameter (tensionParam = new juce::AudioParameterFloat (juce::ParameterID { "tension", 1 }, "Tension",
                                                                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

    formatManager.registerBasicFormats();

    for (int t = 0; t < numMoveTypes; ++t)
        model.templates[(size_t) t] = defaultMove ((MoveType) t);
    model.moves = factoryPresets().front().moves;
    assignUids (model.moves);
    committed = captureEdit();

    renderThread.onResult = [this] (ResultPtr r)
    {
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (r);
        }
        triggerAsyncUpdate();
    };

    startTimerHz (15);
}

KoolaidProcessor::~KoolaidProcessor()
{
    stopTimer();
    renderThread.onResult = nullptr;
    cancelPendingUpdate();
}

bool KoolaidProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

juce::AudioProcessorEditor* KoolaidProcessor::createEditor()
{
    return new KoolaidEditor (*this);
}

//==============================================================================
void KoolaidProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate > 0 && std::abs (sampleRate - engineRate) > 0.5)
    {
        engineRate = sampleRate;
        // rebuild on the message thread (prepareToPlay can be called from elsewhere)
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            rebuildClips();
            modelChanged (false);
        }
        else
        {
            needsRebuild.store (true);
        }
    }
}

void KoolaidProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const double sr = getSampleRate() > 0 ? getSampleRate() : engineRate;

    // ---- host transport
    bool hostIsPlaying = false, hasPpq = false;
    double ppq = 0.0, bpm = hostBpm.load(), lastBarPpq = -1.0;
    int num = hostNum.load(), den = hostDen.load();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { bpm = *b; hostBpm.store (bpm); hostHasTempo.store (true); }
            if (auto ts = pos->getTimeSignature()) { num = ts->numerator; den = ts->denominator; hostNum.store (num); hostDen.store (den); }
            hostIsPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
            if (auto lb = pos->getPpqPositionOfLastBarStart()) lastBarPpq = *lb;
        }
    }
    hostPlaying.store (hostIsPlaying);

    // ---- capture (records the incoming audio, starting on the next bar line)
    if (captureArmed.load() && ! captureDone.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (captureLock);
        if (tl.isLocked() && captureBuffer.getNumSamples() > 0)
        {
            if (hostIsPlaying && hasPpq && bpm > 0)
            {
                int writeFrom = 0;
                if (! capturing.load())
                {
                    const double bpb = num * 4.0 / juce::jmax (1, den);
                    double nextBar;
                    if (lastBarPpq >= 0.0)
                        nextBar = (ppq - lastBarPpq) < 1.0e-6 ? lastBarPpq : lastBarPpq + bpb;
                    else
                        nextBar = std::ceil (ppq / bpb - 1.0e-9) * bpb;
                    const double samplesToBar = (nextBar - ppq) * 60.0 / bpm * sr;
                    if (samplesToBar < numSamples)
                    {
                        writeFrom = juce::jmax (0, (int) std::floor (samplesToBar));
                        capturing.store (true);
                        captureWritten.store (0);
                        captureStartBar.store ((int) std::round (nextBar / bpb) + 1);
                        captureBpm.store (bpm);
                    }
                    else
                    {
                        writeFrom = numSamples;
                    }
                }
                // the transport jumped (cycle loop, locate): end the capture at the jump
                const double beatsPerBlock = numSamples * bpm / (60.0 * sr);
                if (capturing.load() && writeFrom == 0 && std::abs (ppq - captureNextPpq) > 0.05 + 0.02 * beatsPerBlock)
                {
                    capturing.store (false);
                    captureDone.store (true);
                }
                captureNextPpq = ppq + beatsPerBlock;
                if (capturing.load())
                {
                    int written = captureWritten.load();
                    const int toWrite = juce::jmin (numSamples - writeFrom, captureBuffer.getNumSamples() - written);
                    if (toWrite > 0 && numIn > 0)
                        for (int ch = 0; ch < 2; ++ch)
                            captureBuffer.copyFrom (ch, written, buffer, juce::jmin (ch, numIn - 1), writeFrom, toWrite);
                    written += juce::jmax (0, toWrite);
                    captureWritten.store (written);
                    if (written >= captureBuffer.getNumSamples())
                    {
                        capturing.store (false);
                        captureDone.store (true);
                    }
                }
            }
            else if (capturing.load())
            {
                capturing.store (false);
                captureDone.store (true);
            }
        }
    }
    else if (capturing.load())
    {
        capturing.store (false);
        captureDone.store (true);
    }

    // ---- preview
    // Hazard pointer: publish which render we're reading, then re-check it's still current.
    // The message thread never frees a render while it's marked as in use.
    const RenderResult* r = audioResult.load();
    for (;;)
    {
        audioInUse.store (r);
        const auto* again = audioResult.load();
        if (again == r) break;
        r = again;
    }
    struct ReleaseOnExit { std::atomic<const RenderResult*>& p; ~ReleaseOnExit() { p.store (nullptr); } } release { audioInUse };

    // Preview Level: ramped across the block so automation doesn't zipper
    const float gainTarget = juce::Decibels::decibelsToGain (outputParam->get());
    const float gain0 = lastGain;
    lastGain = gainTarget;
    auto gainAt = [&] (int i) { return gain0 + (gainTarget - gain0) * (float) i / (float) juce::jmax (1, numSamples); };
    const bool haveAudio = r != nullptr && r->source != nullptr && r->length > 0;
    const bool syncNow = haveAudio && syncParam->get() && hostIsPlaying && hasPpq;
    syncActive.store (syncNow);

    if (! haveAudio)
        return;

    const bool original = playOriginal.load();
    const auto& orig = r->source->audio;
    const juce::int64 len = r->length, origLen = orig.getNumSamples();
    // Build Amount: blend from the untouched audio (0) to the full drop (1), smoothed per block
    const float amountTarget = original ? 0.0f : buildParam->get() / 100.0f;
    const float amount0 = lastBuild;
    lastBuild = amountTarget;
    // Tension: blend between the render without the linked moves (0) and the full render (1)
    const float tensionTarget = tensionParam->get() / 100.0f;
    const float tension0 = lastTension;
    lastTension = tensionTarget;
    auto sampleAt = [&] (int ch, juce::int64 p, int i) -> float
    {
        const float f = (float) i / (float) juce::jmax (1, numSamples);
        const float a = amount0 + (amountTarget - amount0) * f;
        const float tn = tension0 + (tensionTarget - tension0) * f;
        const float o = p < origLen ? orig.getSample (juce::jmin (ch, orig.getNumChannels() - 1), (int) p) : 0.0f;
        return o + (r->tensionAt (km::RenderResult::Layer::mix, ch, p, tn) - o) * a;
    };

    if (syncNow)
    {
        const double bpb = beatsPerBarAtomic.load();
        const double clipStartPpq = (clipStartBar.load() - 1) * bpb;
        const double spb = r->samplesPerBeat;
        const double startPos = (ppq - clipStartPpq) * spb;
        const bool replace = replaceInput.load();
        auto first = (juce::int64) std::floor (startPos);
        // 5 ms crossfades where the clip starts and ends, so switching between the track and
        // the drop never clicks
        const double xfade = juce::jmax (16.0, 0.005 * sr);
        for (int ch = 0; ch < numOut; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const juce::int64 p = first + i;
                if (p >= 0 && p < len)
                {
                    const float w = (float) juce::jmin (1.0, (double) juce::jmin (p + 1, len - p) / xfade);
                    out[i] = out[i] * (replace ? 1.0f - w : 1.0f) + sampleAt (ch, p, i) * gainAt (i) * w;
                }
            }
        }
        playhead.store (first + numSamples);
        return;
    }

    if (playing.load())
    {
        juce::int64 pos = playhead.load();
        const juce::int64 a = rangeStart.load(), b = juce::jmin (rangeEnd.load(), len);
        const bool loop = loopRange.load();
        bool stillPlaying = true;
        for (int i = 0; i < numSamples; ++i)
        {
            if (pos >= b || pos < a)
            {
                if (loop && b > a) pos = a;
                else { stillPlaying = false; break; }
            }
            for (int ch = 0; ch < numOut; ++ch)
                buffer.getWritePointer (ch)[i] += sampleAt (ch, pos, i) * gainAt (i);
            ++pos;
        }
        playhead.store (pos);
        if (! stillPlaying)
            playing.store (false);
    }
}

//==============================================================================
void KoolaidProcessor::handleAsyncUpdate()
{
    ResultPtr r;
    {
        const juce::ScopedLock sl (resultLock);
        r = std::move (pendingResult);
    }
    if (r == nullptr)
        return;
    if (currentResult != nullptr)
        retired.push_back ({ currentResult, juce::Time::getMillisecondCounter() });
    currentResult = r;
    audioResult.store (currentResult.get());
    renderPending.store (currentResult->requestId != lastRequestId);   // an older render may land first
    updatePlayRange();
}

void KoolaidProcessor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto* inUse = audioInUse.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now, inUse] (const auto& p) { return now - p.second > 500 && p.first.get() != inUse; }),
                   retired.end());

    if (needsRebuild.exchange (false))
    {
        rebuildClips();
        modelChanged (false);
    }

    if (model.tempoSync && hostHasTempo.load())
    {
        const double hb = hostBpm.load();
        const int hn = hostNum.load(), hd = hostDen.load();
        if (hb > 10.0 && (std::abs (hb - model.bpm) > 1.0e-3 || hn != model.tsNum || hd != model.tsDen))
        {
            model.bpm = hb;
            model.tsNum = hn;
            model.tsDen = hd;
            modelChanged (false);
        }
    }

    if (captureDone.load())
        finishCapture();
}

void KoolaidProcessor::requestRender()
{
    renderPending.store (true);
    renderThread.request (model, ++lastRequestId);
}

void KoolaidProcessor::modelChanged (bool markPresetDirty)
{
    if (markPresetDirty)
        settings.presetDirty = true;
    if (! restoring)
        recordUndo();

    assignUids (model.moves);
    const double clipBeats = model.clipLengthBeats();
    if (clipBeats > 0)
    {
        model.zoneEnd = juce::jlimit (0.25, clipBeats + 1.0e-3, model.zoneEnd);
        model.zoneStart = juce::jlimit (0.0, model.zoneEnd - 0.25, model.zoneStart);
    }
    updatePlayRange();
    requestRender();
}

void KoolaidProcessor::updatePlayRange()
{
    const double sr = model.music != nullptr ? model.music->sampleRate : engineRate;
    const double spb = model.samplesPerBeat (sr);
    juce::int64 len = model.music != nullptr ? model.music->audio.getNumSamples() : 0;
    if (currentResult != nullptr && currentResult->source == model.music)
        len = juce::jmax<juce::int64> (len, currentResult->length);   // includes an after-drop tail
    const auto zs = (juce::int64) (model.zoneStart * spb), ze = (juce::int64) (model.zoneEnd * spb);
    const auto bar = (juce::int64) (model.beatsPerBar() * spb);

    juce::int64 a = 0, b = len;
    switch (settings.playbackMode)
    {
        case PlaybackMode::PreDrop:  a = zs; b = ze; break;
        case PlaybackMode::FullClip: a = 0; b = len; break;
        case PlaybackMode::IntoDrop: a = juce::jmax<juce::int64> (0, zs - bar); b = juce::jmin (len, ze + 2 * bar); break;
        case PlaybackMode::Original: a = zs; b = ze; break;
    }
    if (b <= a) { a = 0; b = len; }
    rangeStart.store (a);
    rangeEnd.store (b);
    loopRange.store (settings.loop);
    playOriginal.store (settings.playbackMode == PlaybackMode::Original);
    replaceInput.store (settings.previewReplaces);
    clipStartBar.store (model.clipStartBar);
    beatsPerBarAtomic.store (model.beatsPerBar());
    clipSamplesPerBeat.store (spb);
}

//==============================================================================
void KoolaidProcessor::setPlaying (bool shouldPlay)
{
    if (shouldPlay)
    {
        const auto p = playhead.load();
        if (p < rangeStart.load() || p >= rangeEnd.load())
            playhead.store (rangeStart.load());
    }
    playing.store (shouldPlay);
}

void KoolaidProcessor::jumpToStart() { playhead.store (rangeStart.load()); }

void KoolaidProcessor::jumpToDrop()
{
    const double spb = clipSamplesPerBeat.load();
    const auto target = (juce::int64) ((model.zoneEnd - model.beatsPerBar()) * spb);
    playhead.store (juce::jlimit (rangeStart.load(), juce::jmax (rangeStart.load(), rangeEnd.load() - 1), target));
}

void KoolaidProcessor::seekTo (juce::int64 samplePos)
{
    playhead.store (juce::jmax<juce::int64> (0, samplePos));
}

void KoolaidProcessor::setSyncPreview (bool b)
{
    syncParam->setValueNotifyingHost (b ? 1.0f : 0.0f);
}

//==============================================================================
void KoolaidProcessor::armCapture (bool shouldArm)
{
    if (shouldArm)
    {
        if (captureArmed.load())
            return;
        {
            const juce::SpinLock::ScopedLockType sl (captureLock);
            captureBuffer.setSize (2, (int) (engineRate * 240.0));
            captureBuffer.clear();
        }
        captureWritten.store (0);
        captureDone.store (false);
        capturing.store (false);
        captureArmed.store (true);
    }
    else
    {
        captureArmed.store (false);
        if (! capturing.load() && ! captureDone.load())
        {
            const juce::SpinLock::ScopedLockType sl (captureLock);
            captureBuffer.setSize (0, 0);
        }
    }
}

void KoolaidProcessor::finishCapture()
{
    captureArmed.store (false);
    std::shared_ptr<SourceAudio> s;
    {
        const juce::SpinLock::ScopedLockType sl (captureLock);
        int n = captureWritten.load();
        const double bpm = captureBpm.load();
        const double bpb = hostNum.load() * 4.0 / juce::jmax (1, hostDen.load());
        const double barSamples = engineRate * 60.0 / bpm * bpb;
        if (n > (int) barSamples)
            n = (int) (std::floor (n / barSamples) * barSamples);
        if (n > (int) (engineRate * 0.5))
        {
            s = std::make_shared<SourceAudio>();
            s->sampleRate = engineRate;
            s->name = "Captured from bar " + juce::String (captureStartBar.load());
            s->audio.setSize (2, n);
            for (int ch = 0; ch < 2; ++ch)
                s->audio.copyFrom (ch, 0, captureBuffer, ch, 0, n);
        }
        captureBuffer.setSize (0, 0);
        captureWritten.store (0);
        capturing.store (false);
    }
    captureDone.store (false);

    if (s != nullptr)
    {
        if (model.tempoSync)
        {
            model.bpm = captureBpm.load();
            model.tsNum = hostNum.load();
            model.tsDen = hostDen.load();
        }
        model.clipStartBar = juce::jmax (1, captureStartBar.load());
        if (settings.activeTab == 1)
        {
            drumSource = s;
        }
        else
        {
            musicSource = s;
        }
        rebuildClips();
        if (settings.activeTab != 1)
            setDefaultZone();
        modelChanged (false);
        if (onModelReplaced) onModelReplaced();
    }
}

//==============================================================================
ClipPtr KoolaidProcessor::makeClip (const SourcePtr& src) const
{
    if (src == nullptr)
        return nullptr;
    auto clip = std::make_shared<ClipData>();
    clip->name = src->name;
    clip->sampleRate = engineRate;
    if (std::abs (src->sampleRate - engineRate) < 0.5)
    {
        clip->audio.makeCopyOf (src->audio);
        return clip;
    }
    const double ratio = src->sampleRate / engineRate;
    const int inN = src->audio.getNumSamples();
    const int outN = (int) std::floor (inN / ratio);
    clip->audio.setSize (2, outN);
    std::vector<float> padded ((size_t) inN + 512, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        std::copy (src->audio.getReadPointer (juce::jmin (ch, src->audio.getNumChannels() - 1)),
                   src->audio.getReadPointer (juce::jmin (ch, src->audio.getNumChannels() - 1)) + inN, padded.begin());
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, padded.data(), clip->audio.getWritePointer (ch), outN);
    }
    return clip;
}

void KoolaidProcessor::rebuildClips()
{
    model.music = makeClip (musicSource);
    model.drums = makeClip (drumSource);
}

void KoolaidProcessor::setDefaultZone()
{
    const double clipBeats = model.clipLengthBeats();
    const double bpb = model.beatsPerBar();
    double end = std::floor (clipBeats / bpb + 1.0e-3) * bpb;
    if (end <= 0) end = clipBeats;
    model.zoneEnd = end;
    model.zoneStart = juce::jmax (0.0, end - juce::jmin (4.0 * bpb, end));
    if (end - model.zoneStart < 0.25) model.zoneStart = 0.0;
}

bool KoolaidProcessor::loadAudioFile (const juce::File& file, int slot, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Couldn't read " + file.getFileName() + " (supported: WAV, AIFF, FLAC, MP3, OGG).";
        return false;
    }
    const auto maxLen = (juce::int64) (reader->sampleRate * 600.0);
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    if (len < (int) (reader->sampleRate * 0.25))
    {
        error = "That file is too short.";
        return false;
    }
    auto s = std::make_shared<SourceAudio>();
    s->sampleRate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->audio.setSize (2, len);
    s->audio.clear();
    reader->read (&s->audio, 0, len, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        s->audio.copyFrom (1, 0, s->audio, 0, 0, len);

    if (slot == 1)
    {
        drumSource = s;
        model.drums = makeClip (drumSource);
    }
    else
    {
        musicSource = s;
        model.music = makeClip (musicSource);
        setDefaultZone();
        playhead.store (0);
    }
    modelChanged (false);
    return true;
}

void KoolaidProcessor::clearSlot (int slot)
{
    if (slot == 1) { drumSource = nullptr; model.drums = nullptr; }
    else           { musicSource = nullptr; model.music = nullptr; playing.store (false); }
    modelChanged (false);
}

void KoolaidProcessor::applyPreset (const Preset& p)
{
    model.moves = p.moves;
    for (auto& m : model.moves) m.uid = 0;
    settings.presetName = p.name;
    settings.presetDirty = false;
    modelChanged (false);
}

void KoolaidProcessor::roll()
{
    const auto a = getAnalysis();
    const bool drums = a != nullptr && (a->snare.found || a->hats.found);
    // locked moves survive the roll; the roll fills in around them
    std::vector<Move> kept;
    for (const auto& m : model.moves)
        if (m.locked) kept.push_back (m);

    auto fresh = rollMoves (rng, drums, settings.rollStyle);
    for (auto& m : fresh)
    {
        const bool taken = std::any_of (kept.begin(), kept.end(), [&] (const Move& k) { return k.type == m.type; });
        if (! taken) kept.push_back (m);
    }
    std::stable_sort (kept.begin(), kept.end(), [] (const Move& x, const Move& y)
                      { return canonicalOrder (x.type) < canonicalOrder (y.type); });
    model.moves = std::move (kept);

    const auto style = rollStyleNames()[juce::jlimit (0, rollStyleNames().size() - 1, settings.rollStyle)];
    settings.presetName = (settings.rollStyle > 0 ? style + " Roll #" : juce::String ("Roll #"))
                        + juce::String (1000 + rng.nextInt (9000));
    modelChanged (true);
}

//==============================================================================
juce::String KoolaidProcessor::exportRangeLabel (ExportRange r) const
{
    const double bpb = model.beatsPerBar();
    // Impact / Sub Boom can add a tail after the clip
    double clip = model.clipLengthBeats();
    if (currentResult != nullptr && currentResult->source == model.music && currentResult->samplesPerBeat > 0)
        clip = juce::jmax (clip, currentResult->length / currentResult->samplesPerBeat);
    switch (r)
    {
        case ExportRange::FullClip:  return "FULL CLIP (" + formatLength (clip, bpb) + ")";
        case ExportRange::BuildZone: return "BUILD ZONE (" + formatLength (model.zoneEnd - model.zoneStart, bpb) + ")";
        case ExportRange::ZoneToEnd: return "ZONE + DROP (" + formatLength (clip - model.zoneStart, bpb) + ")";
        case ExportRange::FxLayer:   return "FX LAYER (" + formatLength (clip, bpb) + ")";
        case ExportRange::Stems:     return "STEMS: MUSIC + FX (" + formatLength (clip, bpb) + ")";
    }
    return {};
}

juce::String KoolaidProcessor::exportLandingBar() const
{
    const bool fromZone = settings.exportRange == ExportRange::BuildZone || settings.exportRange == ExportRange::ZoneToEnd;
    return formatBarPosition (fromZone ? model.zoneStart : 0.0, model);
}

juce::Array<juce::File> KoolaidProcessor::exportAudio (juce::String& error)
{
    if (model.music == nullptr)
    {
        error = "Drag in some audio first.";
        return {};
    }
    auto analysis = renderThread.getLatestAnalysis();
    if (analysis == nullptr)
    {
        const auto& srcClip = model.drums != nullptr ? model.drums : model.music;
        auto a = std::make_shared<DrumAnalysis> (analyseDrums (srcClip->audio, srcClip->sampleRate, model.samplesPerBeat (srcClip->sampleRate),
                                                               model.beatsPerBar(), model.sensSnare, model.sens808, model.sensHats));
        a->key = detectKey (model.music->audio, model.music->sampleRate);
        analysis = a;
    }
    // Reuse the render already on screen when it's up to date (no second render, no wait)
    km::ResultPtr r = (! renderPending.load() && currentResult != nullptr && currentResult->requestId == lastRequestId
                       && currentResult->source == model.music)
                        ? currentResult : renderDrop (model, analysis, 0);
    const juce::int64 total = r->length;

    int a = 0, b = (int) total;
    switch (settings.exportRange)
    {
        case ExportRange::BuildZone: a = (int) r->zoneStart; b = (int) r->zoneEnd; break;
        case ExportRange::ZoneToEnd: a = (int) r->zoneStart; break;
        default: break;
    }
    if (b - a < 16)
    {
        error = "Nothing to export.";
        return {};
    }

    // Build Amount: blend each output from the untouched audio towards the full drop
    const float amount = buildParam->get() / 100.0f;
    using Layer = km::RenderResult::Layer;
    auto section = [&] (Layer layer)
    {
        auto out = r->copyOf (layer, a, b, tensionParam->get() / 100.0f);
        if (amount < 0.999f)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const juce::int64 p = a + i;
                    const float o = (layer != Layer::fx && p < r->source->audio.getNumSamples())
                                      ? r->source->audio.getSample (juce::jmin (ch, r->source->audio.getNumChannels() - 1), (int) p) : 0.0f;
                    out.setSample (ch, i, o + (out.getSample (ch, i) - o) * amount);
                }
        return out;
    };

    std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> outputs;
    switch (settings.exportRange)
    {
        case ExportRange::FxLayer: outputs.push_back ({ "FX", section (Layer::fx) }); break;
        case ExportRange::Stems:
            outputs.push_back ({ "Stem Music", section (Layer::music) });
            outputs.push_back ({ "Stem FX", section (Layer::fx) });
            break;
        case ExportRange::FullClip:  outputs.push_back ({ "Full", section (Layer::mix) }); break;
        case ExportRange::BuildZone: outputs.push_back ({ "Zone", section (Layer::mix) }); break;
        case ExportRange::ZoneToEnd: outputs.push_back ({ "Drop", section (Layer::mix) }); break;
    }

    const bool aiff = settings.exportFormat == ExportFormat::Aiff24;
    const int bits = settings.exportFormat == ExportFormat::Wav16 ? 16 : (settings.exportFormat == ExportFormat::Wav32f ? 32 : 24);
    juce::Array<juce::File> files;
    for (auto& [label, buffer] : outputs)
    {
        const juce::String base = "Koolaid Man - " + settings.presetName + " - " + label + " - "
                                + juce::String (model.bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "bpm - bar "
                                + exportLandingBar().upToFirstOccurrenceOf (".", false, false);
        auto file = exportFolder().getNonexistentChildFile (juce::File::createLegalFileName (base), aiff ? ".aif" : ".wav", false);

        std::unique_ptr<juce::AudioFormat> format;
        if (aiff) format = std::make_unique<juce::AiffAudioFormat>();
        else      format = std::make_unique<juce::WavAudioFormat>();

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        if (stream == nullptr)
        {
            error = "Couldn't write to " + file.getFullPathName();
            return files;
        }
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate).withNumChannels (2).withBitsPerSample (bits)
                        .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                      : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = format->createWriterFor (stream, opts);
        if (writer == nullptr)
        {
            error = "Couldn't create the audio file.";
            file.deleteFile();
            return files;
        }
        if (bits == 16)
            km::ditherTo16Bit (buffer);   // TPDF dither with noise shaping instead of plain truncation
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        writer.reset();
        files.add (file);
    }
    return files;
}

//==============================================================================
void KoolaidProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree t ("KOOLAID");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("bpm", model.bpm, nullptr);
    t.setProperty ("tempoSync", model.tempoSync, nullptr);
    t.setProperty ("tsNum", model.tsNum, nullptr);
    t.setProperty ("tsDen", model.tsDen, nullptr);
    t.setProperty ("clipStartBar", model.clipStartBar, nullptr);
    t.setProperty ("zoneStart", model.zoneStart, nullptr);
    t.setProperty ("zoneEnd", model.zoneEnd, nullptr);
    t.setProperty ("sensSnare", model.sensSnare, nullptr);
    t.setProperty ("sens808", model.sens808, nullptr);
    t.setProperty ("sensHats", model.sensHats, nullptr);
    t.setProperty ("keyMode", model.keyMode, nullptr);

    t.setProperty ("presetName", settings.presetName, nullptr);
    t.setProperty ("presetDirty", settings.presetDirty, nullptr);
    t.setProperty ("playbackMode", (int) settings.playbackMode, nullptr);
    t.setProperty ("loop", settings.loop, nullptr);
    t.setProperty ("previewReplaces", settings.previewReplaces, nullptr);
    t.setProperty ("exportRange", (int) settings.exportRange, nullptr);
    t.setProperty ("exportFormat", (int) settings.exportFormat, nullptr);
    t.setProperty ("uiScale", settings.uiScale, nullptr);
    t.setProperty ("activeTab", settings.activeTab, nullptr);
    t.setProperty ("snap", settings.snap, nullptr);
    t.setProperty ("rollStyle", settings.rollStyle, nullptr);
    t.setProperty ("previewLevel", outputParam->get(), nullptr);
    t.setProperty ("syncPreview", syncParam->get(), nullptr);
    t.setProperty ("buildAmount", buildParam->get(), nullptr);
    t.setProperty ("tensionAmount", tensionParam->get(), nullptr);
    {
        auto tt = model.tension.toTree();
        juce::ValueTree tn ("TENSION");
        tn.appendChild (tt, nullptr);
        t.appendChild (tn, nullptr);
    }
    t.setProperty ("abActive", abActive, nullptr);
    const int other = 1 - abActive;
    if (abUsed[other])
    {
        const auto& e = abSlots[other];
        juce::ValueTree ab ("ABSLOT");
        ab.setProperty ("zoneStart", e.zoneStart, nullptr);
        ab.setProperty ("zoneEnd", e.zoneEnd, nullptr);
        ab.setProperty ("sens808", e.sens808, nullptr);
        ab.setProperty ("sensHats", e.sensHats, nullptr);
        ab.setProperty ("presetName", e.presetName, nullptr);
        ab.setProperty ("presetDirty", e.presetDirty, nullptr);
        ab.appendChild (movesToTree (e.moves), nullptr);
        t.appendChild (ab, nullptr);
    }

    t.appendChild (movesToTree (model.moves), nullptr);
    juce::ValueTree tmpl ("TEMPLATES");
    for (auto& m : model.templates)
        tmpl.appendChild (m.toTree(), nullptr);
    t.appendChild (tmpl, nullptr);

    for (auto [key, src] : { std::pair<const char*, SourcePtr> { "music", musicSource }, { "drums", drumSource } })
    {
        if (src == nullptr) continue;
        juce::ValueTree a ("AUDIO");
        a.setProperty ("slot", key, nullptr);
        a.setProperty ("name", src->name, nullptr);
        a.setProperty ("flac", encodedAudio (src), nullptr);
        t.appendChild (a, nullptr);
    }

    juce::MemoryOutputStream mos (destData, false);
    t.writeToStream (mos);
}

void KoolaidProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! t.hasType ("KOOLAID"))
        return;

    playing.store (false);
    model.bpm = t.getProperty ("bpm", model.bpm);
    model.tempoSync = t.getProperty ("tempoSync", model.tempoSync);
    model.tsNum = t.getProperty ("tsNum", model.tsNum);
    model.tsDen = t.getProperty ("tsDen", model.tsDen);
    model.clipStartBar = t.getProperty ("clipStartBar", model.clipStartBar);
    model.zoneStart = t.getProperty ("zoneStart", model.zoneStart);
    model.zoneEnd = t.getProperty ("zoneEnd", model.zoneEnd);
    model.sensSnare = t.getProperty ("sensSnare", model.sensSnare);
    model.sens808 = t.getProperty ("sens808", model.sens808);
    model.sensHats = t.getProperty ("sensHats", model.sensHats);
    model.keyMode = juce::jlimit (-2, 23, (int) t.getProperty ("keyMode", -1));

    settings.presetName = t.getProperty ("presetName", settings.presetName);
    settings.presetDirty = t.getProperty ("presetDirty", settings.presetDirty);
    settings.playbackMode = (PlaybackMode) juce::jlimit (0, 3, (int) t.getProperty ("playbackMode", 0));
    settings.loop = t.getProperty ("loop", settings.loop);
    settings.previewReplaces = t.getProperty ("previewReplaces", settings.previewReplaces);
    settings.exportRange = (ExportRange) juce::jlimit (0, 4, (int) t.getProperty ("exportRange", 0));
    settings.exportFormat = (ExportFormat) juce::jlimit (0, 3, (int) t.getProperty ("exportFormat", 1));
    settings.uiScale = juce::jlimit (0.4f, 1.5f, (float) t.getProperty ("uiScale", settings.uiScale));
    settings.activeTab = juce::jlimit (0, 3, (int) t.getProperty ("activeTab", 0));
    settings.snap = juce::jlimit (0, 2, (int) t.getProperty ("snap", 1));
    settings.rollStyle = juce::jlimit (0, rollStyleNames().size() - 1, (int) t.getProperty ("rollStyle", 0));
    *outputParam = (float) t.getProperty ("previewLevel", 0.0f);
    *syncParam = (bool) t.getProperty ("syncPreview", true);
    *buildParam = (float) t.getProperty ("buildAmount", 100.0f);
    *tensionParam = (float) t.getProperty ("tensionAmount", 100.0f);
    if (auto tn = t.getChildWithName ("TENSION"); tn.isValid() && tn.getNumChildren() > 0)
        model.tension = Move::fromTree (tn.getChild (0));

    model.moves = movesFromTree (t.getChildWithName ("MOVES"));
    if (auto tmpl = t.getChildWithName ("TEMPLATES"); tmpl.isValid())
        for (auto c : tmpl)
        {
            auto m = Move::fromTree (c);
            model.templates[(size_t) m.type] = m;
        }

    musicSource = nullptr;
    drumSource = nullptr;
    for (auto c : t)
    {
        if (! c.hasType ("AUDIO")) continue;
        if (auto* mb = c.getProperty ("flac").getBinaryData())
        {
            auto s = decodeFlac (*mb, c.getProperty ("name").toString());
            if (s != nullptr)
            {
                // remember the FLAC we just read, so the next save doesn't encode it again
                const juce::ScopedLock sl (flacLock);
                flacCache.push_back ({ s, *mb });
            }
            if (c.getProperty ("slot").toString() == "drums") drumSource = s;
            else musicSource = s;
        }
    }
    // A/B: the active version is the model; the other one is stored separately
    abActive = juce::jlimit (0, 1, (int) t.getProperty ("abActive", 0));
    abUsed[0] = abUsed[1] = false;
    if (auto ab = t.getChildWithName ("ABSLOT"); ab.isValid())
    {
        auto& e = abSlots[1 - abActive];
        e.moves = movesFromTree (ab.getChildWithName ("MOVES"));
        e.templates = model.templates;
        e.zoneStart = ab.getProperty ("zoneStart", model.zoneStart);
        e.zoneEnd = ab.getProperty ("zoneEnd", model.zoneEnd);
        e.sens808 = ab.getProperty ("sens808", model.sens808);
        e.sensHats = ab.getProperty ("sensHats", model.sensHats);
        e.presetName = ab.getProperty ("presetName", settings.presetName);
        e.presetDirty = ab.getProperty ("presetDirty", true);
        abUsed[1 - abActive] = true;
    }

    rebuildClips();
    restoring = true;
    modelChanged (false);
    restoring = false;
    undoStack.clear();
    redoStack.clear();
    committed = captureEdit();
    lastEditTime = 0;
    if (onModelReplaced) onModelReplaced();
}

//==============================================================================
void KoolaidProcessor::setTension (float v, bool gestureStart, bool gestureEnd)
{
    if (gestureStart) tensionParam->beginChangeGesture();
    tensionParam->setValueNotifyingHost (tensionParam->convertTo0to1 (juce::jlimit (0.0f, 1.0f, v) * 100.0f));
    if (gestureEnd) tensionParam->endChangeGesture();
}

juce::MemoryBlock KoolaidProcessor::encodedAudio (const SourcePtr& src)
{
    const juce::ScopedLock sl (flacLock);
    // forget sources that are no longer loaded
    flacCache.erase (std::remove_if (flacCache.begin(), flacCache.end(),
                                     [this, &src] (const auto& e) { return e.first != src && e.first != musicSource && e.first != drumSource; }),
                     flacCache.end());
    for (auto& e : flacCache)
        if (e.first == src)
            return e.second;
    flacCache.push_back ({ src, encodeFlac (*src) });
    return flacCache.back().second;
}

//==============================================================================
bool EditState::sameEdit (const EditState& o) const
{
    if (moves.size() != o.moves.size() || zoneStart != o.zoneStart || zoneEnd != o.zoneEnd
        || sens808 != o.sens808 || sensHats != o.sensHats || ! (tension == o.tension))
        return false;
    for (size_t i = 0; i < moves.size(); ++i)
        if (! (moves[i] == o.moves[i])) return false;
    for (size_t i = 0; i < templates.size(); ++i)
        if (! (templates[i] == o.templates[i])) return false;
    return true;
}

EditState KoolaidProcessor::captureEdit() const
{
    EditState e;
    e.moves = model.moves;
    e.templates = model.templates;
    e.zoneStart = model.zoneStart;
    e.zoneEnd = model.zoneEnd;
    e.sens808 = model.sens808;
    e.sensHats = model.sensHats;
    e.tension = model.tension;
    e.presetName = settings.presetName;
    e.presetDirty = settings.presetDirty;
    return e;
}

void KoolaidProcessor::restoreEdit (const EditState& e)
{
    restoring = true;
    model.moves = e.moves;
    model.templates = e.templates;
    model.zoneStart = e.zoneStart;
    model.zoneEnd = e.zoneEnd;
    model.sens808 = e.sens808;
    model.sensHats = e.sensHats;
    model.tension = e.tension;
    settings.presetName = e.presetName;
    settings.presetDirty = e.presetDirty;
    modelChanged (false);
    committed = captureEdit();
    lastEditTime = 0;
    restoring = false;
}

void KoolaidProcessor::recordUndo()
{
    // One undo step per gesture: changes within 600 ms of each other are merged.
    const auto now = captureEdit();
    if (now.sameEdit (committed))
    {
        committed = now;   // e.g. only the preset name changed
        return;
    }
    const auto t = juce::Time::getMillisecondCounter();
    if (lastEditTime == 0 || t - lastEditTime > 600)
    {
        undoStack.push_back (committed);
        if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    committed = now;
    lastEditTime = t;
}

void KoolaidProcessor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (captureEdit());
    const auto e = undoStack.back();
    undoStack.pop_back();
    restoreEdit (e);
}

void KoolaidProcessor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (captureEdit());
    const auto e = redoStack.back();
    redoStack.pop_back();
    restoreEdit (e);
}

void KoolaidProcessor::switchAB (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive) return;
    abSlots[abActive] = captureEdit();
    abUsed[abActive] = true;
    if (! abUsed[slot])
    {
        abSlots[slot] = abSlots[abActive];   // first visit: start from a copy
        abUsed[slot] = true;
    }
    abActive = slot;
    restoreEdit (abSlots[slot]);
}

void KoolaidProcessor::copyABToOther()
{
    abSlots[1 - abActive] = captureEdit();
    abUsed[1 - abActive] = true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KoolaidProcessor();
}
