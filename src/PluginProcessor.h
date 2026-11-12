#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Model.h"
#include "DropRenderer.h"
#include "Presets.h"
#include "LiveEngine.h"
#include <optional>

/** Raw audio as it was loaded (at its own sample rate). */
struct SourceAudio
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    juce::String name;
};
using SourcePtr = std::shared_ptr<const SourceAudio>;

/** The part of the session that undo/redo and A/B compare work on. */
struct EditState
{
    std::vector<km::Move> moves;
    std::array<km::Move, km::numMoveTypes> templates;
    double zoneStart = 0.0, zoneEnd = 0.0;
    float sens808 = 0.55f, sensHats = 0.55f;
    km::Move tension;
    juce::String presetName;
    bool presetDirty = false;

    bool sameEdit (const EditState& o) const;   // ignores the preset name / dirty flag
};

/** UI / session settings that are saved with the project. */
struct SessionSettings
{
    juce::String presetName = "Big Drop Energy";
    bool presetDirty = true;
    km::PlaybackMode playbackMode = km::PlaybackMode::PreDrop;
    bool loop = true;
    bool previewReplaces = true;   // in-sync preview replaces the track audio (vs. layering)
    km::ExportRange exportRange = km::ExportRange::FullClip;
    km::ExportFormat exportFormat = km::ExportFormat::Wav24;
    float uiScale = 0.72f;
    int activeTab = 0;             // 0 music, 1 drums, 2 fx layer, 3 timeline
    int snap = 1;                  // 0 bar, 1 beat, 2 sixteenth
    int rollStyle = 0;             // ROLL style: index into km::rollStyleNames() (0 = any)
};

