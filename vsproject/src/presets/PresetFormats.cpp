#include "presets/PresetFormats.h"
#include "dsp/DspCommon.h"

#include "miniz.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace cs {

// ===========================================================================
// helpers
// ===========================================================================
bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (size < 0 || size > 64 * 1024 * 1024) { fclose(f); return false; }
    out.resize(size_t(size));
    const size_t got = size ? fread(out.data(), 1, out.size(), f) : 0;
    fclose(f);
    return got == out.size();
}

bool writeFileBytes(const std::wstring& path, const std::vector<uint8_t>& data)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const size_t put = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return put == data.size();
}

static inline uint32_t rdBE(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
static inline int32_t  rdLE(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
static inline void wrBE(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
static inline void wrLE(uint8_t* p, int32_t x) { std::memcpy(p, &x, 4); }
static constexpr uint32_t fourCC(const char* s) { return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) | (uint32_t(uint8_t(s[2])) << 8) | uint8_t(s[3]); }

static std::string trimLine(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// ===========================================================================
// Version upgrade
// ===========================================================================
// Index 0..127 whose new-curve value is closest to target (Synth1 searches the
// same way, stopping where the error starts growing).
template <class F> static int nearestValue(double target, F curve)
{
    int best = 0;
    double bestErr = 1e30;
    for (int n = 0; n < 128; ++n) {
        const double e = std::fabs(curve(n) - target);
        if (e < bestErr) { bestErr = e; best = n; }
    }
    return best;
}

// FUN_180026c60: V1.00 -> V1.05
static int upgradeFrom100(int id, int v)
{
    const double x = v / 127.0;
    switch (id) {
    case kOsc2Fine: {
        double f = (x - 0.5) * 2.0;            // old: +/-1 semitone, snapped near -1/0/+1
        for (int s = -1; s <= 1; ++s) if (std::fabs(s - f) < 0.03) f = s;
        const double ratio = std::exp2(f / 12.0);
        return nearestValue(ratio, [](int n) { return std::exp2(curves::fineCents(n) / 1200.0); });
    }
    case kModEnvA: case kFltA: case kAmpA: {
        const double old = x < 0.5 ? std::pow(1.75, x * x) - 1.0 + 0.0005 : std::pow(30.0, std::pow(x, 4.6)) - 1.0;
        return nearestValue(old, [](int n) { return float(curves::attackSeconds(n)); });
    }
    case kAmpS: case kAmpGain:
        return nearestValue(x, [](int n) { return float(curves::ampLevel(n)); });
    case kAmpVel:
        return nearestValue(1.0 - x * 0.9, [](int n) { return std::pow(10.0, n / 127.0 * -30.0 * 0.05); });
    case kLfo1Speed: case kLfo2Speed:
        return nearestValue(std::pow(20.0, x) - 0.98333333, [](int n) { return float(curves::lfoFreeHz(n)); });
    default:
        return v;
    }
}

// FUN_180027150: V1.05/1.06 -> V1.07
static int upgradeFrom105(int id, int v)
{
    const double x = v / 127.0;
    switch (id) {
    case kModEnvD: case kFltD: case kFltR: case kAmpD: case kAmpR: {
        const double old = std::pow(60.0, std::pow(x, 2.95)) - 1.0;
        return nearestValue(old, [](int n) { return float(curves::decaySeconds(n)); });
    }
    case kFltAmt:   // unipolar amount became bipolar: 0 -> centre (no envelope)
        return nearestValue(x, [](int n) { return (n - 63) / 64.0; });
    case kDlyDryWet:
        return v / 2;
    case kChoTime: {
        const double ms = x < 0.5 ? std::pow(50.0, x * 2.0) * 0.3 : x * 30.0;
        return nearestValue(ms * 0.001, [](int n) { return float(curves::chorusTimeSeconds(n)); });
    }
    case kChoRate:
        return nearestValue(std::pow(10.0, std::pow(x, 1.8)) - 1.0 + 0.05, [](int n) { return float(curves::chorusRateHz(n)); });
    case kChoFdbk:  // old unipolar feedback is dropped (integer arithmetic yields 0)
        return 64;
    default:
        return v;
    }
}

void upgradePatchVersion(Patch& p)
{
    const int ver = p.version;
    if (ver >= 113) return;
    auto present = [&](int id) { return p.present[size_t(id)]; };
    auto setDefault = [&](int id) { if (const ParamDef* d = findParam(id)) p.set(id, d->defValue); };

    // Each stage falls through to the next, as in Synth1.
    const bool from100 = ver == 100;
    const bool from105 = from100 || ver == 105 || ver == 106;
    const bool from107 = from105 || ver == 107 || ver == 108;
    const bool from109 = from107 || ver == 109;
    const bool from111 = from109 || ver == 111;

    for (int id = 0; id < kNumPatchParams; ++id) {
        if (!present(id)) continue;
        int v = p.values[size_t(id)];
        if (from100) {
            if (id >= kLfo1Tempo && id <= kOscFine) { setDefault(id); continue; }   // 67..72 from current
            v = upgradeFrom100(id, v);
        }
        if (from105) {
            if ((id >= kUniDetune && id <= kUniPitch) || id == kPan) { setDefault(id); continue; }
            v = upgradeFrom105(id, v);
        }
        p.values[size_t(id)] = v;
    }
    if (from105) {
        p.set(kUniSpread, 64);
        p.set(kUniPitch, 24);
        p.set(kMidiSrc1, 0);     // Synth1 writes 0 = no controller source
        p.set(kMidiSrc2, 0);
        p.set(kMidiAssign1, -1);
        p.set(kMidiAssign2, -1);
    }
    if (from107) {
        // FUN_1800276f0: wheel sensitivities rebased on LFO1 depth / speed.
        if (present(kMidiSens1)) {
            const int a = p.get(kLfo1Depth);
            p.set(kMidiSens1, 64 + int((1.0 - a / 127.0) * p.get(kMidiSens1) / 127.0 * 63.0));
        }
        if (present(kMidiSens2)) {
            const int s = p.get(kLfo1Speed);
            p.set(kMidiSens2, 64 + int((1.0 - s / 127.0) * p.get(kMidiSens2) / 127.0 * 63.0));
        }
        // Old unison: 2 voices in poly, 4 in mono/legato.
        p.set(kUniVoices, p.get(kPlayMode) == 0 ? 2 : 4);
        if (!present(kUniDetune)) p.set(kUniDetune, 32);
    }
    (void)from109;
    (void)from111;   // 95..98 come from the current patch in Synth1; defaults here
    p.version = 113;
}

// ===========================================================================
// .sy1
// ===========================================================================
static bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}

// Strict integer parse of a whole token ("-12" ok, "1.5" / "abc" / "" rejected).
static bool parseInt(const std::string& s, long long& v)
{
    if (s.empty() || s.size() > 12) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size()) return false;
    for (size_t k = i; k < s.size(); ++k) if (s[k] < '0' || s[k] > '9') return false;
    v = std::atoll(s.c_str());
    return true;
}

static int countCoreParams(const Patch& p)
{
    int n = 0;
    for (int id = 0; id <= kOscFine; ++id) if (p.present[size_t(id)]) ++n;   // IDs 0..72 exist since V1.05
    return n;
}

bool parseSy1(const std::string& text, Patch& out, std::string* why)
{
    // Binary data is never a Synth1 patch.
    if (text.empty()) return fail(why, "no compatible ClassicSynth patch (empty file)");
    if (text.find('\0') != std::string::npos) return fail(why, "no compatible ClassicSynth patch (binary data)");

    std::istringstream in(text);
    std::string line;
    Patch p;
    p.present.fill(false);
    p.version = 100;   // files without ver= are V1.00

    if (!std::getline(in, line)) return fail(why, "no compatible ClassicSynth patch (empty file)");
    // Strip a UTF-8 BOM if a text editor added one.
    if (line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB && uint8_t(line[2]) == 0xBF) line.erase(0, 3);
    p.name = trimLine(line);
    if (p.name.size() > 64) return fail(why, "no compatible ClassicSynth patch (invalid program name)");
    long long dummy;
    if (p.name.find(',') != std::string::npos) {
        const size_t c = p.name.find(',');
        if (parseInt(p.name.substr(0, c), dummy)) return fail(why, "no compatible ClassicSynth patch (missing program name)");
    }

    while (std::getline(in, line)) {
        line = trimLine(line);
        if (line.empty()) continue;
        if (_strnicmp(line.c_str(), "color=", 6) == 0) { p.color = line.substr(6); continue; }
        if (_strnicmp(line.c_str(), "ver=", 4) == 0) {
            long long ver = 0;
            if (!parseInt(line.substr(4), ver) || ver < 100 || ver > 199)
                return fail(why, "no compatible ClassicSynth patch (unknown patch version)");
            p.version = int(ver);
            continue;
        }
        const size_t comma = line.find(',');
        long long id = 0, v = 0;
        if (comma == std::string::npos || !parseInt(line.substr(0, comma), id) || !parseInt(line.substr(comma + 1), v))
            return fail(why, "no compatible ClassicSynth patch (unexpected content)");
        if (id < 0 || id > 255) return fail(why, "no compatible ClassicSynth patch (unknown parameter ID)");
        if (id < kNumPatchParams && findParam(int(id))) p.set(int(id), int(clampv<long long>(v, INT32_MIN, INT32_MAX)));
    }
    if (countCoreParams(p) < kMinCoreParams) return fail(why, "no compatible ClassicSynth patch (too few parameters)");

    upgradePatchVersion(p);
    p.fillMissingWithDefaults();
    out = p;
    return true;
}

// Value as stored in Synth1 files (MIDI source slots use status codes).
static int fileValue(const Patch& p, int id)
{
    const int v = p.get(id);
    return (id == kMidiSrc1 || id == kMidiSrc2) ? midiSourceToSynth1Code(v) : v;
}

std::string writeSy1(const Patch& p)
{
    std::string s = p.name + "\n";
    s += "color=" + (p.color.empty() ? std::string("default") : p.color) + "\n";
    s += "ver=113\n";
    char buf[32];
    for (int id = 0; id < kNumPatchParams; ++id) {
        std::snprintf(buf, sizeof(buf), "%d,%d\n", id, fileValue(p, id));
        s += buf;
    }
    return s;
}

// ===========================================================================
// Synth1 VST chunk
// ===========================================================================
static const char kChunkMagic[] = "Synth1 VST Chunk Data";
static const char kBankMagic[]  = "ClassicSynth Bank";

// Legacy chunk from Synth1's DXi era (FUN_180028cb0), still accepted by V1.13:
//   +0x000 "Synth1 DXi Data Header V1.0\0" (0x1C bytes)
//   +0x01C { char present; pad[3]; int32 value } x 254, slot = parameter ID
//          slot 250 = patch version (default 100), 251..254 host-level values
//   total size 0x814
static const char kDxiMagic[] = "Synth1 DXi Data Header V1.0";
static constexpr size_t kDxiChunkSize = 0x814;

static bool parseDxiChunk(const uint8_t* d, size_t size, Patch& out, std::string* why)
{
    if (size != kDxiChunkSize) return fail(why, "no compatible ClassicSynth patch (legacy data has an unexpected size)");
    Patch p;
    p.present.fill(false);
    for (int id = 0; id < kNumPatchParams; ++id) {
        const uint8_t* slot = d + 0x1C + size_t(id) * 8;
        if (slot[0] > 1) return fail(why, "no compatible ClassicSynth patch (corrupt legacy data)");
        if (slot[0] == 1 && findParam(id)) p.set(id, rdLE(slot + 4));
    }
    const uint8_t* verSlot = d + 0x1C + 250 * 8;   // 0x7EC
    p.version = verSlot[0] == 1 ? rdLE(verSlot + 4) : 100;
    if (p.version < 100 || p.version > 199) return fail(why, "no compatible ClassicSynth patch (unknown patch version)");
    if (countCoreParams(p) < kMinCoreParams) return fail(why, "no compatible ClassicSynth patch (too few parameters)");
    upgradePatchVersion(p);
    p.fillMissingWithDefaults();
    p.name.clear();   // the DXi chunk has no name; the .fxp header provides it
    out = p;
    return true;
}

bool parseSynth1Chunk(const uint8_t* d, size_t size, Patch& out, std::string* why)
{
    if (size >= sizeof(kDxiMagic) && std::memcmp(d, kDxiMagic, sizeof(kDxiMagic)) == 0)
        return parseDxiChunk(d, size, out, why);
    if (size < sizeof(kChunkMagic) || std::memcmp(d, kChunkMagic, sizeof(kChunkMagic)) != 0)
        return fail(why, "no compatible ClassicSynth patch data");
    if (size < 0x238 + 8 * kNumPatchParams) return fail(why, "no compatible ClassicSynth patch (data is truncated)");
    const int chunkVersion = rdLE(d + 0x20);
    if (chunkVersion < 1 || chunkVersion > 2) return fail(why, "no compatible ClassicSynth patch (unsupported data version)");
    const int slots = rdLE(d + 0x234);
    if (slots != 0x100) return fail(why, "no compatible ClassicSynth patch (unexpected data layout)");

    Patch p;
    p.present.fill(false);
    const char* name = reinterpret_cast<const char*>(d + 0x28);
    size_t len = 0;
    while (len < 0x200 && name[len]) ++len;
    if (len >= 0x200) return fail(why, "no compatible ClassicSynth patch (corrupt program name)");
    p.name.assign(name, len);
    const int ver = rdLE(d + 0x230);
    if (ver != 0 && (ver < 100 || ver > 199)) return fail(why, "no compatible ClassicSynth patch (unknown patch version)");
    p.version = ver > 0 ? ver : 113;

    for (int id = 0; id < kNumPatchParams; ++id) {
        const uint8_t* slot = d + 0x238 + size_t(id) * 8;
        const int flag = rdLE(slot);
        if (flag != 0 && flag != 1) return fail(why, "no compatible ClassicSynth patch (corrupt parameter data)");
        if (flag == 1 && findParam(id)) p.set(id, rdLE(slot + 4));
    }
    if (countCoreParams(p) < kMinCoreParams) return fail(why, "no compatible ClassicSynth patch (too few parameters)");
    upgradePatchVersion(p);
    p.fillMissingWithDefaults();
    out = p;
    return true;
}

std::vector<uint8_t> writeSynth1Chunk(const Patch& p, int programIndex)
{
    std::vector<uint8_t> c(kSynth1ChunkSize, 0);
    std::memcpy(c.data(), kChunkMagic, sizeof(kChunkMagic));
    wrLE(&c[0x20], 2);
    wrLE(&c[0x24], 0);
    const size_t n = std::min<size_t>(p.name.size(), 0x1FF);
    std::memcpy(&c[0x28], p.name.data(), n);
    wrLE(&c[0x230], 0x71);
    wrLE(&c[0x234], 0x100);
    for (int id = 0; id < kNumPatchParams; ++id) {
        wrLE(&c[0x238 + size_t(id) * 8], 1);
        wrLE(&c[0x23C + size_t(id) * 8], fileValue(p, id));
    }
    // Trailing info slots as written by Synth1.
    wrLE(&c[0xA08], 1); wrLE(&c[0xA0C], 0x71);
    wrLE(&c[0xA10], 1); wrLE(&c[0xA14], -1);
    wrLE(&c[0xA18], 1); wrLE(&c[0xA1C], programIndex);
    wrLE(&c[0xA28], 1); wrLE(&c[0xA2C], programIndex);
    return c;
}

// ===========================================================================
// Synth1 float <-> value (FUN_180025c10 bucketing)
// ===========================================================================
static const int kLfoTypeOrder[6] = { 0, 1, 5, 2, 3, 4 };   // slider position -> wave index

int synth1FloatToValue(int id, float f)
{
    const ParamDef* d = findParam(id);
    if (!d) return 0;
    f = clampv(f, 0.f, 1.f);
    if (id == kLfo1Type || id == kLfo2Type) return kLfoTypeOrder[std::min(int(f * 6.f), 5)];
    const int count = d->maxValue - d->minValue + 1;
    if (count == 128) return std::min(127, int(f * 128.f));
    return d->minValue + std::min(int(f * float(count)), count - 1);
}

float synth1ValueToFloat(int id, int v)
{
    const ParamDef* d = findParam(id);
    if (!d) return 0.f;
    if (id == kLfo1Type || id == kLfo2Type) {
        for (int i = 0; i < 6; ++i) if (kLfoTypeOrder[i] == v) return (i + 0.5f) / 6.f;
        return 0.f;
    }
    const int count = d->maxValue - d->minValue + 1;
    return (float(clampValue(*d, v) - d->minValue) + 0.5f) / float(count);
}

// ===========================================================================
// .fxp / .fxb
// ===========================================================================
static std::string fourCCText(uint32_t v)
{
    std::string s;
    for (int sh = 24; sh >= 0; sh -= 8) {
        const char c = char((v >> sh) & 0xFF);
        s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

// Validates the fxID of a VST2 container: only Synth1 ('S1Vs') data is compatible.
static bool checkFxID(uint32_t fxID, std::string* why)
{
    if (fxID == kSynth1FxID) return true;
    if (why) *why = "made by another plugin - no compatible ClassicSynth patches (plugin ID '" + fourCCText(fxID) + "')";
    return false;
}

static bool parseFxProgram(const uint8_t* d, size_t size, Patch& out, size_t* consumed, std::string* why)
{
    if (size < 56 || rdBE(d) != fourCC("CcnK")) return fail(why, "no compatible ClassicSynth patch (not a VST preset file)");
    const uint32_t byteSize = rdBE(d + 4);
    if (byteSize < 48 || size_t(byteSize) + 8 > size) return fail(why, "no compatible ClassicSynth patch (file size mismatch)");
    const size_t total = size_t(byteSize) + 8;
    const uint32_t magic = rdBE(d + 8);
    if (magic != fourCC("FPCh") && magic != fourCC("FxCk"))
        return fail(why, "no compatible ClassicSynth patch (not a VST program)");
    if (!checkFxID(rdBE(d + 16), why)) return false;
    const int numParams = int(rdBE(d + 24));
    char nameBuf[29] = {};
    std::memcpy(nameBuf, d + 28, 28);
    if (consumed) *consumed = total;

    if (magic == fourCC("FPCh")) {
        if (total < 60) return fail(why, "no compatible ClassicSynth patch (data is truncated)");
        const uint32_t chunkSize = rdBE(d + 56);
        if (60 + size_t(chunkSize) > total) return fail(why, "no compatible ClassicSynth patch (file size mismatch)");
        if (!parseSynth1Chunk(d + 60, chunkSize, out, why)) return false;
        if (out.name.empty()) out.name = nameBuf;
        return true;
    }

    // FxCk: plain parameter list; Synth1 reports 255 parameters (older builds fewer).
    if (numParams < kOscFine + 1 || numParams > 255) return fail(why, "no compatible ClassicSynth patch (parameter count mismatch)");
    if (56 + size_t(numParams) * 4 > total) return fail(why, "no compatible ClassicSynth patch (data is truncated)");
    Patch p;
    p.present.fill(false);
    p.name = nameBuf;
    for (int i = 0; i < numParams; ++i) {
        uint32_t raw = rdBE(d + 56 + size_t(i) * 4);
        float f;
        std::memcpy(&f, &raw, 4);
        if (!std::isfinite(f) || f < -0.0001f || f > 1.0001f) return fail(why, "no compatible ClassicSynth patch (invalid parameter values)");
        if (i < kNumPatchParams && findParam(i)) p.set(i, synth1FloatToValue(i, f));
    }
    p.version = 113;
    p.fillMissingWithDefaults();
    out = p;
    return true;
}

bool parseFxp(const std::vector<uint8_t>& file, Patch& out, std::string* why)
{
    if (file.size() < 60) return fail(why, "no compatible ClassicSynth patch (file too small)");
    size_t used = 0;
    if (!parseFxProgram(file.data(), file.size(), out, &used, why)) return false;
    return true;
}

std::vector<uint8_t> writeFxp(const Patch& p)
{
    const std::vector<uint8_t> chunk = writeSynth1Chunk(p);
    std::vector<uint8_t> v;
    wrBE(v, fourCC("CcnK"));
    wrBE(v, uint32_t(52 + chunk.size()));
    wrBE(v, fourCC("FPCh"));
    wrBE(v, 1);
    wrBE(v, kSynth1FxID);
    wrBE(v, 1);
    wrBE(v, 255);   // numParams as reported by Synth1
    char name[28] = {};
    std::memcpy(name, p.name.data(), std::min<size_t>(p.name.size(), 27));
    v.insert(v.end(), name, name + 28);
    wrBE(v, uint32_t(chunk.size()));
    v.insert(v.end(), chunk.begin(), chunk.end());
    return v;
}

bool parseFxb(const std::vector<uint8_t>& file, std::vector<Patch>& out, std::string* why)
{
    const uint8_t* d = file.data();
    const size_t size = file.size();
    out.clear();
    if (size < 160 || rdBE(d) != fourCC("CcnK")) return fail(why, "no compatible ClassicSynth patches (not a VST bank file)");
    const uint32_t byteSize = rdBE(d + 4);
    if (size_t(byteSize) + 8 > size) return fail(why, "no compatible ClassicSynth patches (file size mismatch)");
    const uint32_t magic = rdBE(d + 8);
    if (magic != fourCC("FBCh") && magic != fourCC("FxBk"))
        return fail(why, "no compatible ClassicSynth patches (not a VST bank)");
    if (!checkFxID(rdBE(d + 16), why)) return false;
    const int numPrograms = int(rdBE(d + 24));
    if (numPrograms < 0 || numPrograms > 4096) return fail(why, "no compatible ClassicSynth patches (invalid program count)");

    if (magic == fourCC("FBCh")) {
        const uint32_t chunkSize = rdBE(d + 156);
        if (160 + size_t(chunkSize) > size) return fail(why, "no compatible ClassicSynth patches (file size mismatch)");
        const uint8_t* c = d + 160;
        if (chunkSize >= 36 && std::memcmp(c, kBankMagic, sizeof(kBankMagic)) == 0) {
            const int count = rdLE(c + 32);
            if (count <= 0 || count > 4096 || 36 + size_t(count) * kSynth1ChunkSize != chunkSize)
                return fail(why, "no compatible ClassicSynth patches (corrupt bank data)");
            for (int i = 0; i < count; ++i) {
                Patch p;
                if (!parseSynth1Chunk(c + 36 + size_t(i) * kSynth1ChunkSize, kSynth1ChunkSize, p, why)) { out.clear(); return false; }
                out.push_back(p);
            }
        } else {
            Patch p;   // Synth1 bank chunk = current program only
            if (!parseSynth1Chunk(c, chunkSize, p, why)) return false;
            out.push_back(p);
        }
        return true;
    }

    // FxBk: list of FxCk / FPCh programs, each validated.
    size_t off = 156;
    for (int i = 0; i < numPrograms; ++i) {
        Patch p;
        size_t used = 0;
        if (off >= size || !parseFxProgram(d + off, size - off, p, &used, why) || used == 0) { out.clear(); return false; }
        out.push_back(p);
        off += used;
    }
    if (out.empty()) return fail(why, "no compatible ClassicSynth patches (bank is empty)");
    return true;
}

std::vector<uint8_t> writeFxb(const std::vector<Patch>& bank)
{
    std::vector<uint8_t> chunk(36, 0);
    std::memcpy(chunk.data(), kBankMagic, sizeof(kBankMagic));
    wrLE(&chunk[32], int32_t(bank.size()));
    for (size_t i = 0; i < bank.size(); ++i) {
        const auto c = writeSynth1Chunk(bank[i], int(i));
        chunk.insert(chunk.end(), c.begin(), c.end());
    }

    std::vector<uint8_t> v;
    wrBE(v, fourCC("CcnK"));
    wrBE(v, uint32_t(152 + chunk.size()));
    wrBE(v, fourCC("FBCh"));
    wrBE(v, 1);
    wrBE(v, kSynth1FxID);
    wrBE(v, 1);
    wrBE(v, uint32_t(bank.size()));
    v.insert(v.end(), 128, 0);   // future
    wrBE(v, uint32_t(chunk.size()));
    v.insert(v.end(), chunk.begin(), chunk.end());
    return v;
}

// ===========================================================================
// .zip banks
// ===========================================================================
struct ZipScan {
    std::map<std::string, ZipBankFolder> folders;
    int rejected = 0, sy1Entries = 0;
    std::string firstReason;
};

static bool isMacJunk(const std::string& path)
{
    if (path.find("__MACOSX/") != std::string::npos) return true;
    const size_t slash = path.rfind('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    return base.rfind("._", 0) == 0;
}

// Scans one archive held in memory; nested .zip banks are followed one level deep.
static bool scanZipMemory(const uint8_t* data, size_t size, const std::string& prefix, int depth, ZipScan& scan)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) return false;

    const mz_uint count = std::min<mz_uint>(mz_zip_reader_get_num_files(&zip), 20000);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        char fname[512];
        mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
        std::string path(fname);
        std::replace(path.begin(), path.end(), '\\', '/');
        if (isMacJunk(path)) continue;
        const bool isSy1 = path.size() >= 5 && _stricmp(path.c_str() + path.size() - 4, ".sy1") == 0;
        const bool isFxp = path.size() >= 5 && _stricmp(path.c_str() + path.size() - 4, ".fxp") == 0;
        const bool isZip = path.size() >= 5 && _stricmp(path.c_str() + path.size() - 4, ".zip") == 0;
        if (!isSy1 && !isFxp && !(isZip && depth < 1)) continue;

        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;

        if (isZip) {
            if (st.m_uncomp_size > 32ull * 1024 * 1024) continue;   // not a preset bank
            size_t sz = 0;
            void* inner = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
            if (!inner) continue;
            scanZipMemory(static_cast<const uint8_t*>(inner), sz, prefix + path.substr(0, path.size() - 4) + "/", depth + 1, scan);
            mz_free(inner);
            continue;
        }

        ++scan.sy1Entries;
        // A Synth1 patch / .fxp is a few KB; refuse oversized entries outright.
        if (st.m_uncomp_size > 16 * 1024) {
            ++scan.rejected;
            if (scan.firstReason.empty()) scan.firstReason = "no compatible ClassicSynth patch (entry too large)";
            continue;
        }
        size_t sz = 0;
        void* entry = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!entry) { ++scan.rejected; continue; }
        Patch p;
        std::string reason;
        const auto* bytes8 = static_cast<const uint8_t*>(entry);
        const bool ok = isFxp ? parseFxp(std::vector<uint8_t>(bytes8, bytes8 + sz), p, &reason)
                              : parseSy1(std::string(static_cast<const char*>(entry), sz), p, &reason);
        mz_free(entry);
        if (ok && p.name.empty()) {
            const size_t s = path.rfind('/');
            const std::string base = s == std::string::npos ? path : path.substr(s + 1);
            p.name = base.substr(0, base.size() - 4);
        }
        if (!ok) {
            ++scan.rejected;
            if (scan.firstReason.empty()) scan.firstReason = reason;
            continue;
        }

        const std::string full = prefix + path;
        const size_t slash = full.rfind('/');
        const std::string folder = slash == std::string::npos ? "" : full.substr(0, slash);
        const std::string base = slash == std::string::npos ? full : full.substr(slash + 1);
        auto& f = scan.folders[folder];
        f.folder = folder;
        f.programNumbers.push_back(std::atoi(base.c_str()));
        f.patches.push_back(p);
    }
    mz_zip_reader_end(&zip);
    return true;
}

