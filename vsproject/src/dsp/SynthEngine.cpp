#include "dsp/SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cs {

static constexpr int kChunk = 32;   // LFO / modulation update interval

SynthEngine::SynthEngine()
{
    for (int id = 0; id <= kMasterVolume; ++id)
        if (const ParamDef* d = findParam(id)) mBase[id] = d->defValue;
    mMonoStack.reserve(128);
    mArpEvents.reserve(256);
    mMerged.reserve(512);
    mPending.reserve(16);
}

void SynthEngine::setSampleRate(double fs)
{
    mFs = fs;
    for (auto& v : mVoices) v.setSampleRate(fs);
    for (auto& l : mLfo) l.setSampleRate(fs);
    mArp.setSampleRate(fs);
    mFx.setSampleRate(fs);
    mEq.setSampleRate(fs);
    mDelay.setSampleRate(fs);
    mChorus.setSampleRate(fs);
    for (auto& d : mDc) d.setup(fs);
    for (int id = 0; id <= kMasterVolume; ++id)
        if (findParam(id)) apply(id, effectiveValue(id));
}

void SynthEngine::reset()
{
    for (auto& v : mVoices) v.kill();
    for (auto& g : mGroups) g = Group{};
    mMonoStack.clear();
    mMonoGroup = -1;
    mHeldCount = 0;
    mSustained.fill(false);
    mArp.allNotesOff();
    mFx.reset();
    mEq.reset();
    mDelay.clear();
    mChorus.clear();
    for (auto& l : mLfo) l.reset();
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
void SynthEngine::setParam(int id, int value)
{
    const ParamDef* d = findParam(id);
    if (!d) return;
    value = clampValue(*d, value);
    if (mBase[id] == value) return;
    const int oldAssign1 = mBase[kMidiAssign1], oldAssign2 = mBase[kMidiAssign2];
    mBase[id] = value;

    switch (id) {
    case kMidiAssign1:
        if (findParam(oldAssign1)) apply(oldAssign1, effectiveValue(oldAssign1));   // drop old modulation
        reapplyAssigned(0);
        break;
    case kMidiAssign2:
        if (findParam(oldAssign2)) apply(oldAssign2, effectiveValue(oldAssign2));
        reapplyAssigned(1);
        break;
    case kMidiSrc1: case kMidiSens1: reapplyAssigned(0); break;
    case kMidiSrc2: case kMidiSens2: reapplyAssigned(1); break;
    default: break;
    }
    apply(id, effectiveValue(id));
}

float SynthEngine::sourceValue(int slot) const
{
    const int src = mBase[slot == 0 ? kMidiSrc1 : kMidiSrc2];
    const int cc = midiSourceToCC(src);
    if (cc == -1) return mAftertouch;
    if (cc == -2) return mBend;
    if (cc < 0) return 0.f;   // no source
    return mCC[size_t(cc)];
}

int SynthEngine::effectiveValue(int id) const
{
    int v = mBase[id];
    const ParamDef* d = findParam(id);
    if (!d || id >= kNumPatchParams) return v;
    // FUN_180028fe0 prologue: value += (max-min+1) * src * sens
    float add = 0.f;
    for (int slot = 0; slot < 2; ++slot) {
        const int assign = mBase[slot == 0 ? kMidiAssign1 : kMidiAssign2];
        if (assign != id) continue;
        const int sv = mBase[slot == 0 ? kMidiSens1 : kMidiSens2];
        const float sens = (sv > 63 ? sv - 64 : sv - 63) / 63.f;
        add += float(d->maxValue - d->minValue + 1) * sourceValue(slot) * sens;
    }
    if (add != 0.f) v = clampValue(*d, int(float(v) + add));
    return v;
}

void SynthEngine::reapplyAssigned(int slot)
{
    const int assign = mBase[slot == 0 ? kMidiAssign1 : kMidiAssign2];
    if (findParam(assign)) apply(assign, effectiveValue(assign));
}

void SynthEngine::apply(int id, int v)
{
    VoiceParams& p = mVp;
    const float x = v * (1.f / 127.f);
    bool envChanged = false;

    switch (id) {
    // Oscillators
    case kOsc1Shape:  p.osc1Wave = v; break;
    case kOsc2Shape:  p.osc2Wave = v; break;
    case kOsc2Pitch:
    case kOsc2Fine:
        p.osc2Semis = float(curves::osc2Semitones(effectiveValue(kOsc2Pitch)) + curves::fineCents(effectiveValue(kOsc2Fine)) * 0.01);
        break;
    case kOsc2Track:  p.osc2Track = v != 0; break;
    case kOscMix:     p.mix = x; break;
    case kOsc2Sync:   p.sync = v != 0; break;
    case kOsc2Ring:   p.ring = v != 0; break;
    case kOscPW:      p.duty = float(curves::pulseDuty(v)); break;
    case kOscKeyShift:p.keyShift = v; break;
    case kOsc1FM:     p.fm = x; break;
    case kOsc1Detune: p.osc1Detune = x; break;
    case kSubGain:    p.subGain = float(curves::subGain(v)); break;
    case kSubShape:   p.subWave = v; break;
    case kSubOct:     p.subOctDown = v == 0; break;
    case kOscPhase:   p.oscPhase = v; break;
    case kOscFine:    p.commonCents = float(curves::fineCents(v)); break;
    // Mod envelope
    case kModEnvOn:   p.modEnvOn = v != 0; break;
    case kModEnvAmt:  p.modEnvAmt = (v - 64) / 64.f; break;
    case kModEnvA:    p.modEnvA = curves::attackSeconds(v); envChanged = true; break;
    case kModEnvD:    p.modEnvD = curves::decaySeconds(v); envChanged = true; break;
    case kModEnvDest: p.modEnvDest = v; break;
    // Filter
    case kFltType:    p.fltType = v; break;
    case kFltA:       p.fltA = curves::attackSeconds(v); envChanged = true; break;
    case kFltD:       p.fltD = curves::decaySeconds(v); envChanged = true; break;
    case kFltS:       p.fltS = x; envChanged = true; break;
    case kFltR:       p.fltR = curves::decaySeconds(v); envChanged = true; break;
    case kFltFreq:    p.fltFreqIndex = v * 1023.f / 127.f; break;
    case kFltRes:     p.fltRes = x; break;
    case kFltAmt:     p.fltAmt = (v - 63) / 64.f; break;
    case kFltTrk:     p.fltTrk = x; break;
    case kFltSat:     p.fltSat = x; break;
    case kFltVel:     p.fltVel = v != 0; break;
    // Amp
    case kAmpA:       p.ampA = curves::attackSeconds(v); envChanged = true; break;
    case kAmpD:       p.ampD = curves::decaySeconds(v); envChanged = true; break;
    case kAmpS:       p.ampS = float(curves::ampLevel(v)); envChanged = true; break;
    case kAmpR:       p.ampR = curves::decaySeconds(v); envChanged = true; break;
    case kAmpGain:    p.ampGain = float(curves::ampLevel(v)); break;
    case kAmpVel:     p.ampVel = x; break;
    // Voice
    case kPortaTime:  p.portaSeconds = double(x) * x * 2.5; break;
    case kPbRange:    break;
    case kPlayMode:
    case kUniOn:
    case kUniVoices:
        for (auto& vc : mVoices) if (vc.active()) vc.fastRelease();
        for (auto& g : mGroups) g = Group{};
        mMonoGroup = -1;
        break;
    // LFOs
    case kLfo1On:    mLfo[0].setOn(v != 0); break;
    case kLfo1Type:  mLfo[0].setWave(v); break;
    case kLfo1Dest:  mLfo[0].setDest(v); break;
    case kLfo1Speed: mLfo[0].setSpeed(v); break;
    case kLfo1Depth: mLfo[0].setDepth(v); break;
    case kLfo1Tempo: mLfo[0].setTempoSync(v != 0); break;
    case kLfo1Key:   mLfo[0].setKeySync(v != 0); break;
    case kLfo2On:    mLfo[1].setOn(v != 0); break;
    case kLfo2Type:  mLfo[1].setWave(v); break;
    case kLfo2Dest:  mLfo[1].setDest(v); break;
    case kLfo2Speed: mLfo[1].setSpeed(v); break;
    case kLfo2Depth: mLfo[1].setDepth(v); break;
    case kLfo2Tempo: mLfo[1].setTempoSync(v != 0); break;
    case kLfo2Key:   mLfo[1].setKeySync(v != 0); break;
    // Arpeggiator
    case kArpOn:
        mArp.setOn(v != 0);
        for (auto& vc : mVoices) if (vc.active()) vc.noteOff();
        for (auto& g : mGroups) g.held = false;
        mMonoStack.clear();
        break;
    case kArpType:  mArp.setType(v); break;
    case kArpRange: mArp.setRange(v); break;
    case kArpBeat:  mArp.setBeat(v); break;
    case kArpGate:  mArp.setGate(v); break;
    // Effect
    case kFxOn:     mFx.setOn(v != 0); break;
    case kFxType:   mFx.setType(v); break;
    case kFxCtl1:   mFx.setCtl1(v); break;
    case kFxCtl2:   mFx.setCtl2(v); break;
    case kFxLevel:  mFx.setLevel(v); break;
    // EQ / pan
    case kEqFreq:   mEq.setFreq(v); break;
    case kEqLevel:  mEq.setLevel(v); break;
    case kEqQ:      mEq.setQ(v); break;
    case kEqTone:   mEq.setTone(v); break;
    case kPan:      mEq.setPan(v); break;
    // Delay
    case kDlyOn:     mDelay.setOn(v != 0); break;
    case kDlyType:   mDelay.setType(v); break;
    case kDlyTime:   mDelay.setTime(v); break;
    case kDlySpread: mDelay.setSpread(v); break;
    case kDlyFdbk:   mDelay.setFeedback(v); break;
    case kDlyTone:   mDelay.setTone(v); break;
    case kDlyDryWet: mDelay.setDryWet(v); break;
    // Chorus
    case kChoOn:    mChorus.setOn(v != 0); break;
    case kChoType:  mChorus.setType(v); break;
    case kChoTime:  mChorus.setTime(v); break;
    case kChoDepth: mChorus.setDepth(v); break;
    case kChoRate:  mChorus.setRate(v); break;
    case kChoFdbk:  mChorus.setFeedback(v); break;
    case kChoLevel: mChorus.setLevel(v); break;
    // Master
    case kMasterVolume: mMasterGain = x * x * 1.4f; break;
    default: break;
    }

    if (envChanged)
        for (auto& vc : mVoices) if (vc.active()) vc.applyEnvParams(p);
}

// ---------------------------------------------------------------------------
// MIDI controllers
// ---------------------------------------------------------------------------
void SynthEngine::setController(int cc, float value)
{
    if (cc < 0 || cc > 127) return;
    if (cc == 64) { setSustain(value >= 0.5f); return; }
    if (cc == 123) { mPending.push_back({ 0, NoteEvent::AllNotesOff, 0, 0.f }); return; }
    mCC[size_t(cc)] = value;
    for (int slot = 0; slot < 2; ++slot)
        if (midiSourceToCC(mBase[slot == 0 ? kMidiSrc1 : kMidiSrc2]) == cc) reapplyAssigned(slot);
}

void SynthEngine::setAftertouch(float value)
{
    mAftertouch = value;
    for (int slot = 0; slot < 2; ++slot)
        if (midiSourceToCC(mBase[slot == 0 ? kMidiSrc1 : kMidiSrc2]) == -1) reapplyAssigned(slot);
}

void SynthEngine::setPitchBend(float value)
{
    mBend = value;
    for (int slot = 0; slot < 2; ++slot)
        if (midiSourceToCC(mBase[slot == 0 ? kMidiSrc1 : kMidiSrc2]) == -2) reapplyAssigned(slot);
}

void SynthEngine::setSustain(bool down)
{
    if (mSustain == down) return;
    mSustain = down;
    mArp.setSustain(down);
    if (!down) {
        for (int n = 0; n < 128; ++n)
            if (mSustained[size_t(n)]) { mSustained[size_t(n)] = false; noteOff(n); }
    }
}

// ---------------------------------------------------------------------------
// Voice allocation
// ---------------------------------------------------------------------------
int SynthEngine::freeVoice()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (!mVoices[size_t(i)].active()) return i;
    return -1;
}

