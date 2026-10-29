#pragma once

#include "dsp/SynthEngine.h"
#include "presets/Patch.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace cs {

// Component state layout (little endian), shared with the controller:
//   char[4] "CSst", int32 version (1), int32 nameLen, char name[nameLen],
//   int32 count, count x { int32 id, int32 value }
struct ComponentState {
    std::string name = "init";
    std::array<int, kMasterVolume + 1> values{};
    bool read(Steinberg::IBStream* s);
    bool write(Steinberg::IBStream* s) const;
};

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor()); }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

private:
    void applyParamChange(Steinberg::Vst::ParamID id, double normalized);

    SynthEngine mEngine;
    std::vector<NoteEvent> mEvents;
    std::vector<float> mScratchL, mScratchR;

    // Values as last set (for getState). Written on the audio thread.
    std::array<std::atomic<int>, kMasterVolume + 1> mValues;
    // State pushed from setState(), applied at the start of the next process().
    std::array<std::atomic<int>, kMasterVolume + 1> mPending;
    std::atomic<bool> mPendingState{ false };
    std::atomic<bool> mPanic{ false };

    // CPU meter: process() time vs. real-time budget of the block.
    void reportMeters(Steinberg::Vst::ProcessData& data, double elapsedSeconds, int n);
    double mQpcToSeconds = 0.0;
    double mCpuAvg = 0.0;
    int    mMeterCountdown = 0;
    float  mPeakL = 0.f, mPeakR = 0.f;    // output peak since the last meter report

    std::mutex mNameLock;
    std::string mProgramName = "init";

    // Preset audition: a private engine plays C3 with the clicked browser patch,
    // mixed into the output, without touching the host-visible parameters.
    void renderAudition(float* L, float* R, int n);
    SynthEngine mAudition;
    std::array<std::atomic<int>, kNumPatchParams> mAuditionPatch;
    std::atomic<bool> mAuditionPending{ false };
    std::vector<float> mAuditionL, mAuditionR;
    std::vector<NoteEvent> mAuditionEvents;
    long long mAuditionRemaining = -1;   // samples until note-off
    bool mAuditionActive = false;
    double mSampleRate = 44100.0;

    // On-screen keyboard / wheels: messages from the controller, drained at the
    // start of process(). Producers (notify, any thread) serialize on a lock the
    // audio thread never takes; the audio thread is the single consumer.
    struct UiEvent {
        enum Kind : Steinberg::int32 { Note, Ctrl } kind = Note;
        Steinberg::int32 id = 0;     // note number or parameter ID
        float value = 0.f;           // velocity (0 = off) or normalized value
        bool held = false;           // Ctrl: the wheel is being dragged
    };
    static constexpr Steinberg::uint32 kUiQueueSize = 1024;
    void pushUiEvent(const UiEvent& e);
    void drainUiEvents();
    std::mutex mUiPushLock;
    std::array<UiEvent, kUiQueueSize> mUiQueue{};
    std::atomic<Steinberg::uint32> mUiWrite{ 0 }, mUiRead{ 0 };
    std::atomic<bool> mUiOverflow{ false };   // events were dropped: release the UI's notes

    // Host MIDI and the on-screen keyboard share the engine. A note held by
    // both sources starts once and ends when the last source lets go, so one
    // source's note-off never cuts the other's note. Audio thread only.
    enum : Steinberg::uint8 { kSrcHost = 1, kSrcUi = 2 };
    std::array<Steinberg::uint8, 128> mNoteSrc{};
    void sourceNoteOn(int offset, int note, float vel, Steinberg::uint8 src);
    void sourceNoteOff(int offset, int note, Steinberg::uint8 src);

    // MIDI controller inputs (CC 0..127, aftertouch, pitch bend; index = id - kMidiCCBase).
    // While an on-screen wheel is held it owns its controller: host values are
    // remembered, not applied, and the pitch wheel springs back to the host's value.
    static constexpr int kNumMidiCtrl = 130;
    std::array<bool, kNumMidiCtrl> mUiHold{};
    std::array<double, kNumMidiCtrl> mHostCtrl{};
    void applyMidiCtrl(Steinberg::Vst::ParamID id, double normalized);
};

} // namespace cs
