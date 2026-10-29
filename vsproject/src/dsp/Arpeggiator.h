#pragma once
// Arpeggiator (Synth1 CArpeggiator): types updown/up/down/random, 1..4 octave
// range, 19 tempo-synced beat values, gate length. Synced to the host's
// musical position when the transport runs, free-running at host tempo otherwise.

#include "dsp/DspCommon.h"

#include <vector>

namespace cs {

struct ArpEvent { int offset; int note; float velocity; bool on; };

class Arpeggiator {
public:
    void setSampleRate(double fs) { mFs = fs; }
    void setOn(bool on);
    void setType(int t)  { mType = t; rebuild(); }       // 1 updown, 2 up, 3 down, 4 random
    void setRange(int r) { mRange = r; rebuild(); }      // 0..3 -> 1..4 octaves
    void setBeat(int b)  { mBeat = clampv(b, 0, 18); }
    void setGate(int g)  { mGate = g; }
    bool on() const      { return mOn; }

    void noteOn(int note, float vel);
    void noteOff(int note);
    void allNotesOff();
    void setSustain(bool down);

    // Generates note events for the next n samples into out (appended).
    void process(int n, double bpm, double ppqStart, bool transportPlaying, std::vector<ArpEvent>& out);

private:
    void rebuild();
    void step(int offset, std::vector<ArpEvent>& out);
    void releaseCurrent(int offset, std::vector<ArpEvent>& out);

    struct Held { int note; float vel; };
    double mFs = 44100.0;
    bool   mOn = false, mSustain = false;
    int    mType = 2, mRange = 0, mBeat = 14, mGate = 100;

    std::vector<Held> mHeld, mSustained;
    std::vector<Held> mPattern;
    int    mPos = -1, mDir = 1;
    int    mPlaying = -1;            // note currently sounding
    double mSamplesToNext = 0.0, mSamplesToOff = -1.0;
    double mLastStepIndex = -1.0;
    Rng    mRng;
};

} // namespace cs