void SynthEngine::stealOldestGroup()
{
    // Prefer released groups, then the oldest held group.
    int best = -1;
    unsigned bestOrder = ~0u;
    bool bestReleased = false;
    for (int g = 0; g < kMaxVoices; ++g) {
        const Group& gr = mGroups[size_t(g)];
        if (gr.note < 0) continue;
        bool anyActive = false;
        for (auto& v : mVoices) if (v.active() && v.group() == g) { anyActive = true; break; }
        if (!anyActive) continue;
        const bool rel = !gr.held;
        if ((rel && !bestReleased) || (rel == bestReleased && gr.order < bestOrder)) {
            best = g; bestOrder = gr.order; bestReleased = rel;
        }
    }
    if (best < 0) return;
    for (auto& v : mVoices) if (v.group() == best && v.active()) { v.kill(); v.setGroup(-1); }
    mGroups[size_t(best)] = Group{};
}

void SynthEngine::startGroup(int note, float vel, bool retrigger, double glideFrom)
{
    const int uni = unisonCount();
    const int maxGroups = std::max(1, std::min(mBase[kPolyphony], kMaxVoices / uni));

    // Count sounding groups and steal if over the polyphony limit.
    auto soundingGroups = [&] {
        int c = 0;
        for (int g = 0; g < kMaxVoices; ++g) {
            if (mGroups[size_t(g)].note < 0) continue;
            for (auto& v : mVoices) if (v.active() && v.group() == g) { ++c; break; }
        }
        return c;
    };
    while (soundingGroups() >= maxGroups) {
        const int before = soundingGroups();
        stealOldestGroup();
        if (soundingGroups() == before) break;
    }

    int gid = -1;
    for (int g = 0; g < kMaxVoices; ++g) {
        bool used = mGroups[size_t(g)].note >= 0;
        if (used) {
            bool anyActive = false;
            for (auto& v : mVoices) if (v.active() && v.group() == g) { anyActive = true; break; }
            if (!anyActive) { mGroups[size_t(g)] = Group{}; used = false; }
        }
        if (!used) { gid = g; break; }
    }
    if (gid < 0) { stealOldestGroup(); gid = 0; }

    mGroups[size_t(gid)] = { note, true, ++mOrder };

    // Unison placement: symmetric detune, pan spread, optional pitch stacking.
    const float detSemis = float((std::pow(40.0, mBase[kUniDetune] / 127.0) - 1.0) / 39.0 * 0.6);
    const float spread = mBase[kUniSpread] < 64 ? (mBase[kUniSpread] - 64) / 64.f : (mBase[kUniSpread] - 64) / 63.f;
    const float uniPhase = mBase[kUniPhase] / 127.f;
    for (int k = 0; k < uni; ++k) {
        int vi = freeVoice();
        if (vi < 0) { stealOldestGroup(); vi = freeVoice(); }
        if (vi < 0) break;
        Voice::Placement pl;
        const float t = uni > 1 ? (float(k) / (uni - 1)) * 2.f - 1.f : 0.f;
        pl.detuneSemis = t * detSemis;
        pl.pan = t * spread;
        pl.pitchSemis = (uni > 1 && (k & 1)) ? float(mBase[kUniPitch] - 24) : 0.f;   // 24 = unison
        pl.phase = mBase[kUniPhase] > 0 ? uniPhase * float(k) / uni : 0.f;
        Voice& v = mVoices[size_t(vi)];
        v.setGroup(gid);
        v.noteOn(note, vel, pl, retrigger, glideFrom, mVp);
    }
}