bool readZipBank(const std::wstring& zipPath, std::vector<ZipBankFolder>& out, std::string* why, int* rejectedEntries)
{
    std::vector<uint8_t> bytes;
    if (!readFileBytes(zipPath, bytes)) return fail(why, "cannot read file");

    ZipScan scan;
    if (!scanZipMemory(bytes.data(), bytes.size(), "", 0, scan)) return fail(why, "no compatible ClassicSynth patches (not a valid zip archive)");
    auto& folders = scan.folders;
    const int rejected = scan.rejected, sy1Entries = scan.sy1Entries;
    const std::string firstReason = scan.firstReason;

    out.clear();
    for (auto& kv : folders) {
        ZipBankFolder& f = kv.second;
        // Sort by program number (001.sy1 .. 128.sy1).
        std::vector<size_t> idx(f.patches.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return f.programNumbers[a] < f.programNumbers[b]; });
        ZipBankFolder sorted;
        sorted.folder = f.folder;
        for (size_t i : idx) { sorted.programNumbers.push_back(f.programNumbers[i]); sorted.patches.push_back(f.patches[i]); }
        out.push_back(std::move(sorted));
    }
    if (rejectedEntries) *rejectedEntries = rejected;
    if (out.empty()) {
        if (sy1Entries == 0) return fail(why, "no compatible ClassicSynth patches in this zip");
        if (why) *why = "no compatible ClassicSynth patches in this zip (" + firstReason + ")";
        return false;
    }
    return true;
}

} // namespace cs
