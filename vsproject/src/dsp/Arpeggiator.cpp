#include "dsp/Arpeggiator.h"

#include <algorithm>
#include <cmath>

namespace cs {

void Arpeggiator::setOn(bool on)
{
    if (mOn == on) return;
    mOn = on;
    mPos = -1;
    mDir = 1;
    mSamplesToNext = 0.0;
    mLastStepIndex = -1.0;
}

void Arpeggiator::noteOn(int note, float vel)
{
    mSustained.erase(std::remove_if(mSustained.begin(), mSustained.end(),
                                    [&](const Held& h) { return h.note == note; }), mSustained.end());
    for (auto& h : mHeld) if (h.note == note) { h.vel = vel; return; }
    const bool first = mHeld.empty() && mSustained.empty();
    mHeld.push_back({ note, vel });
    rebuild();
    if (first) { mPos = -1; mDir = 1; mSamplesToNext = 0.0; mLastStepIndex = -1.0; }
}

void Arpeggiator::noteOff(int note)
{
    auto it = std::find_if(mHeld.begin(), mHeld.end(), [&](const Held& h) { return h.note == note; });
    if (it == mHeld.end()) return;
    if (mSustain) mSustained.push_back(*it);
    mHeld.erase(it);
    rebuild();
}

void Arpeggiator::allNotesOff()
{
    mHeld.clear();
    mSustained.clear();
    rebuild();
}

void Arpeggiator::setSustain(bool down)
{
    mSustain = down;
    if (!down) { mSustained.clear(); rebuild(); }
}

void Arpeggiator::rebuild()
{
    std::vector<Held> base = mHeld;
    base.insert(base.end(), mSustained.begin(), mSustained.end());
    std::sort(base.begin(), base.end(), [](const Held& a, const Held& b) { return a.note < b.note; });

    mPattern.clear();
    for (int oct = 0; oct <= mRange; ++oct)
        for (const auto& h : base)
            if (h.note + oct * 12 <= 127) mPattern.push_back({ h.note + oct * 12, h.vel });

    if (mPattern.empty()) { mPos = -1; return; }
    if (mPos >= int(mPattern.size())) mPos = int(mPattern.size()) - 1;
}

void Arpeggiator::releaseCurrent(int offset, std::vector<ArpEvent>& out)
{
    if (mPlaying >= 0) {
        out.push_back({ offset, mPlaying, 0.f, false });
        mPlaying = -1;
    }
}

void Arpeggiator::step(int offset, std::vector<ArpEvent>& out)
{
    const int n = int(mPattern.size());
    if (n == 0) { releaseCurrent(offset, out); return; }

    switch (mType) {
    case 1: // updown
        if (n == 1) mPos = 0;
        else {
            mPos += mDir;
            if (mPos >= n) { mDir = -1; mPos = n - 2; }
            else if (mPos < 0) { mDir = 1; mPos = n > 1 ? 1 : 0; }
        }
        break;
    case 3: // down
        mPos = mPos <= 0 ? n - 1 : mPos - 1;
        break;
    case 4: // random
        mPos = int(mRng.next() % uint32_t(n));
        break;
    default: // up
        mPos = (mPos + 1) % n;
        break;
    }
    mPos = clampv(mPos, 0, n - 1);

    const Held& h = mPattern[mPos];
    const int prev = mPlaying;
    // Full gate = legato: new note starts before the previous one stops.
    out.push_back({ offset, h.note, h.vel, true });
    if (prev >= 0 && prev != h.note) out.push_back({ offset, prev, 0.f, false });
    mPlaying = h.note;
}

void Arpeggiator::process(int n, double bpm, double ppqStart, bool playing, std::vector<ArpEvent>& out)
{
    if (!mOn) { releaseCurrent(0, out); return; }

    const double stepQuarter = curves::kArpTicks[mBeat] / 24.0;
    const double stepSamples = curves::ticksToSeconds(curves::kArpTicks[mBeat], bpm) * mFs;
    const double gate = mGate >= 127 ? 2.0 : std::max(0.02, mGate / 127.0);

    if (mPattern.empty()) {
        if (mPlaying >= 0) releaseCurrent(0, out);
        mLastStepIndex = -1.0;
        mSamplesToNext = 0.0;
        return;
    }

    const double samplesPerQuarter = 60.0 / std::max(bpm, 1.0) * mFs;
    for (int t = 0; t < n; ++t) {
        // Gate end of the current step.
        if (mSamplesToOff >= 0.0) {
            mSamplesToOff -= 1.0;
            if (mSamplesToOff <= 0.0) { releaseCurrent(t, out); mSamplesToOff = -1.0; }
        }

        bool stepNow;
        if (playing) {
            // Steps fall on the host's musical grid.
            const double idx = std::floor((ppqStart + t / samplesPerQuarter) / stepQuarter + 1e-9);
            stepNow = idx != mLastStepIndex;
            if (stepNow) mLastStepIndex = idx;
        } else {
            stepNow = mSamplesToNext <= 0.0;
            if (stepNow) mSamplesToNext += stepSamples;
            mSamplesToNext -= 1.0;
        }

        if (stepNow) {
            step(t, out);
            mSamplesToOff = gate >= 1.99 ? -1.0 : stepSamples * gate;
        }
    }
    if (!playing) mLastStepIndex = -1.0;
}

} // namespace cs