void SynthEngine::releaseGroup(int group)
{
    if (group < 0) return;
    for (auto& v : mVoices) if (v.group() == group && v.active() && !v.released()) v.noteOff();
    mGroups[size_t(group)].held = false;
}

void SynthEngine::noteOn(int note, float vel)
{
    mSustained[size_t(note)] = false;
    for (auto& l : mLfo) if (mHeldCount == 0 || mBase[kPlayMode] == 0) l.keyTrigger();
    ++mHeldCount;

    const int mode = mBase[kPlayMode];
    const bool portaAuto = mBase[kPortaAuto] != 0;

    if (mode == 0) {   // poly
        // Retrigger the same note instead of stacking duplicates.
        for (int g = 0; g < kMaxVoices; ++g)
            if (mGroups[size_t(g)].note == note) releaseGroup(g);
        const bool legatoPlay = mHeldCount > 1;
        const double from = (mLastPitch >= 0.0 && (!portaAuto || legatoPlay)) ? mLastPitch : -1.0;
        startGroup(note, vel, true, from);
        mLastPitch = note;
        return;
    }

    // mono / legato: one group, last-note priority.
    mMonoStack.erase(std::remove(mMonoStack.begin(), mMonoStack.end(), note), mMonoStack.end());
    const bool wasHeld = !mMonoStack.empty();
    mMonoStack.push_back(note);

    const bool groupAlive = mMonoGroup >= 0 && [&] {
        for (auto& v : mVoices) if (v.active() && v.group() == mMonoGroup) return true;
        return false;
    }();
    const bool glide = mVp.portaSeconds > 0.0005 && (!portaAuto || wasHeld);

    if (mode == 2 && wasHeld && groupAlive) {
        // Legato: only the pitch moves, envelopes keep running.
        for (auto& v : mVoices) if (v.group() == mMonoGroup && v.active()) v.legatoTo(note, glide);
        mGroups[size_t(mMonoGroup)].note = note;
        mGroups[size_t(mMonoGroup)].held = true;
    } else {
        const double from = (glide && mLastPitch >= 0.0) ? mLastPitch : -1.0;
        if (groupAlive) {
            for (auto& v : mVoices) if (v.group() == mMonoGroup && v.active()) v.fastRelease();
            mGroups[size_t(mMonoGroup)] = Group{};
        }
        startGroup(note, vel, true, from);
        for (int g = 0; g < kMaxVoices; ++g)
            if (mGroups[size_t(g)].note == note && mGroups[size_t(g)].held) mMonoGroup = g;
    }
    mLastPitch = note;
}

