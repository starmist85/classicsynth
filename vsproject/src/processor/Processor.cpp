#include "processor/Processor.h"
#include "pluginids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace cs {

// ===========================================================================
// ComponentState
// ===========================================================================
bool ComponentState::read(IBStream* s)
{
    IBStreamer st(s, kLittleEndian);
    char magic[4];
    if (st.readRaw(magic, 4) != 4 || std::memcmp(magic, "CSst", 4) != 0) return false;
    int32 version = 0, nameLen = 0, count = 0;
    if (!st.readInt32(version) || version < 1) return false;
    if (!st.readInt32(nameLen) || nameLen < 0 || nameLen > 1024) return false;
    name.assign(size_t(nameLen), '\0');
    if (nameLen && st.readRaw(name.data(), nameLen) != nameLen) return false;
    for (int id = 0; id <= kMasterVolume; ++id)
        values[size_t(id)] = findParam(id) ? findParam(id)->defValue : 0;
    if (!st.readInt32(count) || count < 0 || count > 4096) return false;
    for (int32 i = 0; i < count; ++i) {
        int32 id = 0, v = 0;
        if (!st.readInt32(id) || !st.readInt32(v)) return false;
        if (const ParamDef* d = findParam(id)) values[size_t(id)] = clampValue(*d, v);
    }
    return true;
}

bool ComponentState::write(IBStream* s) const
{
    IBStreamer st(s, kLittleEndian);
    st.writeRaw("CSst", 4);
    st.writeInt32(1);
    st.writeInt32(int32(name.size()));
    if (!name.empty()) st.writeRaw(name.data(), int32(name.size()));
    int32 count = 0;
    for (int id = 0; id <= kMasterVolume; ++id) if (findParam(id)) ++count;
    st.writeInt32(count);
    for (int id = 0; id <= kMasterVolume; ++id) {
        if (!findParam(id)) continue;
        st.writeInt32(id);
        st.writeInt32(values[size_t(id)]);
    }
    return true;
}

// ===========================================================================
// Processor
// ===========================================================================
Processor::Processor()
{
    setControllerClass(kControllerUID);
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    mQpcToSeconds = 1.0 / double(f.QuadPart);
    for (int id = 0; id <= kMasterVolume; ++id) {
        const ParamDef* d = findParam(id);
        mValues[size_t(id)].store(d ? d->defValue : 0);
        mPending[size_t(id)].store(d ? d->defValue : 0);
    }
    mEvents.reserve(512);
    mHostCtrl[7] = 100.0 / 127.0;                              // same defaults as the controller's
    mHostCtrl[size_t(kMidiPitchBend - kMidiCCBase)] = 0.5;     // hidden MIDI parameters
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    const tresult r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 16);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement*, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(nullptr, 0, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    mSampleRate = setup.sampleRate;
    mEngine.setSampleRate(setup.sampleRate);
    mAudition.setSampleRate(setup.sampleRate);
    const size_t maxN = size_t(std::max(setup.maxSamplesPerBlock, 1));
    mScratchL.assign(maxN, 0.f);
    mScratchR.assign(maxN, 0.f);
    mAuditionL.assign(maxN, 0.f);
    mAuditionR.assign(maxN, 0.f);
    mAuditionEvents.reserve(4);
    return AudioEffect::setupProcessing(setup);
}

void Processor::renderAudition(float* L, float* R, int n)
{
    mAuditionEvents.clear();
    if (mAuditionPending.exchange(false)) {
        mAudition.reset();
        for (int id = 0; id < kNumPatchParams; ++id)
            if (findParam(id)) mAudition.setParam(id, mAuditionPatch[size_t(id)].load());
        mAudition.setParam(kArpOn, 0);    // audition plays a plain note
        mAudition.setParam(kMasterVolume, mValues[kMasterVolume].load());
        mAuditionEvents.push_back({ 0, NoteEvent::NoteOn, kAuditionNote, 0.8f });
        mAuditionRemaining = (long long)(kAuditionSeconds * mSampleRate);
        mAuditionActive = true;
    }
    if (!mAuditionActive) return;

    if (mAuditionRemaining >= 0 && mAuditionRemaining < n) {
        mAuditionEvents.push_back({ int(mAuditionRemaining), NoteEvent::NoteOff, kAuditionNote, 0.f });
        mAuditionRemaining = -1;
    } else if (mAuditionRemaining >= 0) {
        mAuditionRemaining -= n;
    }

    if (int(mAuditionL.size()) < n) { mAuditionL.resize(size_t(n)); mAuditionR.resize(size_t(n)); }
    TransportInfo t;   // free-running, 120 bpm
    mAudition.process(mAuditionL.data(), mAuditionR.data(), n, mAuditionEvents, t);

    float peak = 0.f;
    for (int i = 0; i < n; ++i) {
        L[i] += mAuditionL[size_t(i)];
        R[i] += mAuditionR[size_t(i)];
        peak = std::max(peak, std::fabs(mAuditionL[size_t(i)]) + std::fabs(mAuditionR[size_t(i)]));
    }
    // Stop once the note has ended and the tails (release, delay, chorus) are silent.
    if (mAuditionRemaining < 0 && mAudition.activeVoiceCount() == 0 && peak < 1e-5f) mAuditionActive = false;
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state) {
        mEngine.reset();
        mNoteSrc.fill(0);
    }
    return AudioEffect::setActive(state);
}

