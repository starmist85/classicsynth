#include "dsp/Voice.h"

#include <cmath>

namespace cs {

static constexpr int kControlBlock = 16;
static unsigned gAgeCounter = 0;

void Voice::setSampleRate(double fs)
{
    mFs = fs;
    const uint32_t seed = uint32_t(reinterpret_cast<uintptr_t>(this) >> 4) * 2654435761u;
    mRng.s = seed | 1u;
    mNoise.reg = (seed & 0x7FFFFFu) | 1u;
    mAmpEnv.setSampleRate(fs);
    mFltEnv.setSampleRate(fs / kControlBlock);   // control-rate envelopes
    mModEnv.setSampleRate(fs / kControlBlock);
    mFilter.setSampleRate(fs);
}

void Voice::applyEnvParams(const VoiceParams& p)
{
    mAmpEnv.setAttack(p.ampA); mAmpEnv.setDecay(p.ampD); mAmpEnv.setSustain(p.ampS); mAmpEnv.setRelease(p.ampR);
    mFltEnv.setAttack(p.fltA); mFltEnv.setDecay(p.fltD); mFltEnv.setSustain(p.fltS); mFltEnv.setRelease(p.fltR);
    mModEnv.setAttack(p.modEnvA); mModEnv.setDecay(p.modEnvD); mModEnv.setSustain(0.f); mModEnv.setRelease(p.modEnvD);
}

void Voice::noteOn(int note, float velocity, const Placement& place, bool retrigger, double glideFrom,
                   const VoiceParams& p)
{
    const bool wasActive = active();
    mNote = note;
    mVel = velocity;
    mPlace = place;
    mReleased = false;
    mAge = ++gAgeCounter;
    mFadeGain = 1.f;
    mFadeStep = 0.f;

    mTargetPitch = note;
    mGliding = glideFrom >= 0.0 && p.portaSeconds > 0.0005;
    mPitch = mGliding ? glideFrom : mTargetPitch;

    applyEnvParams(p);

    if (retrigger || !wasActive) {
        mAmpEnv.gateOn();
        mFltEnv.gateOn();
        mModEnv.gateOn();
        // Phase control (V1.09): fixed phase relation at trigger when phase != 0.
        if (p.oscPhase > 0) {
            const uint32_t uni = uint32_t(double(place.phase) * 4294967296.0);
            for (auto& ph : mPh1) ph = uni;
            mPhSub = uni;
            mPh2 = uni + uint32_t((p.oscPhase - 1) / 126.0 * 4294967295.0);
        } else if (!wasActive) {
            // Free running: decorrelate the detune copies and unison voices.
            for (auto& ph : mPh1) ph = mRng.next();
            mPhSub = mPh1[0];
            mPh2 = mRng.next();
        }
        if (!wasActive) mFilter.reset();
    }
}

void Voice::legatoTo(int note, bool glide)
{
    mNote = note;
    mTargetPitch = note;
    mGliding = glide;
    if (!glide) mPitch = mTargetPitch;
    mReleased = false;
    mAge = ++gAgeCounter;
}

void Voice::noteOff()
{
    mReleased = true;
    mAmpEnv.gateOff();
    mFltEnv.gateOff();
    mModEnv.gateOff();
}

void Voice::kill()
{
    mAmpEnv.reset();
    mFltEnv.reset();
    mModEnv.reset();
    mReleased = true;
}

void Voice::fastRelease()
{
    // ~3 ms fade to avoid clicks when a voice is stolen.
    mFadeStep = float(1.0 / std::max(1.0, 0.003 * mFs));
    mReleased = true;
}

void Voice::updateControl(const VoiceParams& p, int n)
{
    // Portamento: exponential glide in pitch space.
    if (mGliding) {
        const double coef = 1.0 - std::exp(-double(n) / std::max(p.portaSeconds * mFs * 0.35, 1.0));
        mPitch += (mTargetPitch - mPitch) * coef;
        if (std::fabs(mTargetPitch - mPitch) < 0.001) { mPitch = mTargetPitch; mGliding = false; }
    }

    const float fe = mFltEnv.process();
    const float me = p.modEnvOn ? mModEnv.process() : 0.f;

    // --- Pitch
    const double common = mPitch + p.keyShift + p.pitchBend + p.commonCents * 0.01
                        + mPlace.detuneSemis + mPlace.pitchSemis + p.lfoPitchBoth;
    double osc2Pitch = (p.osc2Track ? common : 60.0 + p.keyShift + p.commonCents * 0.01) + p.osc2Semis + p.lfoPitchOsc2;

    float fm = p.fm + p.lfoFM;
    float duty = p.duty + p.lfoPW;
    if (p.modEnvOn) {
        const float amt = p.modEnvAmt * me;
        if (p.modEnvDest == 0)      osc2Pitch += amt * 48.0;
        else if (p.modEnvDest == 1) fm += amt;
        else                        duty += amt * 0.48f;
    }
    mFmNow = clampv(fm, 0.f, 1.5f);
    duty = clampv(duty, 0.02f, 0.98f);
    mDutyOff = uint32_t(double(duty) * 4294967295.0);

    const double f1 = noteToHz(common);
    if (p.osc1Detune > 0.f) {
        const double spread = std::pow(double(p.osc1Detune), 1.5) * 0.6;   // up to +/-60 cents
        for (int k = 0; k < 8; ++k) {
            const double off = (k - 3.5) / 3.5 * spread;
            mInc1[k] = hzToPhaseInc(f1 * std::exp2(off / 12.0), mFs);
        }
    } else {
        mInc1[0] = hzToPhaseInc(f1, mFs);
    }
    mIncSub = hzToPhaseInc(p.subOctDown ? f1 * 0.5 : f1, mFs);
    mInc2 = hzToPhaseInc(noteToHz(osc2Pitch), mFs);

    // --- Filter
    const float velScale = p.fltVel ? mVel : 1.f;
    const double keyIdx = (mPitch - 48.0) / 12.0 * std::log2(1.0 + p.fltTrk) * curves::cutoffIndexPerOctave();
    const double idx = p.fltFreqIndex + p.fltAmt * fe * velScale * 1024.0 + keyIdx + p.lfoFilter;
    mFilter.setType(p.fltType);
    mFilter.setParams(curves::cutoffHz(idx), p.fltRes, p.fltSat);

    // --- Amp / pan
    const float vel = 1.f - p.ampVel * (1.f - mVel);
    mAmpSmooth.setTarget(p.ampGain * vel * (1.f - clampv(p.lfoAmp, 0.f, 1.f)), n);
    const float pan = clampv(mPlace.pan + p.lfoPan, -1.f, 1.f);
    const float a = float((pan + 1.0) * kPi * 0.25);
    mGainL = std::cos(a) * 1.41421356f;
    mGainR = std::sin(a) * 1.41421356f;
}

void Voice::render(float* outL, float* outR, int n, const VoiceParams& p)
{
    const WaveTables& wt = WaveTables::instance();
    const int sub = p.subWave;   // 0 sine 1 tri 2 saw 3 square
    const bool detuned = p.osc1Detune > 0.f;
    const float subG = p.subGain;
    const float subNorm = 1.f / (1.f + subG);
    const float mix = p.mix;
    const int o2wave = p.osc2Wave == kO2Tri ? kWaveTri : p.osc2Wave == kO2Saw ? kWaveSaw : kWavePulse;
    const bool noise = p.osc2Wave == kO2Noise;
    (void)wt;

    int done = 0;
    while (done < n) {
        const int blk = std::min(kControlBlock, n - done);
        updateControl(p, blk);

        for (int i = 0; i < blk; ++i) {
            // OSC2 (also the FM modulator)
            const float o2raw = noise ? mNoise.next() : renderWave(o2wave, mPh2, mInc2, mDutyOff);

            // OSC1 with phase modulation from OSC2.
            const uint32_t pm = uint32_t(int32_t(mFmNow * o2raw * 1073741824.0f));   // +/-0.25 cycle per unit
            float o1;
            if (detuned) {
                float acc = 0.f;
                for (int k = 0; k < 8; ++k) {
                    acc += renderWave(p.osc1Wave, mPh1[k] + pm, mInc1[k], mDutyOff);
                    mPh1[k] += mInc1[k];
                }
                o1 = acc * 0.3535f;
            } else {
                const uint32_t before = mPh1[0];
                o1 = renderWave(p.osc1Wave, mPh1[0] + pm, mInc1[0], mDutyOff);
                mPh1[0] += mInc1[0];
                if (p.sync && mPh1[0] < before) mPh2 = 0;    // hard sync on OSC1 wrap
            }
            if (detuned && p.sync && mPh1[0] < mInc1[0]) mPh2 = 0;

            // Sub oscillator (follows FM, square ignores pulse width).
            if (subG > 0.f) {
                const int sw = sub == 3 ? kWavePulse : sub;
                const float s = renderWave(sw, mPhSub + pm, mIncSub, 0x80000000u);
                o1 = (o1 + subG * s) * subNorm;
            }
            mPhSub += mIncSub;

            float o2 = p.ring ? o1 * o2raw : o2raw;
            mPh2 += mInc2;

            float x = o1 * (1.f - mix) + o2 * mix;
            x = mFilter.process(x);

            const float env = mAmpEnv.process();
            float g = env * mAmpSmooth.next();
            if (mFadeStep > 0.f) {
                mFadeGain -= mFadeStep;
                if (mFadeGain <= 0.f) { mFadeGain = 0.f; kill(); }
                g *= mFadeGain;
            }
            const float y = x * g;
            outL[done + i] += y * mGainL;
            outR[done + i] += y * mGainR;
        }
        done += blk;
        if (!active()) break;
    }
}

} // namespace cs
