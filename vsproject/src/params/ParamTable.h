#pragma once
// Parameter table recovered from Synth1 V1.13 (x64) at .data:0x180093f10
// (98 entries x 40 bytes: id, min, max, flags, long name, short name) plus the
// per-patch polyphony (id 94) that only exists in the patch format.
//
// Parameter IDs are identical to the Synth1 patch IDs used in .sy1 files and in
// the VST2 chunk, so presets map 1:1. VST3 parameter IDs use the same numbers.

#include <cstdint>
#include <string>

namespace cs {

enum class Kind : uint8_t {
    Knob,      // 0..127 continuous-ish, unipolar
    Bipolar,   // 0..127 with centre at 64
    Switch,    // 0/1
    List,      // enumerated, labels[]
    Int        // integer range with numeric display
};

struct ParamDef {
    int         id;
    const char* name;       // long name (host-visible)
    const char* shortName;  // label under the control
    int         minValue;
    int         maxValue;
    int         defValue;
    Kind        kind;
    const char* const* labels; // for Kind::List, indexed by (value - minValue)
    bool        automatable;
};

// Synth1 patch parameter IDs (do not renumber; they are the file format).
enum ParamId : int {
    kOsc1Shape = 0, kOsc2Shape = 1, kOsc2Pitch = 2, kOsc2Fine = 3, kOsc2Track = 4,
    kOscMix = 5, kOsc2Sync = 6, kOsc2Ring = 7, kOscPW = 8, kOscKeyShift = 9,
    kModEnvOn = 10, kModEnvAmt = 11, kModEnvA = 12, kModEnvD = 13,
    kFltType = 14, kFltA = 15, kFltD = 16, kFltS = 17, kFltR = 18,
    kFltFreq = 19, kFltRes = 20, kFltAmt = 21, kFltTrk = 22, kFltSat = 23, kFltVel = 24,
    kAmpA = 25, kAmpD = 26, kAmpS = 27, kAmpR = 28, kAmpGain = 29, kAmpVel = 30,
    kArpType = 31, kArpRange = 32, kArpBeat = 33, kArpGate = 34,
    kDlyTime = 35, kDlyFdbk = 36, kDlyDryWet = 37,
    kPlayMode = 38, kPortaTime = 39, kPbRange = 40,
    kLfo1Dest = 41, kLfo1Type = 42, kLfo1Speed = 43, kLfo1Depth = 44,
    kOsc1FM = 45,
    kLfo2Dest = 46, kLfo2Type = 47, kLfo2Speed = 48, kLfo2Depth = 49,
    kMidiSens1 = 50, kMidiSens2 = 51,
    kChoTime = 52, kChoDepth = 53, kChoRate = 54, kChoFdbk = 55, kChoLevel = 56,
    kLfo1On = 57, kLfo2On = 58, kArpOn = 59,
    kEqTone = 60, kEqFreq = 61, kEqLevel = 62, kEqQ = 63,
    kChoType = 64, kDlyOn = 65, kChoOn = 66,
    kLfo1Tempo = 67, kLfo1Key = 68, kLfo2Tempo = 69, kLfo2Key = 70,
    kModEnvDest = 71, kOscFine = 72,
    kUniOn = 73, kPortaAuto = 74, kUniDetune = 75, kOsc1Detune = 76,
    kFxOn = 77, kFxType = 78, kFxCtl1 = 79, kFxCtl2 = 80, kFxLevel = 81,
    kDlyType = 82, kDlySpread = 83, kUniSpread = 84, kUniPitch = 85,
    kMidiSrc1 = 86, kMidiAssign1 = 87, kMidiSrc2 = 88, kMidiAssign2 = 89,
    kPan = 90, kOscPhase = 91, kUniPhase = 92, kUniVoices = 93, kPolyphony = 94,
    kSubGain = 95, kSubShape = 96, kSubOct = 97, kDlyTone = 98,

    kNumPatchParams = 99,

    // Plugin-level parameters (not stored in Synth1 patches).
    kMasterVolume = 100,

    // Hidden MIDI controller parameters (fed by IMidiMapping).
    kMidiCCBase      = 1000,  // 1000 + CC number (0..127)
    kMidiAftertouch  = 1128,
    kMidiPitchBend   = 1129,

    // Read-only meters sent from processor to controller (output parameters).
    kMeterCpu        = 1200,  // DSP load, normalized 0..1 = 0..100 % of the block budget
    kMeterVoices     = 1201,  // active voices / 64 (engine + audition)
    kMeterOutL       = 1202,  // output peak, normalized dB: 0 = -60 dB, 1 = +6 dB
    kMeterOutR       = 1203,
};

// MIDI controller sources for params 86/88. The engine/UI use an index
// (0 none, 1..11 CC1..CC11, 12 channel aftertouch, 13 pitch bend); Synth1 files
// store the MIDI status code instead: (status << 8) | cc, e.g. 0xB001 = mod wheel,
// 0xD000 = aftertouch, 0xE000 = pitch bend, 0 = none (FUN_180037660).
inline constexpr int kNumMidiSources = 14;
extern const char* const kMidiSourceNames[kNumMidiSources];
int midiSourceToCC(int sourceIndex);      // CC#, -1 = aftertouch, -2 = pitch bend, -3 = none
int midiSourceFromSynth1Code(int code);   // file code -> index
int midiSourceToSynth1Code(int index);    // index -> file code

const ParamDef* findParam(int id);
const ParamDef* paramAt(int index);       // iterate 0..numHostParams()-1
int  numHostParams();                     // patch params + master volume

inline int clampValue(const ParamDef& d, int v) {
    return v < d.minValue ? d.minValue : (v > d.maxValue ? d.maxValue : v);
}
inline double toNormalized(const ParamDef& d, int v) {
    const int span = d.maxValue - d.minValue;
    return span > 0 ? double(clampValue(d, v) - d.minValue) / span : 0.0;
}
inline int fromNormalized(const ParamDef& d, double n) {
    const int span = d.maxValue - d.minValue;
    int v = d.minValue + int(n * span + 0.5);
    return clampValue(d, v);
}

} // namespace cs
