#include "params/ParamTable.h"

#include <array>

namespace cs {

// Option labels recovered from the Synth1 GUI string pool (.rdata 0x180076090..).
static const char* const kOsc1Shapes[]  = { "sine", "tri", "saw", "pulse" };
static const char* const kOsc2Shapes[]  = { "tri", "saw", "pulse", "noise" };          // value 1..4
static const char* const kSubShapes[]   = { "sine", "tri", "saw", "square" };
static const char* const kSubOctLabels[] = { "-1 oct", "0 oct" };
static const char* const kModDest[]     = { "osc2", "FM", "p/w" };
static const char* const kFltTypes[]    = { "LP12", "LP24", "HP12", "BP12", "LPDL" };
static const char* const kLfoShapes[]   = { "saw", "tri", "square", "S&H", "smooth", "sine" };
static const char* const kLfoDests[]    = { "osc2", "osc1,2", "filter", "amp", "p/w", "FM", "pan" }; // 1..7
static const char* const kArpTypes[]    = { "updown", "up", "down", "random" };        // 1..4
static const char* const kArpRanges[]   = { "1 oct", "2 oct", "3 oct", "4 oct" };
static const char* const kPlayModes[]   = { "poly", "mono", "legato" };
static const char* const kFxTypes[]     = { "a.d.1", "a.d.2", "d.d.", "deci.", "r.m.",
                                            "comp.", "ph.1", "ph.2", "ph.3", "ph.4" };
static const char* const kDelayTypes[]  = { "ST", "X", "PP" };
static const char* const kChorusTypes[] = { "x1", "x2", "x3", "x4" };                  // 1..4
// Tempo-synced lengths: (1)=whole note, '+'=add, '/3'=triplet (Synth1 notation).
static const char* const kArpBeats[] = {
    "(1)", "(2)+(4)+(8)", "(2)+(4)", "(2)", "(4)+(8)+(16)", "(4)+(8)", "(1) /3", "(4)",
    "(8)+(16)+(32)", "(8)+(16)", "(2) /3", "(8)", "(16)+(32)", "(4) /3", "(16)",
    "(8) /3", "(32)", "(16) /3", "(32) /3" };
static const char* const kDelayTimes[] = {
    "0.1 ms", "(32) /3", "(16) /3", "(32)", "(8) /3", "(16)", "(4) /3", "(16)+(32)",
    "(8)", "(2) /3", "(8)+(16)", "(8)+(16)+(32)", "(4)", "(1) /3", "(4)+(8)",
    "(4)+(8)+(16)", "(2)", "(2)+(4)", "(2)+(4)+(8)", "(1)" };

const char* const kMidiSourceNames[kNumMidiSources] = {
    "none", "#1 mod wheel", "#2 breath", "#3 ctrl 3", "#4 foot", "#5 porta time", "#6 data entry",
    "#7 volume", "#8 balance", "#9 ctrl 9", "#10 pan", "#11 expression",
    "chan a.touch", "pitch bend" };

int midiSourceToCC(int s)
{
    if (s >= 1 && s <= 11) return s;
    if (s == 12) return -1;
    if (s == 13) return -2;
    return -3;
}

int midiSourceFromSynth1Code(int code)
{
    const int status = (code >> 8) & 0xF0, cc = code & 0xFF;
    if (status == 0xB0 && cc >= 1 && cc <= 11) return cc;
    if (status == 0xD0) return 12;
    if (status == 0xE0) return 13;
    return 0;
}

int midiSourceToSynth1Code(int index)
{
    if (index >= 1 && index <= 11) return 0xB000 | index;
    if (index == 12) return 0xD000;
    if (index == 13) return 0xE000;
    return 0;
}

#define P(id, name, sn, mn, mx, def, kind, labels) { id, name, sn, mn, mx, def, Kind::kind, labels, true }
#define H(id, name, sn, mn, mx, def, kind, labels) { id, name, sn, mn, mx, def, Kind::kind, labels, false }

// Default values form the "init" patch: saw osc, open LP24, organ-style amp.
static const ParamDef kParams[] = {
    // --- Oscillators
    P(kOsc1Shape,   "osc1 shape",          "1",        0,   3,   2, List,    kOsc1Shapes),
    P(kOsc1FM,      "osc1 FM",             "FM",       0, 127,   0, Knob,    nullptr),
    P(kOsc1Detune,  "osc1 detune",         "det",      0, 127,   0, Knob,    nullptr),
    P(kSubGain,     "osc1 sub gain",       "sub",      0, 127,   0, Knob,    nullptr),
    P(kSubShape,    "osc1 sub shape",      "sub wave", 0,   3,   0, List,    kSubShapes),
    P(kSubOct,      "osc1 sub octave",     "sub oct",  0,   1,   0, List,    kSubOctLabels),
    P(kOsc2Shape,   "osc2 shape",          "2",        1,   4,   2, List,    kOsc2Shapes),
    P(kOsc2Sync,    "osc2 sync",           "sync",     0,   1,   0, Switch,  nullptr),
    P(kOsc2Ring,    "osc2 ring modulation","ring",     0,   1,   0, Switch,  nullptr),
    P(kOsc2Track,   "osc2 kbd track",      "track",    0,   1,   1, Switch,  nullptr),
    P(kOsc2Pitch,   "osc2 pitch",          "pitch",    0, 127,  64, Bipolar, nullptr),
    P(kOsc2Fine,    "osc2 fine tune",      "fine",     0, 127,  64, Bipolar, nullptr),
    P(kModEnvOn,    "osc mod env on/off",  "m.env",    0,   1,   0, Switch,  nullptr),
    P(kModEnvA,     "osc mod env attack",  "A",        0, 127,   0, Knob,    nullptr),
    P(kModEnvD,     "osc mod env decay",   "D",        0, 127,  64, Knob,    nullptr),
    P(kModEnvAmt,   "osc mod env amount",  "amt",      0, 127,  64, Bipolar, nullptr),
    P(kModEnvDest,  "osc mod dest",        "dest.",    0,   2,   0, List,    kModDest),
    P(kOscKeyShift, "osc key shift",       "key shift",-24, 24,  0, Int,     nullptr),
    P(kOscMix,      "osc mix",             "mix",      0, 127,  64, Bipolar, nullptr),
    P(kOscPW,       "osc pulse width",     "p/w",      0, 127,  64, Bipolar, nullptr),
    P(kOscPhase,    "osc phase shift",     "phase",    0, 127,   0, Knob,    nullptr),
    P(kOscFine,     "osc1,2 fine tune",    "tune",     0, 127,  64, Bipolar, nullptr),
    // --- Filter
    P(kFltType,     "filter type",         "type",     0,   4,   1, List,    kFltTypes),
    P(kFltA,        "filter attack",       "A",        0, 127,   0, Knob,    nullptr),
    P(kFltD,        "filter decay",        "D",        0, 127,  64, Knob,    nullptr),
    P(kFltS,        "filter sustain",      "S",        0, 127,  64, Knob,    nullptr),
    P(kFltR,        "filter release",      "R",        0, 127,  32, Knob,    nullptr),
    P(kFltFreq,     "filter freq",         "frq",      0, 127, 100, Knob,    nullptr),
    P(kFltRes,      "filter resonance",    "res",      0, 127,   0, Knob,    nullptr),
    P(kFltAmt,      "filter amount",       "amt",      0, 127,  64, Bipolar, nullptr),
    P(kFltSat,      "filter saturation",   "sat",      0, 127,   0, Knob,    nullptr),
    P(kFltTrk,      "filter kbd track",    "trk",      0, 127,  64, Knob,    nullptr),
    P(kFltVel,      "filter velocity switch","vel",    0,   1,   0, Switch,  nullptr),
    // --- Amplifier
    P(kAmpA,        "amp attack",          "A",        0, 127,   0, Knob,    nullptr),
    P(kAmpD,        "amp decay",           "D",        0, 127,  64, Knob,    nullptr),
    P(kAmpS,        "amp sustain",         "S",        0, 127, 127, Knob,    nullptr),
    P(kAmpR,        "amp release",         "R",        0, 127,  32, Knob,    nullptr),
    P(kAmpGain,     "amp gain",            "gain",     0, 127, 100, Knob,    nullptr),
    P(kAmpVel,      "amp velocity sens",   "vel",      0, 127,  64, Knob,    nullptr),
    // --- Effect
    P(kFxOn,        "effect on/off",       "ON",       0,   1,   0, Switch,  nullptr),
    P(kFxType,      "effect type",         "type",     0,   9,   0, List,    kFxTypes),
    P(kFxCtl1,      "effect control1",     "ctl1",     0, 127,  64, Knob,    nullptr),
    P(kFxCtl2,      "effect control2",     "ctl2",     0, 127,  64, Knob,    nullptr),
    P(kFxLevel,     "effect level/mix",    "level",    0, 127,  64, Knob,    nullptr),
    // --- Equalizer / Pan
    P(kEqFreq,      "equalizer freq",      "freq",     0, 127,  64, Knob,    nullptr),
    P(kEqLevel,     "equalizer level",     "level",    0, 127,  64, Bipolar, nullptr),
    P(kEqQ,         "equalizer Q",         "Q",        0, 127,  64, Knob,    nullptr),
    P(kEqTone,      "equalizer tone",      "tone",     0, 127,  64, Bipolar, nullptr),
    P(kPan,         "pan",                 "L-R",      0, 127,  64, Bipolar, nullptr),
    // --- Tempo Delay
    P(kDlyOn,       "delay on/off",        "ON",       0,   1,   0, Switch,  nullptr),
    P(kDlyType,     "delay type",          "type",     0,   2,   0, List,    kDelayTypes),
    P(kDlyTime,     "delay time",          "time",     0,  19,  12, List,    kDelayTimes),
    P(kDlySpread,   "delay time spread",   "sprd",     0, 127,  64, Bipolar, nullptr),
    P(kDlyFdbk,     "delay feedback",      "fdbk",     0, 127,  40, Knob,    nullptr),
    P(kDlyTone,     "delay tone",          "tone",     0, 127,  64, Bipolar, nullptr),
    P(kDlyDryWet,   "delay dry/wet",       "d/w",      0, 127,  32, Knob,    nullptr),
    // --- Chorus / Flanger
    P(kChoOn,       "chorus on/off",       "ON",       0,   1,   0, Switch,  nullptr),
    P(kChoType,     "chorus type",         "type",     1,   4,   2, List,    kChorusTypes),
    P(kChoTime,     "chorus delay time",   "time",     0, 127,  64, Knob,    nullptr),
    P(kChoDepth,    "chorus depth",        "depth",    0, 127,  64, Knob,    nullptr),
    P(kChoRate,     "chorus rate",         "rate",     0, 127,  48, Knob,    nullptr),
    P(kChoFdbk,     "chorus feedback",     "fdbk",     0, 127,  64, Bipolar, nullptr),
    P(kChoLevel,    "chorus level",        "level",    0, 127,  64, Knob,    nullptr),
    // --- Voice / Play mode
    P(kPlayMode,    "play mode type",      "mode",     0,   2,   0, List,    kPlayModes),
    P(kPortaTime,   "portament time",      "porta",    0, 127,   0, Knob,    nullptr),
    P(kPortaAuto,   "portament auto mode", "auto",     0,   1,   0, Switch,  nullptr),
    P(kUniOn,       "unison mode",         "unison",   0,   1,   0, Switch,  nullptr),
    P(kUniVoices,   "unison voice num",    "num",      2,   8,   2, Int,     nullptr),
    P(kUniDetune,   "unison detune",       "det",      0, 127,  32, Knob,    nullptr),
    P(kUniPhase,    "unison phase shift",  "phase",    0, 127,   0, Knob,    nullptr),
    P(kUniSpread,   "unison pan spread",   "sprd",     0, 127,  64, Bipolar, nullptr),
    P(kUniPitch,    "unison pitch",        "pitch",    0,  48,  24, Int,     nullptr),   // 24 = 0 st
    P(kPolyphony,   "polyphony",           "poly",     1,  32,  16, Int,     nullptr),
    P(kPbRange,     "pitch bend range",    "p.b.",     0,  24,   5, Int,     nullptr),
    // --- Arpeggiator
    P(kArpOn,       "arpeggiator on/off",  "ON",       0,   1,   0, Switch,  nullptr),
    P(kArpType,     "arpeggiator type",    "type",     1,   4,   2, List,    kArpTypes),
    P(kArpRange,    "arpeggiator oct range","range",   0,   3,   0, List,    kArpRanges),
    P(kArpBeat,     "arpeggiator beat",    "beat",     0,  18,  14, List,    kArpBeats),
    P(kArpGate,     "arpeggiator gate",    "gate",     0, 127, 100, Knob,    nullptr),
    // --- LFO 1
    P(kLfo1On,      "lfo1 on/off",         "1",        0,   1,   1, Switch,  nullptr),
    P(kLfo1Type,    "lfo1 type",           "wave",     0,   5,   1, List,    kLfoShapes),
    P(kLfo1Dest,    "lfo1 destination",    "dst",      1,   7,   1, List,    kLfoDests),
    P(kLfo1Speed,   "lfo1 speed",          "spd",      0, 127,  64, Knob,    nullptr),
    P(kLfo1Depth,   "lfo1 depth",          "amt",      0, 127,   0, Knob,    nullptr),
    P(kLfo1Tempo,   "lfo1 tempo sync",     "tempo",    0,   1,   0, Switch,  nullptr),
    P(kLfo1Key,     "lfo1 key sync",       "key",      0,   1,   0, Switch,  nullptr),
    // --- LFO 2
    P(kLfo2On,      "lfo2 on/off",         "2",        0,   1,   0, Switch,  nullptr),
    P(kLfo2Type,    "lfo2 type",           "wave",     0,   5,   1, List,    kLfoShapes),
    P(kLfo2Dest,    "lfo2 destination",    "dst",      1,   7,   3, List,    kLfoDests),
    P(kLfo2Speed,   "lfo2 speed",          "spd",      0, 127,  64, Knob,    nullptr),
    P(kLfo2Depth,   "lfo2 depth",          "amt",      0, 127,   0, Knob,    nullptr),
    P(kLfo2Tempo,   "lfo2 tempo sync",     "tempo",    0,   1,   0, Switch,  nullptr),
    P(kLfo2Key,     "lfo2 key sync",       "key",      0,   1,   0, Switch,  nullptr),
    // --- Wheel / MIDI (stored per patch, not automatable)
    // Synth1 "initial sound": mod wheel -> LFO1 depth and mod wheel -> LFO1 speed.
    H(kMidiSrc1,    "midi ctrl src1",      "src1",     0,  13,   1, List,    kMidiSourceNames),
    H(kMidiSens1,   "midi ctrl sens1",     "sens1",    0, 127, 127, Bipolar, nullptr),
    H(kMidiAssign1, "midi ctrl assign1",   "dst1",    -1,  98, kLfo1Depth, Int, nullptr),
    H(kMidiSrc2,    "midi ctrl src2",      "src2",     0,  13,   1, List,    kMidiSourceNames),
    H(kMidiSens2,   "midi ctrl sens2",     "sens2",    0, 127,  64, Bipolar, nullptr),
    H(kMidiAssign2, "midi ctrl assign2",   "dst2",    -1,  98, kLfo1Speed, Int, nullptr),
    // --- Plugin level
    P(kMasterVolume,"master volume",       "vol",      0, 127, 100, Knob,    nullptr),
};

#undef P
#undef H

static constexpr int kTableSize = int(sizeof(kParams) / sizeof(kParams[0]));

static const std::array<const ParamDef*, 101>& lookup()
{
    static std::array<const ParamDef*, 101> table = [] {
        std::array<const ParamDef*, 101> t{};
        for (const auto& p : kParams)
            if (p.id >= 0 && p.id <= 100) t[p.id] = &p;
        return t;
    }();
    return table;
}

const ParamDef* findParam(int id)
{
    if (id < 0 || id > 100) return nullptr;
    return lookup()[id];
}

const ParamDef* paramAt(int index)
{
    return (index >= 0 && index < kTableSize) ? &kParams[index] : nullptr;
}

int numHostParams() { return kTableSize; }

} // namespace cs
