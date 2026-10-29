#pragma once
// Voice filter (Synth1 CVcf): LP12, LP24, HP12, BP12 and LPDL (diode ladder,
// added in V1.13 beta2: 4th order, fatter/darker than LP24, resonance does not
// thin the low end, oversampled at 44.1/48 kHz).
//
// Implemented as zero-delay-feedback (TPT) structures so modulation is smooth.

#include "dsp/DspCommon.h"

namespace cs {

enum FilterType { kLP12 = 0, kLP24 = 1, kHP12 = 2, kBP12 = 3, kLPDL = 4 };

class VoiceFilter {
public:
    void setSampleRate(double fs) { mFs = fs; mOversample = fs < 64000.0; }
    void setType(int t) { if (t != mType) { mType = t; reset(); } }
    void reset();

    // Called at control rate. cutoff in Hz, resonance 0..1, saturation 0..1
    void setParams(double cutoffHz, double resonance, double saturation);

    inline float process(float x)
    {
        if (mDrive > 0.f) x = fastTanh(x * mDrive) * mDriveComp;
        switch (mType) {
        case kLP12: return svf(x, 0);
        case kHP12: return svf(x, 1);
        case kBP12: return svf(x, 2);
        case kLP24: return ladder(x);
        default:    return diodeLadder(x);
        }
    }

private:
    inline float svf(float x, int mode)
    {
        const float v3 = x - mIc2;
        const float v1 = mA1 * mIc1 + mA2 * v3;
        const float v2 = mIc2 + mA2 * mIc1 + mA3 * v3;
        mIc1 = 2.f * v1 - mIc1;
        mIc2 = 2.f * v2 - mIc2;
        if (mode == 0) return v2;
        if (mode == 1) return x - mK * v1 - v2;
        return v1;
    }

    inline float ladder(float x)
    {
        // Linear ZDF 4-pole with tanh at the summing point.
        const float G = mG;
        const float S = G * G * G * mS1 + G * G * mS2 + G * mS3 + mS4;
        const float G4 = G * G * G * G;
        const float y4 = (G4 * x * mBassComp + S * mOneOverOnePlusG) / (1.f + mFb * G4);
        float u = fastTanh(x * mBassComp - mFb * y4);
        float y;
        y = stage(u, mS1);
        y = stage(y, mS2);
        y = stage(y, mS3);
        y = stage(y, mS4);
        return y;
    }

    inline float stage(float x, float& s)
    {
        const float v = (x - s) * mG;
        const float y = v + s;
        s = y + v;
        return y;
    }

    inline float diodeLadder(float x)
    {
        if (!mOversample) return diodeTick(x);
        // 2x oversampling (linear interpolation up, average down).
        const float a = diodeTick(0.5f * (x + mPrevIn));
        const float b = diodeTick(x);
        mPrevIn = x;
        return 0.5f * (a + b);
    }

    inline float diodeTick(float x)
    {
        // Diode-ladder style: saturating stages, coupled feedback taken from the
        // last stage, input gain compensation keeps bass with high resonance.
        const float fb = mFbDiode * fastTanh(mD4);
        const float in = fastTanh((x * (1.f + 0.6f * mRes)) - fb);
        mD1 += mGd * (in - fastTanh(mD1));
        mD2 += mGd * (mD1 - mD2);
        mD3 += mGd * (mD2 - mD3);
        mD4 += mGd * (mD3 - mD4);
        return mD4 * (1.f + 0.4f * mRes);
    }

    double mFs = 44100.0;
    bool   mOversample = true;
    int    mType = kLP24;
    // SVF
    float mIc1 = 0, mIc2 = 0, mA1 = 0, mA2 = 0, mA3 = 0, mK = 2;
    // Ladder
    float mS1 = 0, mS2 = 0, mS3 = 0, mS4 = 0, mG = 0, mFb = 0, mOneOverOnePlusG = 1, mBassComp = 1;
    // Diode
    float mD1 = 0, mD2 = 0, mD3 = 0, mD4 = 0, mGd = 0, mFbDiode = 0, mRes = 0, mPrevIn = 0;
    // Saturation
    float mDrive = 0, mDriveComp = 1;
};

} // namespace cs