void SynthEngine::noteOff(int note)
{
    if (mSustain) { mSustained[size_t(note)] = true; return; }
    mHeldCount = std::max(0, mHeldCount - 1);

    if (mBase[kPlayMode] == 0) {
        for (int g = 0; g < kMaxVoices; ++g)
            if (mGroups[size_t(g)].note == note && mGroups[size_t(g)].held) releaseGroup(g);
        return;
    }

    const bool wasTop = !mMonoStack.empty() && mMonoStack.back() == note;
    mMonoStack.erase(std::remove(mMonoStack.begin(), mMonoStack.end(), note), mMonoStack.end());
    if (!wasTop) return;

    if (!mMonoStack.empty() && mMonoGroup >= 0) {
        // Fall back to the previous held note (legato-style glide back).
        const int back = mMonoStack.back();
        const bool glide = mVp.portaSeconds > 0.0005;
        for (auto& v : mVoices) if (v.group() == mMonoGroup && v.active()) v.legatoTo(back, glide);
        mGroups[size_t(mMonoGroup)].note = back;
        mLastPitch = back;
    } else {
        releaseGroup(mMonoGroup);
    }
}

void SynthEngine::handleNote(const NoteEvent& e)
{
    switch (e.type) {
    case NoteEvent::NoteOn:  noteOn(e.note, e.velocity); break;
    case NoteEvent::NoteOff: noteOff(e.note); break;
    default:
        for (auto& v : mVoices) if (v.active()) v.noteOff();
        for (auto& g : mGroups) g.held = false;
        mMonoStack.clear();
        mHeldCount = 0;
        mArp.allNotesOff();
        break;
    }
}

