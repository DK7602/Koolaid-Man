#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

namespace km
{

/** LIVE PADS: real-time drop gestures played on top of the plug-in's output while the DAW
    plays. A pad is "held" from the on-screen pads, a MIDI note (C1 = note 36 upwards) or host
    automation. Everything is tempo-synced to the host.

    Real-time safe: all memory is allocated in prepare(). With no pad held and no echo /
    reverb tail ringing, the audio passes through bit-for-bit untouched. */
class LiveEngine
{
public:
    enum Pad { stutter = 0, roll, filter, riser, tapeStop, echo, wash, cut, numPads };
    static constexpr int firstNote = 36;   // C1 (in the C3 = 60 convention Cubase uses)

    static const char* padName (int pad);
    static const char* padHint (int pad);
    static juce::String noteName (int pad);   // "C1", "C#1", ...

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Processes [start, start + num) of 'io' in place. held[p] is the pad's intensity
        (0 = not held, up to 1 = full). bpm and ppq describe the segment's first sample. */
    void process (juce::AudioBuffer<float>& io, int start, int num, const float* held, double bpm, double ppq);

    /** 0..1 per pad, for the lights on the pads. */
    float getActivity (int pad) const { return activity[(size_t) pad].load (std::memory_order_relaxed); }
    bool isIdle() const { return idle; }

private:
    void press (int pad, float intensity, double bpm, double ppq);
    float histAt (int ch, double pos) const;

    double sr = 48000.0;
    bool prepared = false, idle = true;

    // everything that came in, so stutters and the tape stop can play from the past
    std::vector<float> hist[2];
    int histSize = 0;
    juce::int64 written = 0;

    bool on[numPads] {};
    float level[numPads] {};
    juce::int64 pressedAt[numPads] {};
    std::array<std::atomic<float>, numPads> activity {};

    // stutter / roll
    juce::int64 sliceStart = 0;
    int sliceLen = 1, slice0 = 1, stutPos = 0;
    float stutEnv = 0.0f;

    // tape stop
    double tapeRead = 0.0, tapeDur = 1.0;
    float tapeEnv = 0.0f, tapeLp[2] {};

    // filter sweep / riser
    struct Svf
    {
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void set (double sampleRate, double hz, double res);
        void process (float x, float& lp, float& bp, float& hp);
        void clear() { ic1 = ic2 = 0.0f; }
    };
    Svf lp[2], hp[2], noiseBp[2];
    float filterProg = 0.0f, riserProg = 0.0f, inPow = 0.0f;
    juce::Random noise;

    // echo throw (dotted 1/8 ping-pong)
    std::vector<float> dl, dr;
    int dSize = 0, dW = 0;
    int echoSendLeft = 0, echoSendLen = 1;
    float echoHold = 0.0f, echoTone[4] {}, echoLevel = 0.9f, echoFb = 0.6f;

    // reverb wash (8-line FDN)
    std::vector<float> fdn[8];
    int fdnLen[8] {}, fdnPos[8] {};
    float fdnGain[8] {}, fdnLp[8] {};
    float washSend = 0.0f, washDuck = 0.0f;

    // cut
    float cutEnv = 0.0f;

    float tailLevel = 0.0f;   // how loud the echo / reverb tails still are
    int quietSamples = 0;
    bool tailsLive = false;
};

} // namespace km
