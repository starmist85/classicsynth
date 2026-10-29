#pragma once
// Master effect chain, in the order wired by CSynthesizer1's constructor:
//   voices -> mixer -> Effect -> EQ (peak) -> Tone (LP/HP) -> Pan -> Tempo Delay -> Chorus -> out

#include "dsp/DspCommon.h"

#include <vector>

namespace cs {

// ---------------------------------------------------------------------------
// Effect section (Synth1 CEffector + CDistortion/CDecimator/CRingMod/
// CCompressor/CPhaser). Types: a.d.1, a.d.2, d.d., deci., r.m., comp., ph.1..ph.4
// ---------------------------------------------------------------------------
class EffectUnit {
public:
    void setSampleRate(double fs);
    void setOn(bool on)    { mOn = on; }
    void setType(int t)    { if (t != mType) { mType = t; reset(); } update(); }
    void setCtl1(int v)    { mCtl1 = v; update(); }
    void setCtl2(int v)    { mCtl2 = v; update(); }
    void setLevel(int v)   { mLevel = v; }
    void reset();
    void process(float* L, float* R, int n);

private:
    void update();
    float distort(float x, int ch);

    double mFs = 44100.0;
    bool   mOn = false;
    int    mType = 0, mCtl1 = 64, mCtl2 = 64, mLevel = 64;
    // distortion
    float  mDrive = 1.f;
    OnePole mTone[2];
    // decimator
    float  mHold[2] = {}, mHoldCount = 0.f, mHoldStep = 1.f, mQuant = 0.f;
    // ring mod
    double mRmPhase = 0.0, mRmInc = 0.0;
    // compressor
    float  mEnv = 0.f, mThresh = 0.5f, mRatio = 4.f, mAtk = 0.f, mRel = 0.f, mMakeup = 1.f;
    // phaser
    struct AllPass { float z = 0.f; };
    AllPass mAp[2][12];
    int    mStages = 4;
    double mPhPhase = 0.0, mPhInc = 0.0;
    float  mPhDepth = 0.5f, mPhFb = 0.f, mPhLast[2] = {};
};

// ---------------------------------------------------------------------------
// Equalizer / tone / pan (CEq x2 + CPan)
// ---------------------------------------------------------------------------
class EqualizerPan {
public:
    void setSampleRate(double fs) { mFs = fs; updatePeak(); updateTone(); }
    void setFreq(int v)  { mFreq = v; updatePeak(); }
    void setLevel(int v) { mGain = v; updatePeak(); }
    void setQ(int v)     { mQ = v; updatePeak(); }
    void setTone(int v)  { mTone = v; updateTone(); }
    void setPan(int v)   { mPan = v; }
    void reset();
    void process(float* L, float* R, int n, float lfoPan);

private:
    void updatePeak();
    void updateTone();
    double mFs = 44100.0;
    int mFreq = 64, mGain = 64, mQ = 64, mTone = 64, mPan = 64;
    bool mPeakOn = false, mToneOn = false;
    Biquad mPeak[2];
    FirstOrder mToneF[2];
};

// ---------------------------------------------------------------------------
// Tempo delay (CDelay): ST / X (cross feedback) / PP (ping-pong), 3 s buffer,
// tempo-synced time table, L/R spread, tone filter in the feedback loop.
// ---------------------------------------------------------------------------
class TempoDelay {
public:
    void setSampleRate(double fs);
    void setOn(bool on)      { if (on && !mOn) clear(); mOn = on; }
    void setType(int t)      { mType = t; }
    void setTime(int idx)    { mTimeIdx = clampv(idx, 0, 19); }
    void setSpread(int v)    { mSpread = v; }
    void setFeedback(int v)  { mFeedback = v * (1.f / 127.f) * 0.98f; }
    void setTone(int v);
    void setDryWet(int v)    { mWet = v * (1.f / 127.f); }
    void clear();
    void process(float* L, float* R, int n, double bpm);

private:
    inline float readAt(const std::vector<float>& buf, double delaySamples) const;
    double mFs = 44100.0;
    bool   mOn = false;
    int    mType = 0, mTimeIdx = 12, mSpread = 64;
    float  mFeedback = 0.3f, mWet = 0.25f;
    bool   mToneOn = false;
    FirstOrder mToneF[2];
    std::vector<float> mBuf[2];
    int    mWrite = 0, mSize = 0;
    Smoothed mTimeL, mTimeR;
};

// ---------------------------------------------------------------------------
// Chorus / flanger (CChorus): 1..4 stages (1 = mono, best for flanging),
// time 0.05..30 ms, depth, rate 0.01..400 Hz, +/- feedback, level.
// ---------------------------------------------------------------------------
class Chorus {
public:
    void setSampleRate(double fs);
    void setOn(bool on)     { if (on && !mOn) clear(); mOn = on; }
    void setType(int t)     { mStages = clampv(t, 1, 4); }
    void setTime(int v)     { mTime = curves::chorusTimeSeconds(v); }
    void setDepth(int v)    { mDepth = curves::chorusDepth(v); }
    void setRate(int v)     { mRateHz = curves::chorusRateHz(v); }
    void setFeedback(int v) { mFb = float(curves::chorusFeedback(v)); }
    void setLevel(int v)    { mLevel = v * (1.f / 127.f); }
    void clear();
    void process(float* L, float* R, int n);

private:
    double mFs = 44100.0;
    bool   mOn = false;
    int    mStages = 2;
    double mTime = 0.01, mDepth = 0.3, mRateHz = 1.0, mPhase = 0.0;
    float  mFb = 0.f, mLevel = 0.5f;
    std::vector<float> mBuf[2];
    int    mWrite = 0, mSize = 0;
    float  mFbState[2] = {};
    Smoothed mTimeSmooth;
};

} // namespace cs