class KoolaidProcessor : public juce::AudioProcessor,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    KoolaidProcessor();
    ~KoolaidProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Koolaid Man"; }
    bool acceptsMidi() const override { return true; }   // LIVE PADS: notes C1-G1 hold the pads
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //========================================================================== model (message thread)
    km::DropModel& getModel() { return model; }
    SessionSettings& getSettings() { return settings; }

    /** Call after editing the model. Re-renders and marks the preset modified. */
    void modelChanged (bool markPresetDirty = true);

    /** LIVE PADS: hold / release a pad from the UI (drives the pad's automatable parameter,
        so a DAW in write mode records the performance). */
    void setLivePad (int pad, bool down);
    bool isLivePadHeld (int pad) const;
    float getLiveActivity (int pad) const { return live.getActivity (pad); }

    bool loadAudioFile (const juce::File& file, int slot, juce::String& error);

    /** Your own sample for a Riser / Reverse Crash / Impact / Sub Boom move in the stack
        (by uid). Stored with the project; presets remember the file path. */
    bool loadMoveSample (int uid, const juce::File& file, juce::String& error);
    void clearMoveSample (int uid);
    void clearSlot (int slot);
    bool hasMusic() const { return model.music != nullptr; }

    void applyPreset (const km::Preset&);
    void roll();

    km::ResultPtr getResult() const { return currentResult; }
    km::AnalysisPtr getAnalysis() const { return currentResult != nullptr ? currentResult->analysis : nullptr; }
    int getResultVersion() const { return currentResult != nullptr ? currentResult->version : 0; }
    bool isRendering() const { return renderPending.load(); }

    /** Renders synchronously and writes a file. Returns the file (or an empty File + error). */
    /** Renders and writes the export. Stems give two files (music + fx). */
    juce::Array<juce::File> exportAudio (juce::String& error);
    juce::String exportRangeLabel (km::ExportRange) const;
    juce::String exportLandingBar() const;

    //========================================================================== transport
    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing.load(); }
    void jumpToStart();
    void jumpToDrop();
    void seekTo (juce::int64 samplePos);
    juce::int64 getPlayheadSample() const { return playhead.load(); }
    bool isSyncPlayheadActive() const { return syncActive.load(); }

    bool getSyncPreview() const { return syncParam->get(); }
    void setSyncPreview (bool);

    //========================================================================== capture
    void armCapture (bool shouldArm);
    bool isCaptureArmed() const { return captureArmed.load(); }
    bool isCapturing() const { return capturing.load(); }

    //========================================================================== host info
    double getHostBpm() const { return hostBpm.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    std::function<void()> onModelReplaced;   // editor refresh hook (message thread)

    //========================================================================== undo / A-B
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    int getABSlot() const { return abActive; }
    void switchAB (int slot);      // 0 = A, 1 = B
    void copyABToOther();          // copy the current version into the other slot
    float getBuildAmount() const { return buildParam->get() / 100.0f; }
    float getTension() const { return tensionParam->get() / 100.0f; }
    void setTension (float v01, bool gestureStart, bool gestureEnd);

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void rebuildClips();
    EditState captureEdit() const;
    void restoreEdit (const EditState&);
    void recordUndo();
    void requestRender();
    void updatePlayRange();
    void finishCapture();
    km::ClipPtr makeClip (const SourcePtr&) const;
    void setDefaultZone();

    km::DropModel model;
    SessionSettings settings;
    SourcePtr musicSource, drumSource;
    std::map<juce::String, SourcePtr> sampleSources;   // your own samples, by Move::sampleId
    SourcePtr readAudioFile (const juce::File&, double maxSeconds, double minSeconds, juce::String& error);
    void restoreMissingSamples();                      // reload samples that presets point to on disk

    double engineRate = 48000.0;
    km::RenderThread renderThread;

    juce::CriticalSection resultLock;
    km::ResultPtr pendingResult;              // from the render thread
    km::ResultPtr currentResult;              // message thread view
    std::atomic<const km::RenderResult*> audioResult { nullptr };
    std::atomic<const km::RenderResult*> audioInUse { nullptr };   // hazard pointer: never freed while set
    std::vector<std::pair<km::ResultPtr, juce::uint32>> retired;
    std::atomic<bool> renderPending { false };
    int lastRequestId = 0;

    // Offline bounces (Export Audio Mixdown, Render in Place): no deadline, so the audio thread
    // waits for the render of your latest edit instead of printing the previous one. The newest
    // render is handed over straight from the render thread (not via the message thread, which
    // a host may keep busy while it exports).
    km::ResultPtr latestDelivered;            // guarded by resultLock
    juce::WaitableEvent resultReady;
    std::atomic<int> requestedId { 0 };
    km::ResultPtr offlineHold;                // used by the audio thread only while offline (offlineLock)
    juce::SpinLock offlineLock;
    int offlineGaveUpOn = -1;                 // a request we stopped waiting for: never stall twice

    // transport (shared with the audio thread)
    std::atomic<bool> playing { false }, syncActive { false };
    std::atomic<juce::int64> playhead { 0 }, rangeStart { 0 }, rangeEnd { 0 };
    std::atomic<bool> loopRange { true }, playOriginal { false }, replaceInput { true };
    std::atomic<int> clipStartBar { 1 };
    std::atomic<double> beatsPerBarAtomic { 4.0 }, clipSamplesPerBeat { 22153.8 };

    // host
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostNum { 4 }, hostDen { 4 };
    std::atomic<bool> hostPlaying { false }, hostHasTempo { false };

    // capture
    std::atomic<bool> captureArmed { false }, capturing { false }, captureDone { false };
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> captureWritten { 0 };
    std::atomic<int> captureStartBar { 1 };
    std::atomic<double> captureBpm { 120.0 };
    juce::SpinLock captureLock;

    // the audio stored in the project is FLAC-encoded once per source, not on every save
    juce::MemoryBlock encodedAudio (const SourcePtr&);
    juce::CriticalSection flacLock;
    std::vector<std::pair<SourcePtr, juce::MemoryBlock>> flacCache;
    std::atomic<bool> needsRebuild { false };

    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterBool* syncParam = nullptr;
    juce::AudioParameterFloat* buildParam = nullptr;
    juce::AudioParameterFloat* tensionParam = nullptr;
    float lastBuild = 1.0f, lastGain = 1.0f, lastTension = 1.0f;
    // LIVE PADS
    void processMain (juce::AudioBuffer<float>&);
    void processLive (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    km::LiveEngine live;
    std::array<juce::AudioParameterBool*, km::LiveEngine::numPads> liveParams {};
    float midiHeld[km::LiveEngine::numPads] {};
    double blockBpm = 120.0, blockPpq = 0.0, freePpq = 0.0;
    bool blockHasPpq = false, blockPlaying = false;
    double captureNextPpq = 0.0;

    std::vector<EditState> undoStack, redoStack;
    EditState committed;
    juce::uint32 lastEditTime = 0;
    bool restoring = false;
    EditState abSlots[2];
    bool abUsed[2] { false, false };
    int abActive = 0;
    juce::AudioFormatManager formatManager;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoolaidProcessor)
};
