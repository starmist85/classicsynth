#include "dsp/Effects.h"

#include <cmath>
#include <cstring>

namespace cs {

// ===========================================================================
// EffectUnit
// ===========================================================================
void EffectUnit::setSampleRate(double fs)
{
    mFs = fs;
    update();
    reset();
}

void EffectUnit::reset()
{
    for (auto& t : mTone) t.reset();
    mHold[0] = mHold[1] = 0.f;
    mHoldCount = 0.f;
    mEnv = 0.f;
    for (auto& ch : mAp) for (auto& a : ch) a.z = 0.f;
    mPhLast[0] = mPhLast[1] = 0.f;
}

void EffectUnit::update()
{
    const double c1 = mCtl1 / 127.0, c2 = mCtl2 / 127.0;
    switch (mType) {
    case 0: case 1: case 2:                                  // distortions
        mDrive = float(1.0 + c1 * c1 * 60.0);
        for (auto& t : mTone) t.setCutoff(400.0 * std::pow(50.0, c2), mFs);
        break;
    case 3:                                                  // decimator
        mHoldStep = float(1.0 / (1.0 + c1 * c1 * 63.0));
        mQuant = c2 <= 0.0 ? 0.f : float(std::pow(2.0, 16.0 - c2 * 14.0));
        break;
    case 4:                                                  // ring modulator
        mRmInc = 20.0 * std::pow(250.0, c1) / mFs;
        break;
    case 5:                                                  // compressor
        mThresh = float(std::pow(10.0, -c1 * 40.0 / 20.0));
        mRatio = float(1.0 + c2 * 19.0);
        mAtk = float(1.0 - std::exp(-1.0 / (0.003 * mFs)));
        mRel = float(1.0 - std::exp(-1.0 / (0.12 * mFs)));
        mMakeup = float(1.0 / std::pow(double(mThresh), 1.0 - 1.0 / mRatio) * 0.5 + 0.5);
        break;
    default: {                                               // phasers ph.1..ph.4
        static const int stages[] = { 4, 6, 8, 12 };
        mStages = stages[clampv(mType - 6, 0, 3)];
        mPhInc = (0.05 * std::pow(200.0, c1)) / mFs;
        mPhDepth = 0.9f;
        mPhFb = float(c2 * 0.85);
        break;
    }
    }
}

float EffectUnit::distort(float x, int ch)
{
    float y;
    const float d = x * mDrive;
    switch (mType) {
    case 0:  y = fastTanh(d); break;                                    // a.d.1: symmetric soft
    case 1: {                                                           // a.d.2: asymmetric tube-ish
        const float b = 0.35f;
        y = fastTanh(d + b) - fastTanh(b);
        y *= 1.3f;
        break;
    }
    default:                                                            // d.d.: hard clip + fold
        y = d;
        if (y > 1.f)  y = 2.f - y;
        if (y < -1.f) y = -2.f - y;
        y = clampv(y, -1.f, 1.f);
        break;
    }
    return mTone[ch].lp(y) * (1.f / std::sqrt(std::min(mDrive, 16.f)) * 0.5f + 0.5f);
}

void EffectUnit::process(float* L, float* R, int n)
{
    if (!mOn) return;
    const float wet = mLevel * (1.f / 127.f), dry = 1.f - wet;
    float* io[2] = { L, R };

    for (int i = 0; i < n; ++i) {
        float x[2] = { L[i], R[i] }, y[2];
        switch (mType) {
        case 0: case 1: case 2:
            y[0] = distort(x[0], 0);
            y[1] = distort(x[1], 1);
            break;
        case 3:
            mHoldCount += mHoldStep;
            if (mHoldCount >= 1.f) {
                mHoldCount -= 1.f;
                for (int c = 0; c < 2; ++c)
                    mHold[c] = mQuant > 0.f ? std::round(x[c] * mQuant) / mQuant : x[c];
            }
            y[0] = mHold[0];
            y[1] = mHold[1];
            break;
        case 4: {
            const float c = float(std::sin(kTwoPi * mRmPhase));
            const float shape = mCtl2 / 127.f;   // sine -> square blend
            const float car = c * (1.f - shape) + (c >= 0.f ? 1.f : -1.f) * shape;
            mRmPhase += mRmInc;
            if (mRmPhase >= 1.0) mRmPhase -= 1.0;
            y[0] = x[0] * car;
            y[1] = x[1] * car;
            break;
        }
        case 5: {
            const float lev = std::max(std::fabs(x[0]), std::fabs(x[1]));
            mEnv += (lev > mEnv ? mAtk : mRel) * (lev - mEnv);
            float g = 1.f;
            if (mEnv > mThresh) g = std::pow(mThresh / mEnv, 1.f - 1.f / mRatio);
            y[0] = x[0] * g * mMakeup;
            y[1] = x[1] * g * mMakeup;
            break;
        }
        default: {
            const double lfo = 0.5 + 0.5 * std::sin(kTwoPi * mPhPhase);
            mPhPhase += mPhInc;
            if (mPhPhase >= 1.0) mPhPhase -= 1.0;
            const double fc = 200.0 * std::pow(20.0, lfo * mPhDepth);
            const double t = std::tan(kPi * std::min(fc, mFs * 0.45) / mFs);
            const float a = float((t - 1.0) / (t + 1.0));
            for (int c = 0; c < 2; ++c) {
                float s = x[c] + mPhLast[c] * mPhFb;
                for (int k = 0; k < mStages; ++k) {
                    AllPass& ap = mAp[c][k];
                    const float o = a * s + ap.z;
                    ap.z = s - a * o;
                    s = o;
                }
                mPhLast[c] = s;
                y[c] = s;
            }
            break;
        }
        }
        for (int c = 0; c < 2; ++c) io[c][i] = x[c] * dry + y[c] * wet;
    }
}

// ===========================================================================
// EqualizerPan
// ===========================================================================
void EqualizerPan::reset()
{
    for (auto& b : mPeak) b.reset();
    for (auto& b : mToneF) b.reset();
}

void EqualizerPan::updatePeak()
{
    const double db = curves::eqGainDb(mGain);
    mPeakOn = std::fabs(db) > 0.05;
    for (auto& b : mPeak) b.setPeak(curves::eqFreqHz(mFreq), curves::eqQ(mQ) * 0.7, db, mFs);
}

void EqualizerPan::updateTone()
{
    mToneOn = mTone != 64;
    bool hp = false;
    const double hz = curves::toneHz(mTone, hp);
    for (auto& b : mToneF) {
        if (hp) b.setHighPass(hz, mFs);
        else    b.setLowPass(hz, mFs);
    }
}

void EqualizerPan::process(float* L, float* R, int n, float lfoPan)
{
    if (mPeakOn)
        for (int i = 0; i < n; ++i) { L[i] = mPeak[0].process(L[i]); R[i] = mPeak[1].process(R[i]); }
    if (mToneOn)
        for (int i = 0; i < n; ++i) { L[i] = mToneF[0].process(L[i]); R[i] = mToneF[1].process(R[i]); }

    const float base = mPan < 64 ? (mPan - 64) / 64.f : (mPan - 64) / 63.f;
    const float p = clampv(base + lfoPan, -1.f, 1.f);
    if (p != 0.f) {
        const float gL = p > 0.f ? 1.f - p : 1.f;
        const float gR = p < 0.f ? 1.f + p : 1.f;
        for (int i = 0; i < n; ++i) { L[i] *= gL; R[i] *= gR; }
    }
}

// ===========================================================================
// TempoDelay
// ===========================================================================
void TempoDelay::setSampleRate(double fs)
{
    mFs = fs;
    mSize = int(fs * 3.2) + 4;
    for (auto& b : mBuf) b.assign(size_t(mSize), 0.f);
    mWrite = 0;
    setTone(64);
}

void TempoDelay::setTone(int v)
{
    mToneOn = v != 64;
    bool hp = false;
    const double hz = curves::toneHz(v, hp);
    for (auto& b : mToneF) {
        if (hp) b.setHighPass(hz, mFs);
        else    b.setLowPass(hz, mFs);
    }
}

void TempoDelay::clear()
{
    for (auto& b : mBuf) std::fill(b.begin(), b.end(), 0.f);
    for (auto& b : mToneF) b.reset();
}

inline float TempoDelay::readAt(const std::vector<float>& buf, double d) const
{
    double pos = mWrite - d;
    while (pos < 0) pos += mSize;
    const int i0 = int(pos);
    const int i1 = (i0 + 1) % mSize;
    const float f = float(pos - i0);
    return buf[size_t(i0)] + (buf[size_t(i1)] - buf[size_t(i0)]) * f;
}

void TempoDelay::process(float* L, float* R, int n, double bpm)
{
    if (!mOn || mSize == 0) return;

    double base = mTimeIdx == 0 ? 0.0001 : curves::ticksToSeconds(curves::kDelayTicks[mTimeIdx], bpm);
    const double spread = curves::delaySpreadSeconds(mSpread);
    double tl = base + std::max(spread, 0.0), tr = base + std::max(-spread, 0.0);
    const double maxT = (mSize - 4) / mFs;
    tl = std::min(tl, maxT); tr = std::min(tr, maxT);
    // Ramp time changes (tape-like) instead of jumping.
    mTimeL.setTarget(float(tl * mFs), n);
    mTimeR.setTarget(float(tr * mFs), n);

    const float wetG = std::min(1.f, 2.f * mWet), dryG = std::min(1.f, 2.f * (1.f - mWet));
    for (int i = 0; i < n; ++i) {
        float dl = readAt(mBuf[0], std::max(1.f, mTimeL.next()));
        float dr = readAt(mBuf[1], std::max(1.f, mTimeR.next()));
        if (mToneOn) { dl = mToneF[0].process(dl); dr = mToneF[1].process(dr); }

        float wl, wr;
        switch (mType) {
        case 1:  wl = L[i] + dr * mFeedback; wr = R[i] + dl * mFeedback; break;          // X
        case 2:  wl = 0.5f * (L[i] + R[i]) + dr * mFeedback; wr = dl * mFeedback; break; // PP
        default: wl = L[i] + dl * mFeedback; wr = R[i] + dr * mFeedback; break;          // ST
        }
        mBuf[0][size_t(mWrite)] = fastTanh(wl * 0.5f) * 2.f;   // gentle limit in the loop
        mBuf[1][size_t(mWrite)] = fastTanh(wr * 0.5f) * 2.f;
        if (++mWrite >= mSize) mWrite = 0;

        L[i] = L[i] * dryG + dl * wetG;
        R[i] = R[i] * dryG + dr * wetG;
    }
}

// ===========================================================================
// Chorus
// ===========================================================================
void Chorus::setSampleRate(double fs)
{
    mFs = fs;
    mSize = int(fs * 0.08) + 4;   // 80 ms
    for (auto& b : mBuf) b.assign(size_t(mSize), 0.f);
    mWrite = 0;
    mTimeSmooth.snap(float(mTime * fs));
}

void Chorus::clear()
{
    for (auto& b : mBuf) std::fill(b.begin(), b.end(), 0.f);
    mFbState[0] = mFbState[1] = 0.f;
}

void Chorus::process(float* L, float* R, int n)
{
    if (!mOn || mSize == 0) return;
    mTimeSmooth.setTarget(float(mTime * mFs), n);
    const double inc = mRateHz / mFs;
    const float norm = 1.f / (1.f + 0.5f * mLevel);

    for (int i = 0; i < n; ++i) {
        const float center = std::max(mTimeSmooth.next(), 1.f);
        const float mod = float(mDepth) * 0.95f * center;

        mBuf[0][size_t(mWrite)] = L[i] + mFbState[0] * mFb;
        mBuf[1][size_t(mWrite)] = R[i] + mFbState[1] * mFb;

        float out[2] = { 0.f, 0.f };
        for (int s = 0; s < mStages; ++s) {
            const double ph = mPhase + double(s) / mStages;
            const float d = center + mod * float(std::sin(kTwoPi * ph)) + 1.f;
            double pos = mWrite - d;
            while (pos < 0) pos += mSize;
            const int i0 = int(pos), i1 = (i0 + 1) % mSize;
            const float f = float(pos - i0);
            // Stage 1 is mono; more stages alternate sides for stereo width.
            const int ch = mStages == 1 ? -1 : (s & 1);
            for (int c = 0; c < 2; ++c) {
                if (ch >= 0 && ch != c) continue;
                const auto& b = mBuf[c];
                out[c] += b[size_t(i0)] + (b[size_t(i1)] - b[size_t(i0)]) * f;
            }
        }
        const float per = mStages == 1 ? 1.f : 2.f / mStages;
        out[0] *= per; out[1] *= per;
        mFbState[0] = out[0];
        mFbState[1] = out[1];

        mPhase += inc;
        if (mPhase >= 1.0) mPhase -= 1.0;
        if (++mWrite >= mSize) mWrite = 0;

        L[i] = (L[i] + out[0] * mLevel) * norm;
        R[i] = (R[i] + out[1] * mLevel) * norm;
    }
}

} // namespace cs
