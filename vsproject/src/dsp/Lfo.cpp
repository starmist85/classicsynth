#include "dsp/Lfo.h"

#include <cmath>

namespace cs {

float Lfo::advance(int n, double bpm)
{
    double hz;
    if (mTempo) {
        const int idx = clampv((mSpeed * 30) >> 7, 0, 29);
        // FUN_180015210: bpm * 48 / ticks / 120  ==  (bpm/60) * (24/ticks)
        hz = (bpm / 60.0) * (24.0 / curves::kLfoSyncTicks[idx]);
    } else {
        hz = curves::lfoFreeHz(mSpeed);
    }

    mPhase += hz * n / mFs;
    if (mPhase >= 1.0) {
        mPhase -= std::floor(mPhase);
        newRandom();
    }

    const float p = float(mPhase);
    switch (mWave) {
    case kLfoSaw:    return 1.f - 2.f * p;
    case kLfoTri:    return p < 0.5f ? -1.f + 4.f * p : 3.f - 4.f * p;
    case kLfoSquare: return p < 0.5f ? 1.f : -1.f;
    case kLfoSH:     return mRand;
    case kLfoSmooth: return mPrevRand + (mRand - mPrevRand) * p;
    default:         return float(std::sin(kTwoPi * mPhase));
    }
}

float Lfo::modulation(float w) const
{
    if (!on()) return 0.f;
    const float x = mDepth * (1.f / 127.f);
    const float uni = 0.5f * (w + 1.f);
    // Ranges follow FUN_180015080 (min/max per destination).
    switch (mDest) {
    case kDestOsc2:
    case kDestOsc12: return w * float(curves::lfoPitchDepth(mDepth)) * 60.f; // +/-5 oct (640/128)
    case kDestFilter: return uni * x * 1024.f;                                 // cutoff index
    case kDestAmp:    return uni * x;                                          // gain reduction
    case kDestPW:     return w * x * 0.48f;                                    // duty offset
    case kDestFM:     return uni * x * 0.5f;                                   // FM amount (512/1024)
    case kDestPan:    return w * x;                                            // -1..1
    default:          return 0.f;
    }
}

} // namespace cs