void Processor::applyParamChange(ParamID id, double normalized)
{
    if (id <= ParamID(kMasterVolume)) {
        if (const ParamDef* d = findParam(int(id))) {
            const int v = fromNormalized(*d, normalized);
            mValues[id].store(v, std::memory_order_relaxed);
            mEngine.setParam(int(id), v);
        }
        return;
    }
    if (id >= ParamID(kMidiCCBase) && id <= ParamID(kMidiPitchBend)) {
        const size_t i = size_t(id - kMidiCCBase);
        mHostCtrl[i] = normalized;
        if (mUiHold[i]) return;                       // the on-screen wheel owns it for now
        if (id == ParamID(kMidiCCBase + 123))         // all notes off clears host-held notes
            for (auto& s : mNoteSrc) s &= uint8(~kSrcHost);
        applyMidiCtrl(id, normalized);
    }
}

void Processor::applyMidiCtrl(ParamID id, double normalized)
{
    if (id < ParamID(kMidiCCBase + 128))
        mEngine.setController(int(id - kMidiCCBase), float(normalized));
    else if (id == ParamID(kMidiAftertouch))
        mEngine.setAftertouch(float(normalized));
    else if (id == ParamID(kMidiPitchBend))
        mEngine.setPitchBend(float(normalized * 2.0 - 1.0));
}

void Processor::sourceNoteOn(int offset, int note, float vel, uint8 src)
{
    if (note < 0 || note > 127) return;
    uint8& s = mNoteSrc[size_t(note)];
    const bool otherHolds = (s & ~src) != 0;
    s |= src;
    if (!otherHolds) mEvents.push_back({ offset, NoteEvent::NoteOn, note, vel });
}

void Processor::sourceNoteOff(int offset, int note, uint8 src)
{
    if (note < 0 || note > 127) return;
    uint8& s = mNoteSrc[size_t(note)];
    s &= uint8(~src);
    if (s == 0) mEvents.push_back({ offset, NoteEvent::NoteOff, note, 0.f });
}

