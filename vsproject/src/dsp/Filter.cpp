#include "dsp/Filter.h"

#include <cmath>

namespace cs {

void VoiceFilter::reset()
{
    mIc1 = mIc2 = 0;
    mS1 = mS2 = mS3 = mS4 = 0;
    mD1 = mD2 = mD3 = mD4 = 0;
    mPrevIn = 0;
}

void VoiceFilter::setParams(double cutoffHz, double resonance, double saturation)
{
    resonance = clampv(resonance, 0.0, 1.0);
    const double nyq = mFs * 0.5;
    const double fc = clampv(cutoffHz, 10.0, nyq * 0.95);
    const double g = std::tan(kPi * fc / mFs);

    // SVF (Cytomic TPT): k = 2 - 2r, r just below self-oscillation at max.
    mK = float(2.0 - 1.97 * resonance);
    mA1 = float(1.0 / (1.0 + g * (g + mK)));
    mA2 = float(g * mA1);
    mA3 = float(g * mA2);

    // Ladder: feedback 0..4 (self-oscillates at the top), partial bass compensation.
    mG = float(g / (1.0 + g));
    mOneOverOnePlusG = float(1.0 / (1.0 + g));
    mFb = float(resonance * 4.05);
    mBassComp = float(1.0 + resonance * 1.2);

    // Diode ladder runs at 2x when oversampling.
    const double fsD = mOversample ? mFs * 2.0 : mFs;
    mGd = float(1.0 - std::exp(-kTwoPi * std::min(fc, fsD * 0.45) / fsD));
    mFbDiode = float(resonance * 4.4);
    mRes = float(resonance);

    // Saturation: soft-clip drive before the filter (CVcf sat, 0x17).
    if (saturation <= 0.0) { mDrive = 0.f; mDriveComp = 1.f; }
    else {
        mDrive = float(1.0 + saturation * saturation * 24.0);
        mDriveComp = float(1.0 / std::tanh(std::min(double(mDrive), 3.0)) * (1.0 / (1.0 + saturation * 0.5)));
    }
}

} // namespace cs
