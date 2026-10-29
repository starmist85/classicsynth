#pragma once
// ADSR envelope modelled on Synth1's CEnvelopedObject (0x18000c6e0..):
//   state 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
//   attack  : linear ramp over the attack time
//   decay / release : piecewise slope that steepens as the level falls, reaching
//                     ~1/20 of the start level at the stage time (FUN_18000c780).
// The piecewise curve is reproduced here as an exponential with tau = T / ln(20).

#include <cmath>

namespace cs {

class Envelope {
public:
    enum Stage { Idle = 0, Attack, Decay, Sustain, Release };

    void setSampleRate(double fs) { mFs = fs; recalc(); }
    void setAttack(double s)  { mA = s; recalc(); }
    void setDecay(double s)   { mD = s; recalc(); }
    void setSustain(double l) { mS = float(l); }
    void setRelease(double s) { mR = s; recalc(); }

    void gateOn(bool retrigger = true)
    {
        if (!retrigger && mStage != Idle && mStage != Release) return;
        mStage = Attack;
        if (retrigger && mLevel <= 0.f) mLevel = 0.f;   // attack starts from current level
    }
    void gateOff() { if (mStage != Idle) mStage = Release; }
    void reset() { mStage = Idle; mLevel = 0.f; }

    bool  active() const { return mStage != Idle; }
    Stage stage() const  { return mStage; }
    float level() const  { return mLevel; }

    inline float process()
    {
        switch (mStage) {
        case Attack:
            mLevel += mAttackStep;
            if (mLevel >= 1.f) { mLevel = 1.f; mStage = Decay; }
            break;
        case Decay:
            mLevel = mS + (mLevel - mS) * mDecayCoef;
            if (mLevel - mS < 1e-4f) { mLevel = mS; mStage = Sustain; }
            break;
        case Sustain:
            mLevel = mS;
            break;
        case Release:
            mLevel *= mReleaseCoef;
            if (mLevel < 1e-5f) { mLevel = 0.f; mStage = Idle; }
            break;
        default:
            break;
        }
        return mLevel;
    }

    // Advances n samples at once (for control-rate envelopes).
    inline float process(int n)
    {
        float v = mLevel;
        for (int i = 0; i < n; ++i) v = process();
        return v;
    }

private:
    void recalc()
    {
        const double a = std::max(mA * mFs, 1.0);
        mAttackStep = float(1.0 / a);
        const double k = std::log(20.0);
        mDecayCoef   = float(std::exp(-k / std::max(mD * mFs, 1.0)));
        mReleaseCoef = float(std::exp(-k / std::max(mR * mFs, 1.0)));
    }

    double mFs = 44100.0, mA = 0.001, mD = 0.3, mR = 0.2;
    float  mS = 1.f;
    float  mLevel = 0.f;
    float  mAttackStep = 1.f, mDecayCoef = 0.999f, mReleaseCoef = 0.999f;
    Stage  mStage = Idle;
};

} // namespace cs