void Processor::reportMeters(ProcessData& data, double elapsed, int n)
{
    if (n <= 0 || mSampleRate <= 0.0) return;
    const double load = elapsed / (double(n) / mSampleRate);
    // ~150 ms smoothing regardless of block size.
    const double a = std::min(1.0, (double(n) / mSampleRate) / 0.15);
    mCpuAvg += (load - mCpuAvg) * a;

    // Send to the controller roughly 20 times per second.
    mMeterCountdown -= n;
    if (mMeterCountdown > 0 || !data.outputParameterChanges) return;
    mMeterCountdown = int(mSampleRate / 20.0);

    int32 index = 0;
    if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(ParamID(kMeterCpu), index))
        q->addPoint(0, std::clamp(mCpuAvg, 0.0, 1.0), index);
    if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(ParamID(kMeterVoices), index))
        q->addPoint(0, (mEngine.activeVoiceCount() + mAudition.activeVoiceCount()) / 64.0, index);

    auto toNorm = [](float peak) {
        if (peak <= 1e-6f) return 0.0;
        const double db = 20.0 * std::log10(double(peak));
        return std::clamp((db + 60.0) / 66.0, 0.0, 1.0);
    };
    if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(ParamID(kMeterOutL), index))
        q->addPoint(0, toNorm(mPeakL), index);
    if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(ParamID(kMeterOutR), index))
        q->addPoint(0, toNorm(mPeakR), index);
    mPeakL = mPeakR = 0.f;
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    struct MeterScope {
        Processor& p; ProcessData& d; LARGE_INTEGER start;
        ~MeterScope()
        {
            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);
            p.reportMeters(d, double(t1.QuadPart - start.QuadPart) * p.mQpcToSeconds, d.numSamples);
        }
    } meterScope{ *this, data, t0 };

    if (mPanic.exchange(false)) {
        mEngine.reset();          // kills voices, clears arp, delay and chorus buffers
        mNoteSrc.fill(0);
        mAudition.reset();
        mAuditionActive = false;
        mAuditionPending.store(false);
        mAuditionRemaining = -1;
    }

    // Host state restore (setState) is applied here, on the audio thread.
    if (mPendingState.exchange(false)) {
        for (int id = 0; id <= kMasterVolume; ++id) {
            if (!findParam(id)) continue;
            const int v = mPending[size_t(id)].load();
            mValues[size_t(id)].store(v);
            mEngine.setParam(id, v);
        }
    }

    if (IParameterChanges* changes = data.inputParameterChanges) {
        const int32 n = changes->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q || q->getPointCount() <= 0) continue;
            int32 offset = 0;
            ParamValue value = 0;
            if (q->getPoint(q->getPointCount() - 1, offset, value) == kResultOk)
                applyParamChange(q->getParameterId(), value);
        }
    }

    mEvents.clear();
    drainUiEvents();
    if (IEventList* events = data.inputEvents) {
        const int32 n = events->getEventCount();
        for (int32 i = 0; i < n; ++i) {
            Event e{};
            if (events->getEvent(i, e) != kResultOk) continue;
            if (e.type == Event::kNoteOnEvent) {
                if (e.noteOn.velocity <= 0.f)
                    sourceNoteOff(e.sampleOffset, e.noteOn.pitch, kSrcHost);
                else
                    sourceNoteOn(e.sampleOffset, e.noteOn.pitch, e.noteOn.velocity, kSrcHost);
            } else if (e.type == Event::kNoteOffEvent) {
                sourceNoteOff(e.sampleOffset, e.noteOff.pitch, kSrcHost);
            }
        }
    }

    if (data.numOutputs == 0 || data.numSamples <= 0) return kResultOk;

    TransportInfo t;
    if (const ProcessContext* ctx = data.processContext) {
        if (ctx->state & ProcessContext::kTempoValid) t.bpm = ctx->tempo;
        if (ctx->state & ProcessContext::kProjectTimeMusicValid) t.ppq = ctx->projectTimeMusic;
        t.playing = (ctx->state & ProcessContext::kPlaying) != 0 && (ctx->state & ProcessContext::kProjectTimeMusicValid) != 0;
    }

    AudioBusBuffers& out = data.outputs[0];
    const int n = data.numSamples;
    float* L = out.numChannels > 0 ? out.channelBuffers32[0] : nullptr;
    float* R = out.numChannels > 1 ? out.channelBuffers32[1] : nullptr;
    if (!L) return kResultOk;
    if (!R) {
        if (int(mScratchR.size()) < n) mScratchR.resize(size_t(n));
        R = mScratchR.data();
    }

    mEngine.process(L, R, n, mEvents, t);
    renderAudition(L, R, n);

    // Output peak for the level meter; report silence so hosts can skip work.
    float pl = mPeakL, pr = mPeakR;
    for (int i = 0; i < n; ++i) {
        pl = std::max(pl, std::fabs(L[i]));
        pr = std::max(pr, std::fabs(R[i]));
    }
    const bool silent = pl == 0.f && pr == 0.f && mPeakL == 0.f && mPeakR == 0.f;
    mPeakL = pl;
    mPeakR = pr;
    out.silenceFlags = silent ? 0x3 : 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    ComponentState cs;
    if (!cs.read(state)) return kResultFalse;
    for (int id = 0; id <= kMasterVolume; ++id) mPending[size_t(id)].store(cs.values[size_t(id)]);
    {
        std::lock_guard<std::mutex> lock(mNameLock);
        mProgramName = cs.name;
    }
    mPendingState.store(true);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    ComponentState cs;
    for (int id = 0; id <= kMasterVolume; ++id)
        cs.values[size_t(id)] = mPendingState.load() ? mPending[size_t(id)].load() : mValues[size_t(id)].load();
    {
        std::lock_guard<std::mutex> lock(mNameLock);
        cs.name = mProgramName;
    }
    return cs.write(state) ? kResultOk : kResultFalse;
}

