#pragma once
// Global LFO (Synth1 CLFO, two instances at CSynthesizer1+0xab70/+0xacd0).
//
// Wave index (FUN_180014d10): 0 saw (falls from max to min), 1 triangle,
// 2 square, 3 sample & hold, 4 smoothed random, 5 sine.
// Speed: free = 1500^x/12 Hz, tempo = 30-step table in 24ths of a quarter.
// Output is a value between the destination's min..max (see LfoRange).

#include "dsp/DspCommon.h"

namespace cs {

enum LfoWave { kLfoSaw = 0, kLfoTri = 1, kLfoSquare = 2, kLfoSH = 3, kLfoSmooth = 4, kLfoSine = 5 };
enum LfoDest { kDestOsc2 = 1, kDestOsc12 = 2, kDestFilter = 3, kDestAmp = 4, kDestPW = 5, kDestFM = 6, kDestPan = 7 };

class Lfo {
public:
    void setSampleRate(double fs) { mFs = fs; }
    void setOn(bool on)        { mOn = on; }
    void setWave(int w)        { mWave = w; }
    void setDest(int d)        { mDest = d; }
    void setSpeed(int v)       { mSpeed = v; }
    void setDepth(int v)       { mDepth = v; }
    void setTempoSync(bool s)  { mTempo = s; }
    void setKeySync(bool k)    { mKeySync = k; }

    bool on() const    { return mOn && mDepth > 0; }
    int  dest() const  { return mDest; }
    bool keySync() const { return mKeySync; }

    void reset() { mPhase = 0.0; newRandom(); newRandom(); }
    void keyTrigger() { if (mKeySync) reset(); }

    // Advances by n samples and returns the bipolar wave value (-1..1).
    float advance(int n, double bpm);

    // Depth scaled into destination units:
    //   osc pitch (semitones), filter (cutoff index 0..1024), amp (0..1 reduction),
    //   p/w (duty offset), FM (amount), pan (-1..1)
    float modulation(float wave) const;

private:
    void newRandom() { mPrevRand = mRand; mRand = mRng.bipolar(); }

    double mFs = 44100.0, mPhase = 0.0;
    int    mWave = kLfoTri, mDest = kDestOsc2, mSpeed = 64, mDepth = 0;
    bool   mOn = false, mTempo = false, mKeySync = false;
    float  mRand = 0.f, mPrevRand = 0.f;
    Rng    mRng;
};

} // namespace cs