int SynthEngine::activeVoiceCount() const
{
    int c = 0;
    for (auto& v : mVoices) if (v.active()) ++c;
    return c;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void SynthEngine::renderVoices(float* L, float* R, int n, const TransportInfo& t)
{
    int done = 0;
    while (done < n) {
        const int blk = std::min(kChunk, n - done);

        // Global LFOs -> shared modulation values.
        mVp.lfoPitchOsc2 = mVp.lfoPitchBoth = mVp.lfoFilter = mVp.lfoAmp = mVp.lfoPW = mVp.lfoFM = 0.f;
        mLfoPanNow = 0.f;
        for (auto& l : mLfo) {
            const float w = l.advance(blk, t.bpm);
            if (!l.on()) continue;
            const float m = l.modulation(w);
            switch (l.dest()) {
            case kDestOsc2:   mVp.lfoPitchOsc2 += m; break;
            case kDestOsc12:  mVp.lfoPitchBoth += m; break;
            case kDestFilter: mVp.lfoFilter += m; break;
            case kDestAmp:    mVp.lfoAmp += m; break;
            case kDestPW:     mVp.lfoPW += m; break;
            case kDestFM:     mVp.lfoFM += m; break;
            case kDestPan:    mLfoPanNow += m; break;
            default: break;
            }
        }
        mVp.pitchBend = mBend * float(mBase[kPbRange]);

        for (auto& v : mVoices)
            if (v.active()) v.render(L + done, R + done, blk, mVp);
        done += blk;
    }
}

void SynthEngine::process(float* outL, float* outR, int n, const std::vector<NoteEvent>& events,
                          const TransportInfo& t)
{
    std::memset(outL, 0, sizeof(float) * size_t(n));
    std::memset(outR, 0, sizeof(float) * size_t(n));

    // mPending holds all-notes-off requests from CC123 (see setController).
    std::vector<NoteEvent>& merged = mMerged;
    merged.clear();
    merged.insert(merged.end(), mPending.begin(), mPending.end());
    mPending.clear();

    if (mArp.on()) {
        for (const auto& e : events) {
            if (e.type == NoteEvent::NoteOn) mArp.noteOn(e.note, e.velocity);
            else if (e.type == NoteEvent::NoteOff) mArp.noteOff(e.note);
            else { mArp.allNotesOff(); merged.push_back(e); }
        }
        mArpEvents.clear();
        mArp.process(n, t.bpm, t.ppq, t.playing, mArpEvents);
        for (const auto& a : mArpEvents)
            merged.push_back({ a.offset, a.on ? NoteEvent::NoteOn : NoteEvent::NoteOff, a.note, a.velocity });
    } else {
        merged.insert(merged.end(), events.begin(), events.end());
    }
    std::stable_sort(merged.begin(), merged.end(),
                     [](const NoteEvent& a, const NoteEvent& b) { return a.offset < b.offset; });

    int pos = 0;
    for (const auto& e : merged) {
        const int at = clampv(e.offset, 0, n);
        if (at > pos) { renderVoices(outL + pos, outR + pos, at - pos, t); pos = at; }
        handleNote(e);
    }
    if (pos < n) renderVoices(outL + pos, outR + pos, n - pos, t);
    merged.clear();

    // Unison voices add up; keep headroom similar to Synth1's mixer.
    const float uniNorm = 1.f / std::sqrt(float(unisonCount()));
    const float g = 0.5f * uniNorm;
    for (int i = 0; i < n; ++i) { outL[i] *= g; outR[i] *= g; }

    mFx.process(outL, outR, n);
    mEq.process(outL, outR, n, mLfoPanNow);
    mDelay.process(outL, outR, n, t.bpm);
    mChorus.process(outL, outR, n);

    for (int i = 0; i < n; ++i) {
        outL[i] = mDc[0].process(outL[i]) * mMasterGain;
        outR[i] = mDc[1].process(outR[i]) * mMasterGain;
    }
}

} // namespace cs