void Processor::pushUiEvent(const UiEvent& e)
{
    std::lock_guard<std::mutex> lock(mUiPushLock);
    const uint32 w = mUiWrite.load(std::memory_order_relaxed);
    if (w - mUiRead.load(std::memory_order_acquire) >= kUiQueueSize) {   // audio stalled: drop, recover later
        mUiOverflow.store(true, std::memory_order_release);
        return;
    }
    mUiQueue[w % kUiQueueSize] = e;
    mUiWrite.store(w + 1, std::memory_order_release);
}

// UI notes go in at offset 0, ahead of the host's events for this block.
void Processor::drainUiEvents()
{
    uint32 r = mUiRead.load(std::memory_order_relaxed);
    const uint32 w = mUiWrite.load(std::memory_order_acquire);
    for (; r != w; ++r) {
        const UiEvent& e = mUiQueue[r % kUiQueueSize];
        if (e.kind == UiEvent::Note) {
            if (e.value > 0.f) sourceNoteOn(0, int(e.id), e.value, kSrcUi);
            else               sourceNoteOff(0, int(e.id), kSrcUi);
            continue;
        }
        const size_t i = size_t(e.id - kMidiCCBase);
        if (e.held || e.id != kMidiPitchBend) {
            applyMidiCtrl(ParamID(e.id), double(e.value));
            if (!e.held) mHostCtrl[i] = e.value;      // a released mod wheel stays where it was left
        } else {
            applyMidiCtrl(ParamID(e.id), mHostCtrl[i]);   // released pitch wheel: back to the host's bend
        }
        mUiHold[i] = e.held;
    }
    mUiRead.store(r, std::memory_order_release);

    // A dropped note-off would leave a note stuck, a dropped wheel release would
    // lock out host MIDI: release every note and controller the UI holds.
    if (mUiOverflow.exchange(false, std::memory_order_acq_rel)) {
        for (int k = 0; k < 128; ++k)
            if (mNoteSrc[size_t(k)] & kSrcUi) sourceNoteOff(0, k, kSrcUi);
        for (int i = 0; i < kNumMidiCtrl; ++i)
            if (mUiHold[size_t(i)]) {
                mUiHold[size_t(i)] = false;
                applyMidiCtrl(ParamID(kMidiCCBase + i), mHostCtrl[size_t(i)]);
            }
    }
}

tresult PLUGIN_API Processor::notify(IMessage* message)
{
    if (message && std::strcmp(message->getMessageID(), kMsgNote) == 0) {
        int64 pitch = 0;
        double vel = 0.0;
        if (message->getAttributes()->getInt(kMsgAttrPitch, pitch) == kResultOk &&
            message->getAttributes()->getFloat(kMsgAttrVelocity, vel) == kResultOk && pitch >= 0 && pitch < 128)
            pushUiEvent({ UiEvent::Note, int32(pitch), float(vel) });
        return kResultOk;
    }
    if (message && std::strcmp(message->getMessageID(), kMsgMidiCtrl) == 0) {
        int64 id = 0;
        double v = 0.0;
        int64 held = 0;
        message->getAttributes()->getInt(kMsgAttrHeld, held);
        if (message->getAttributes()->getInt(kMsgAttrId, id) == kResultOk &&
            message->getAttributes()->getFloat(kMsgAttrValue, v) == kResultOk &&
            id >= kMidiCCBase && id <= kMidiPitchBend)
            pushUiEvent({ UiEvent::Ctrl, int32(id), float(std::clamp(v, 0.0, 1.0)), held != 0 });
        return kResultOk;
    }
    if (message && std::strcmp(message->getMessageID(), kMsgPanic) == 0) {
        mPanic.store(true);
        return kResultOk;
    }
    if (message && std::strcmp(message->getMessageID(), kMsgAudition) == 0) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary(kMsgAttrValues, data, size) == kResultOk &&
            size >= sizeof(int32) * kNumPatchParams) {
            const int32* v = static_cast<const int32*>(data);
            for (int id = 0; id < kNumPatchParams; ++id) mAuditionPatch[size_t(id)].store(v[id]);
            mAuditionPending.store(true);
        }
        return kResultOk;
    }
    if (message && std::strcmp(message->getMessageID(), kMsgProgramName) == 0) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary(kMsgAttrName, data, size) == kResultOk) {
            std::lock_guard<std::mutex> lock(mNameLock);
            mProgramName.assign(static_cast<const char*>(data), size);
        }
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

} // namespace cs
