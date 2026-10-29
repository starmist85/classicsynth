#include "params/ParamDisplay.h"
#include "params/ParamTable.h"
#include "dsp/DspCommon.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cs {

static std::string fmt(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

static std::string fmtTime(double seconds)
{
    if (seconds < 1.0) return fmt("%.1f ms", seconds * 1000.0);
    return fmt("%.2f s", seconds);
}

static std::string fmtHz(double hz)
{
    if (hz >= 1000.0) return fmt("%.2f kHz", hz * 0.001);
    if (hz >= 100.0)  return fmt("%.0f Hz", hz);
    return fmt("%.2f Hz", hz);
}

static std::string bipolarPercent(int v)
{
    // Synth1: (v - 64) / 64 below centre, / 63 above.
    const double x = v < 64 ? (v - 64) / 64.0 : (v - 64) / 63.0;
    if (v == 64) return "0";
    return fmt("%+.0f%%", x * 100.0);
}

std::string formatParamValue(int id, int v)
{
    const ParamDef* d = findParam(id);
    if (!d) return std::to_string(v);
    v = clampValue(*d, v);
    const double x = v / 127.0;

    switch (id) {
    case kModEnvA: case kFltA: case kAmpA:
        return fmtTime(curves::attackSeconds(v));
    case kModEnvD: case kFltD: case kFltR: case kAmpD: case kAmpR:
        return fmtTime(curves::decaySeconds(v));
    case kAmpS:
        return v == 0 ? "-inf dB" : fmt("%.1f dB", 20.0 * std::log10(curves::ampLevel(v)));
    case kAmpGain:
        return v == 0 ? "-inf dB" : fmt("%.1f dB", 20.0 * std::log10(curves::ampLevel(v)));
    case kOsc2Pitch: {
        const int semis = curves::osc2Semitones(v);
        return semis == 0 ? "0" : fmt("%+.0f st", semis);
    }
    case kOsc2Fine: case kOscFine:
        return fmt("%+.0f cent", curves::fineCents(v));
    case kOscKeyShift:
        return v == 0 ? "0" : fmt("%+.0f st", v);
    case kOscMix: {
        const int o2 = int(std::lround(x * 100.0));
        return std::to_string(100 - o2) + " : " + std::to_string(o2);
    }
    case kOscPW:
        return fmt("%.0f%%", curves::pulseDuty(v) * 100.0);
    case kSubGain: {
        const double g = curves::subGain(v);
        return fmt("%.0f%%", g / (g + 1.0) * 100.0);
    }
    case kOscPhase: case kUniPhase:
        return v == 0 ? "free" : fmt("%.0f deg", (v - 1) / 126.0 * 360.0);
    case kFltFreq:
        return fmtHz(curves::cutoffHz(v * 1023.0 / 127.0));
    case kFltTrk: case kFltRes: case kFltSat: case kAmpVel: case kArpGate:
    case kFxCtl1: case kFxCtl2: case kFxLevel: case kChoLevel: case kLfo1Depth: case kLfo2Depth:
    case kOsc1FM: case kOsc1Detune: case kUniDetune: case kDlyFdbk: case kPortaTime:
    case kMasterVolume:
        return fmt("%.0f%%", x * 100.0);
    case kDlyDryWet: {
        const int wet = int(std::lround(x * 100.0));
        return std::to_string(100 - wet) + " : " + std::to_string(wet);
    }
    case kFltAmt: case kModEnvAmt: case kChoFdbk: case kMidiSens1: case kMidiSens2: case kUniSpread:
        return bipolarPercent(v);
    case kEqFreq:
        return fmtHz(curves::eqFreqHz(v));
    case kEqLevel:
        return fmt("%+.1f dB", curves::eqGainDb(v));
    case kEqQ:
        return fmt("Q %.2f", curves::eqQ(v));
    case kEqTone: case kDlyTone: {
        if (v == 64) return "flat";
        bool hp = false;
        const double hz = curves::toneHz(v, hp);
        return std::string(hp ? "LC " : "HC ") + fmtHz(hz);
    }
    case kPan:
        if (v == 64) return "center";
        return v < 64 ? fmt("L %.0f%%", (64 - v) / 64.0 * 100.0) : fmt("R %.0f%%", (v - 64) / 63.0 * 100.0);
    case kDlySpread: {
        const double s = curves::delaySpreadSeconds(v);
        if (v == 64) return "0 ms";
        return s < 0 ? fmt("0 : %.1f ms", -s * 1000.0) : fmt("%.1f : 0 ms", s * 1000.0);
    }
    case kChoTime:
        return fmt("%.2f ms", curves::chorusTimeSeconds(v) * 1000.0);
    case kChoDepth:
        return fmt("%.0f%%", curves::chorusDepth(v) * 100.0);
    case kChoRate:
        return fmt("%.2f Hz", curves::chorusRateHz(v));
    case kLfo1Speed: case kLfo2Speed:
        return fmt("%.2f Hz", curves::lfoFreeHz(v));
    case kUniPitch:
        return v == 24 ? "0" : fmt("%+.0f st", v - 24);
    case kPolyphony: case kUniVoices: case kPbRange:
        return std::to_string(v);
    case kMidiAssign1: case kMidiAssign2: {
        const ParamDef* t = v >= 0 ? findParam(v) : nullptr;
        return t ? std::string(t->name) : std::string("none");
    }
    default:
        break;
    }

    switch (d->kind) {
    case Kind::Switch: return v ? "on" : "off";
    case Kind::List:   return d->labels ? d->labels[v - d->minValue] : std::to_string(v);
    default:           return std::to_string(v);
    }
}

bool parseParamValue(int id, const std::string& text, int& outValue)
{
    const ParamDef* d = findParam(id);
    if (!d) return false;
    if (d->kind == Kind::List && d->labels) {
        for (int v = d->minValue; v <= d->maxValue; ++v)
            if (text == d->labels[v - d->minValue]) { outValue = v; return true; }
    }
    char* end = nullptr;
    const long n = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) return false;
    outValue = clampValue(*d, int(n));
    return true;
}

} // namespace cs
