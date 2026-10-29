#pragma once
// One synth voice: OSC1 (+8x detune, sub osc) / OSC2 (sync, ring, FM source),
// mod envelope, filter + filter envelope, amp + amp envelope, portamento.
// Mirrors Synth1's CVoice layout (CVco @+0x50, CVcf @+0x220, CVca @+0x3f0).

#include "dsp/DspCommon.h"
#include "dsp/Envelope.h"
#include "dsp/Filter.h"
#include "dsp/Oscillator.h"

namespace cs {

// Engine-wide values shared by all voices, refreshed when parameters change.
struct VoiceParams {
    // Oscillators
    int    osc1Wave = kWaveSaw, osc2Wave = kO2Saw, subWave = 0;
    bool   subOctDown = true;
    float  subGain = 0.f;              // g, mixed as (o1 + g*sub)/(1+g)
    float  osc1Detune = 0.f;           // 0..1 -> 8 detuned copies
    float  fm = 0.f;                   // 0..1
    bool   osc2Track = true, sync = false, ring = false;
    float  osc2Semis = 0.f;            // pitch + fine (osc2 only)
    float  commonCents = 0.f;          // osc1,2 fine tune
    int    keyShift = 0;
    float  mix = 0.5f;                 // 0 = osc1, 1 = osc2
    float  duty = 0.5f;
    int    oscPhase = 0;               // 0 = free running
    // Mod envelope
    bool   modEnvOn = false;
    int    modEnvDest = 0;             // 0 osc2 pitch, 1 FM, 2 p/w
    float  modEnvAmt = 0.f;            // -1..1
    double modEnvA = 0.001, modEnvD = 0.3;
    // Filter
    int    fltType = kLP24;
    float  fltFreqIndex = 800.f;       // 0..1023
    float  fltRes = 0.f, fltSat = 0.f, fltAmt = 0.f, fltTrk = 0.5f;
    bool   fltVel = false;
    double fltA = 0.001, fltD = 0.3, fltR = 0.2;
    float  fltS = 0.5f;
    // Amp
    double ampA = 0.001, ampD = 0.3, ampR = 0.2;
    float  ampS = 1.f, ampGain = 0.7f, ampVel = 0.5f;
    // Glide
    double portaSeconds = 0.0;
    // Live modulation (updated every control block)
    float  pitchBend = 0.f;            // semitones
    float  lfoPitchOsc2 = 0.f, lfoPitchBoth = 0.f;   // semitones
    float  lfoFilter = 0.f, lfoAmp = 0.f, lfoPW = 0.f, lfoFM = 0.f, lfoPan = 0.f;
};

class Voice {
public:
    void setSampleRate(double fs);

    // Unison placement for this voice.
    struct Placement { float detuneSemis = 0, pitchSemis = 0, pan = 0, phase = 0; };

    // glideFrom: start pitch for portamento (semitones), or < 0 for none.
    void noteOn(int note, float velocity, const Placement& place, bool retrigger, double glideFrom,
                const VoiceParams& p);
    void applyEnvParams(const VoiceParams& p);
    double pitch() const { return mPitch; }
    void legatoTo(int note, bool glide);      // pitch change only (legato / unison follow)
    void noteOff();
    void kill();                              // immediate stop (voice stealing)
    void fastRelease();                       // short fade when stolen

    bool  active() const   { return mAmpEnv.active(); }
    bool  released() const { return mReleased; }
    int   note() const     { return mNote; }
    int   group() const    { return mGroup; }
    void  setGroup(int g)  { mGroup = g; }
    unsigned age() const   { return mAge; }
    float level() const    { return mAmpEnv.level(); }

    void render(float* outL, float* outR, int n, const VoiceParams& p);

private:
    void updateControl(const VoiceParams& p, int n);

    double mFs = 44100.0;
    int    mNote = 60, mGroup = -1;
    unsigned mAge = 0;
    float  mVel = 1.f;
    bool   mReleased = false;
    Placement mPlace;

    // Pitch / glide (in semitones)
    double mPitch = 60.0, mTargetPitch = 60.0;
    bool   mGliding = false;

    // Oscillator phases (32-bit fixed point)
    uint32_t mPh1[8] = {}, mPh2 = 0, mPhSub = 0;
    uint32_t mInc1[8] = {}, mInc2 = 0, mIncSub = 0, mDutyOff = 0x80000000u;
    float    mFmNow = 0.f;
    MSequenceNoise mNoise;
    Rng      mRng;

    Envelope    mAmpEnv, mFltEnv, mModEnv;
    VoiceFilter mFilter;

    float mGainL = 0.7f, mGainR = 0.7f, mAmpNow = 0.f;
    Smoothed mAmpSmooth;
    float mFadeGain = 1.f, mFadeStep = 0.f;
};

} // namespace cs
