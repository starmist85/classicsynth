#pragma once
// ClassicSynth engine: a re-implementation of Synth1's CSynthesizer1.
//
// Signal flow (from the CSynthesizer1 constructor @0x180025110):
//   Arpeggiator -> 32 voices -> mixer -> Effect -> EQ -> Tone -> Pan -> Delay -> Chorus -> out
// Parameter application follows CSynthesizer1::setParameter (@0x180028fe0),
// including the two "midi ctrl" slots that add a scaled controller value to
// any parameter (src 86/88, sens 50/51, assign 87/89).

#include "dsp/Arpeggiator.h"
#include "dsp/Effects.h"
#include "dsp/Lfo.h"
#include "dsp/Voice.h"
#include "params/ParamTable.h"

#include <array>
#include <vector>

namespace cs {

struct NoteEvent {
    enum Type : uint8_t { NoteOn, NoteOff, AllNotesOff };
    int   offset = 0;
    Type  type = NoteOn;
    int   note = 60;
    float velocity = 1.f;
};

struct TransportInfo {
    double bpm = 120.0;
    double ppq = 0.0;          // musical position at block start (quarter notes)
    bool   playing = false;
};

class SynthEngine {
public:
    static constexpr int kMaxVoices = 32;

    SynthEngine();

    void setSampleRate(double fs);
    void reset();

    // Raw patch value (0..127 style, see ParamTable). Thread: audio.
    void setParam(int id, int value);
    int  param(int id) const { return (id >= 0 && id <= kMasterVolume) ? mBase[id] : 0; }

    // MIDI controllers. cc value 0..1, pitch bend -1..1.
    void setController(int cc, float value);
    void setAftertouch(float value);
    void setPitchBend(float value);
    void setSustain(bool down);

    void process(float* outL, float* outR, int n, const std::vector<NoteEvent>& events, const TransportInfo& t);

    int activeVoiceCount() const;

private:
    void apply(int id, int value);           // effective value -> engine state
    void reapplyAssigned(int slot);
    int  effectiveValue(int id) const;
    float sourceValue(int slot) const;

    void handleNote(const NoteEvent& e);
    void noteOn(int note, float vel);
    void noteOff(int note);
    void startGroup(int note, float vel, bool retrigger, double glideFrom);
    void releaseGroup(int group);
    int  freeVoice();
    void stealOldestGroup();
    int  unisonCount() const { return mBase[kUniOn] ? mBase[kUniVoices] : 1; }

    void renderVoices(float* L, float* R, int n, const TransportInfo& t);

    double mFs = 44100.0;
    std::array<int, kMasterVolume + 1> mBase{};

    VoiceParams mVp;
    std::array<Voice, kMaxVoices> mVoices;
    Lfo mLfo[2];
    Arpeggiator mArp;
    EffectUnit mFx;
    EqualizerPan mEq;
    TempoDelay mDelay;
    Chorus mChorus;
    DcBlocker mDc[2];

    // MIDI state
    std::array<float, 128> mCC{};
    float mAftertouch = 0.f, mBend = 0.f;
    bool  mSustain = false;
    std::array<bool, 128> mSustained{};

    // Note bookkeeping
    struct Group { int note = -1; bool held = false; unsigned order = 0; };
    std::array<Group, kMaxVoices> mGroups{};
    unsigned mOrder = 0;
    std::vector<int> mMonoStack;             // held notes, last = most recent
    int    mMonoGroup = -1;
    double mLastPitch = -1.0;
    int    mHeldCount = 0;

    float mMasterGain = 0.9f;
    float mLfoPanNow = 0.f;
    std::vector<ArpEvent> mArpEvents;
    std::vector<NoteEvent> mMerged;
    std::vector<NoteEvent> mPending;
};

} // namespace cs
