// ClassicSynth test harness (console).
//   1) loads the built .vst3 and lists its factory classes
//   2) parses Synth1 zip banks and round-trips .sy1 / .fxp / .fxb
//   3) renders presets through the engine to WAV and checks the output
//   4) drives the built processor with host MIDI and on-screen keyboard/wheel
//      messages at the same time (shared notes, queue overflow, wheel ownership)
// Usage: EngineTest <bundle .vst3 binary> <zip bank> <out dir>

#include "dsp/SynthEngine.h"
#include "presets/PresetFormats.h"

#include "pluginids.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace cs;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++gFailures; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static void writeWav(const std::wstring& path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    const uint32_t n = uint32_t(L.size()), dataBytes = n * 4;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(uint32_t(fs)); w32(uint32_t(fs) * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(dataBytes);
    for (uint32_t i = 0; i < n; ++i) {
        const int16_t l = int16_t(std::lround(clampv(L[i], -1.f, 1.f) * 32767.f));
        const int16_t r = int16_t(std::lround(clampv(R[i], -1.f, 1.f) * 32767.f));
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
}

static void testFactory(const std::wstring& binary)
{
    std::printf("[1] VST3 factory: %ls\n", binary.c_str());
    HMODULE h = LoadLibraryW(binary.c_str());
    CHECK(h != nullptr, "LoadLibrary failed (%lu)", GetLastError());
    if (!h) return;
    using InitFn = bool (*)();
    if (auto init = reinterpret_cast<InitFn>(GetProcAddress(h, "InitDll"))) init();
    using GetFactory = Steinberg::IPluginFactory* (*)();
    auto gf = reinterpret_cast<GetFactory>(GetProcAddress(h, "GetPluginFactory"));
    CHECK(gf != nullptr, "GetPluginFactory export missing");
    if (!gf) return;
    Steinberg::IPluginFactory* factory = gf();
    Steinberg::PFactoryInfo fi;
    factory->getFactoryInfo(&fi);
    std::printf("  vendor: %s  url: %s\n", fi.vendor, fi.url);
    CHECK(std::string(fi.vendor) == "Starmist Audio", "vendor is '%s'", fi.vendor);
    const int n = factory->countClasses();
    for (int i = 0; i < n; ++i) {
        Steinberg::PClassInfo ci;
        factory->getClassInfo(i, &ci);
        std::printf("  class %d: %-28s category: %s\n", i, ci.name, ci.category);
    }
    CHECK(n == 2, "expected 2 classes, got %d", n);
    Steinberg::IPluginFactory2* f2 = nullptr;
    if (factory->queryInterface(Steinberg::IPluginFactory2::iid, reinterpret_cast<void**>(&f2)) == Steinberg::kResultOk && f2) {
        Steinberg::PClassInfo2 c2;
        f2->getClassInfo2(0, &c2);
        std::printf("  subCategories: %s  sdk: %s  version: %s\n", c2.subCategories, c2.sdkVersion, c2.version);
        f2->release();
    }
    factory->release();
    using ExitFn = bool (*)();
    if (auto ex = reinterpret_cast<ExitFn>(GetProcAddress(h, "ExitDll"))) ex();
    FreeLibrary(h);
}

// ---------------------------------------------------------------------------
// Minimal host-side objects for driving the processor directly.
// ---------------------------------------------------------------------------
namespace host {
using namespace Steinberg;
using namespace Steinberg::Vst;
#define CS_TEST_FUNKNOWN                                                                         \
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override { *obj = nullptr; return kNoInterface; } \
    uint32 PLUGIN_API addRef() override { return 1; }                                            \
    uint32 PLUGIN_API release() override { return 1; }

struct Attributes : IAttributeList {
    std::map<std::string, int64> ints;
    std::map<std::string, double> floats;
    tresult PLUGIN_API setInt(AttrID id, int64 v) override { ints[id] = v; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64& v) override { auto it = ints.find(id); if (it == ints.end()) return kResultFalse; v = it->second; return kResultOk; }
    tresult PLUGIN_API setFloat(AttrID id, double v) override { floats[id] = v; return kResultOk; }
    tresult PLUGIN_API getFloat(AttrID id, double& v) override { auto it = floats.find(id); if (it == floats.end()) return kResultFalse; v = it->second; return kResultOk; }
    tresult PLUGIN_API setString(AttrID, const TChar*) override { return kResultFalse; }
    tresult PLUGIN_API getString(AttrID, TChar*, uint32) override { return kResultFalse; }
    tresult PLUGIN_API setBinary(AttrID, const void*, uint32) override { return kResultFalse; }
    tresult PLUGIN_API getBinary(AttrID, const void*&, uint32&) override { return kResultFalse; }
    CS_TEST_FUNKNOWN
};

struct Message : IMessage {
    std::string id;
    Attributes attrs;
    FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
    void PLUGIN_API setMessageID(FIDString s) override { id = s; }
    IAttributeList* PLUGIN_API getAttributes() override { return &attrs; }
    CS_TEST_FUNKNOWN
};

struct EventList : IEventList {
    std::vector<Event> events;
    int32 PLUGIN_API getEventCount() override { return int32(events.size()); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override { if (i < 0 || i >= int32(events.size())) return kResultFalse; e = events[size_t(i)]; return kResultOk; }
    tresult PLUGIN_API addEvent(Event& e) override { events.push_back(e); return kResultOk; }
    CS_TEST_FUNKNOWN
};

struct ValueQueue : IParamValueQueue {
    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> points;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return int32(points.size()); }
    tresult PLUGIN_API getPoint(int32 i, int32& o, ParamValue& v) override { if (i < 0 || i >= int32(points.size())) return kResultFalse; o = points[size_t(i)].first; v = points[size_t(i)].second; return kResultOk; }
    tresult PLUGIN_API addPoint(int32 o, ParamValue v, int32& i) override { i = int32(points.size()); points.push_back({ o, v }); return kResultOk; }
    CS_TEST_FUNKNOWN
};

struct ParamChanges : IParameterChanges {
    std::vector<ValueQueue> queues;
    int32 PLUGIN_API getParameterCount() override { return int32(queues.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < int32(queues.size()) ? &queues[size_t(i)] : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& i) override
    {
        i = int32(queues.size());
        queues.emplace_back();
        queues.back().id = id;
        return &queues.back();
    }
    CS_TEST_FUNKNOWN
};
} // namespace host

// Host MIDI and the on-screen keyboard / wheels hitting the processor at the same time.
static void testMixedInput(const std::wstring& binary)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    std::printf("[4] Host MIDI + on-screen keyboard/wheels\n");
    HMODULE h = LoadLibraryW(binary.c_str());
    if (!h) { CHECK(false, "LoadLibrary failed"); return; }
    if (auto init = reinterpret_cast<bool (*)()>(GetProcAddress(h, "InitDll"))) init();
    auto gf = reinterpret_cast<IPluginFactory* (*)()>(GetProcAddress(h, "GetPluginFactory"));
    IPluginFactory* factory = gf();

    const int fs = 48000, block = 256;
    struct Instance {
        IComponent* comp = nullptr;
        IAudioProcessor* proc = nullptr;
        IConnectionPoint* conn = nullptr;
        host::EventList events;
        host::ParamChanges changes;
        std::vector<float> l, r;
    } inst;
    TUID cid;
    kProcessorUID.toTUID(cid);
    factory->createInstance(cid, IComponent::iid, reinterpret_cast<void**>(&inst.comp));
    if (!inst.comp) { CHECK(false, "cannot create processor"); factory->release(); return; }
    inst.comp->initialize(nullptr);
    inst.comp->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&inst.proc));
    inst.comp->queryInterface(IConnectionPoint::iid, reinterpret_cast<void**>(&inst.conn));
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    inst.proc->setBusArrangements(nullptr, 0, &stereo, 1);
    inst.comp->activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{ kRealtime, kSample32, block, double(fs) };
    inst.proc->setupProcessing(setup);
    inst.comp->setActive(true);
    inst.proc->setProcessing(true);

    // --- helpers
    auto uiNote = [&](int pitch, float vel) {
        host::Message m;
        m.setMessageID(kMsgNote);
        m.attrs.setInt(kMsgAttrPitch, pitch);
        m.attrs.setFloat(kMsgAttrVelocity, vel);
        inst.conn->notify(&m);
    };
    auto uiCtrl = [&](int id, double v, bool held) {
        host::Message m;
        m.setMessageID(kMsgMidiCtrl);
        m.attrs.setInt(kMsgAttrId, id);
        m.attrs.setFloat(kMsgAttrValue, v);
        m.attrs.setInt(kMsgAttrHeld, held ? 1 : 0);
        inst.conn->notify(&m);
    };
    auto hostNote = [&](int pitch, bool on) {
        Event e{};
        e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
        e.sampleOffset = 17;
        if (on) { e.noteOn.pitch = int16(pitch); e.noteOn.velocity = 0.8f; e.noteOn.noteId = -1; }
        else    { e.noteOff.pitch = int16(pitch); e.noteOff.noteId = -1; }
        inst.events.addEvent(e);
    };
    auto hostParam = [&](int id, double v) {
        int32 qi = 0, pi = 0;
        inst.changes.addParameterData(ParamID(id), qi)->addPoint(0, v, pi);
    };
    // Renders `seconds`; the first block carries any queued host events/changes.
    auto run = [&](double seconds) {
        std::vector<float> L(block), R(block);
        float* ch[2] = { L.data(), R.data() };
        inst.l.clear();
        const int blocks = int(seconds * fs / block);
        for (int b = 0; b < blocks; ++b) {
            AudioBusBuffers out{};
            out.numChannels = 2;
            out.channelBuffers32 = ch;
            ProcessData d{};
            d.processMode = kRealtime;
            d.symbolicSampleSize = kSample32;
            d.numSamples = block;
            d.numOutputs = 1;
            d.outputs = &out;
            d.inputEvents = &inst.events;
            d.inputParameterChanges = &inst.changes;
            inst.proc->process(d);
            inst.events.events.clear();
            inst.changes.queues.clear();
            inst.l.insert(inst.l.end(), L.begin(), L.end());
        }
    };
    auto rmsTail = [&](double fromSec) {   // RMS of the last run() from `fromSec` on
        double s = 0; size_t n = 0;
        for (size_t i = size_t(fromSec * fs); i < inst.l.size(); ++i) { s += double(inst.l[i]) * inst.l[i]; ++n; }
        return n ? std::sqrt(s / double(n)) : 0.0;
    };
    auto pitchHz = [&]() {                 // rising crossings with hysteresis over the last run()
        int crossings = 0; bool low = false;
        size_t first = 0, last = 0;
        for (size_t i = inst.l.size() / 4; i < inst.l.size(); ++i) {
            if (inst.l[i] < -0.02f) low = true;
            else if (low && inst.l[i] > 0.02f) { low = false; if (!crossings) first = i; last = i; ++crossings; }
        }
        return crossings > 1 ? double(crossings - 1) * fs / double(last - first) : 0.0;
    };
    const double kSilent = 1e-4;

    // A) host + UI hold the same note; the UI lets go first: the host's note keeps sounding.
    hostNote(60, true); uiNote(60, 0.8f);
    run(0.3);
    uiNote(60, 0.f);
    run(0.6);
    const double a1 = rmsTail(0.3);
    hostNote(60, false);
    run(3.0);
    const double a2 = rmsTail(2.5);
    std::printf("  shared note, UI off first: held rms %.4f, after host off %.6f\n", a1, a2);
    CHECK(a1 > 0.01, "UI note-off cut the host's held note (rms %.4f)", a1);
    CHECK(a2 < kSilent, "note stuck after both sources released (rms %.6f)", a2);

    // B) the other way round.
    uiNote(64, 0.8f); hostNote(64, true);
    run(0.3);
    hostNote(64, false);
    run(0.6);
    const double b1 = rmsTail(0.3);
    uiNote(64, 0.f);
    run(3.0);
    const double b2 = rmsTail(2.5);
    std::printf("  shared note, host off first: held rms %.4f, after UI off %.6f\n", b1, b2);
    CHECK(b1 > 0.01, "host note-off cut the UI's held note (rms %.4f)", b1);
    CHECK(b2 < kSilent, "note stuck after both sources released (rms %.6f)", b2);

    // C) UI queue overflow (audio stalled while the keyboard is played): no stuck notes.
    uiNote(62, 0.8f);
    for (int i = 0; i < 1500; ++i) uiNote(70 + (i % 5), (i & 1) ? 0.f : 0.8f);
    uiNote(62, 0.f);                       // dropped: the queue is full by now
    run(3.5);
    const double c1 = rmsTail(3.0);
    std::printf("  queue overflow: tail rms %.6f\n", c1);
    CHECK(c1 < kSilent, "notes stuck after UI queue overflow (rms %.6f)", c1);

    // D) Pitch wheel held by the UI while the host keeps sending pitch bend.
    hostNote(60, true);
    run(0.6);
    const double f0 = pitchHz();
    uiCtrl(kMidiPitchBend, 1.0, true);     // full bend up (+5 st default range)
    hostParam(kMidiPitchBend, 0.5);
    run(0.3);
    for (int i = 0; i < 4; ++i) { hostParam(kMidiPitchBend, 0.5); run(0.15); }
    run(0.6);
    const double f1 = pitchHz();
    hostParam(kMidiPitchBend, 0.75);       // host moves while the wheel is still held
    run(0.1);
    uiCtrl(kMidiPitchBend, 0.5, false);    // release: back to the host's bend (0.75 = +2.5 st)
    run(0.6);
    const double f2 = pitchHz();
    hostParam(kMidiPitchBend, 0.5);        // host owns it again
    run(0.6);
    const double f3 = pitchHz();
    hostNote(60, false);
    run(3.0);
    std::printf("  pitch wheel: base %.1f Hz, UI held +5 st %.1f Hz (x%.3f), released to host +2.5 st %.1f Hz (x%.3f), host centre %.1f Hz\n",
                f0, f1, f0 > 0 ? f1 / f0 : 0.0, f2, f0 > 0 ? f2 / f0 : 0.0, f3);
    // The init saw shows two rising crossings per cycle, so f0 reads 2 x 261.6 Hz; only ratios matter.
    CHECK(f0 > 200.0 && f0 < 1100.0, "base pitch %.1f Hz", f0);
    CHECK(f0 > 0 && std::fabs(f1 / f0 - std::pow(2.0, 5.0 / 12.0)) < 0.04, "held wheel overridden by host bend (x%.3f)", f0 > 0 ? f1 / f0 : 0.0);
    CHECK(f0 > 0 && std::fabs(f2 / f0 - std::pow(2.0, 2.5 / 12.0)) < 0.04, "released wheel did not return to host bend (x%.3f)", f0 > 0 ? f2 / f0 : 0.0);
    CHECK(f0 > 0 && std::fabs(f3 / f0 - 1.0) < 0.03, "host bend ignored after release (x%.3f)", f0 > 0 ? f3 / f0 : 0.0);

    inst.proc->setProcessing(false);
    inst.comp->setActive(false);
    inst.conn->release();
    inst.proc->release();
    inst.comp->terminate();
    inst.comp->release();
    factory->release();
    if (auto ex = reinterpret_cast<bool (*)()>(GetProcAddress(h, "ExitDll"))) ex();
    FreeLibrary(h);
}

static std::vector<Patch> testFormats(const std::wstring& zip)
{
    std::printf("[2] Preset formats: %ls\n", zip.c_str());
    std::vector<ZipBankFolder> folders;
    CHECK(readZipBank(zip, folders), "readZipBank failed");
    std::vector<Patch> all;
    for (auto& f : folders) {
        std::printf("  folder '%s': %zu patches\n", f.folder.c_str(), f.patches.size());
        all.insert(all.end(), f.patches.begin(), f.patches.end());
    }
    CHECK(!all.empty(), "no patches parsed");
    if (all.empty()) return all;

    int mismatches = 0;
    for (const Patch& p : all) {
        Patch a, b, c;
        parseSy1(writeSy1(p), a);
        const auto fxp = writeFxp(p);
        parseFxp(fxp, b);
        const auto chunk = writeSynth1Chunk(p);
        parseSynth1Chunk(chunk.data(), chunk.size(), c);
        for (int id = 0; id < kNumPatchParams; ++id)
            if (a.get(id) != p.get(id) || b.get(id) != p.get(id) || c.get(id) != p.get(id)) ++mismatches;
        if (b.name != p.name) ++mismatches;
    }
    CHECK(mismatches == 0, "%d round-trip mismatches", mismatches);

    std::vector<Patch> bank(all.begin(), all.begin() + std::min<size_t>(all.size(), 16));
    std::vector<Patch> back;
    CHECK(parseFxb(writeFxb(bank), back) && back.size() == bank.size(), "fxb round trip (%zu)", back.size());
    std::printf("  round trips: %zu patches, %d mismatches; fxp size %zu bytes\n", all.size(), mismatches, writeFxp(all[0]).size());
    return all;
}

static void render(const Patch& p, const std::wstring& outDir, int index)
{
    const int fs = 48000;
    SynthEngine eng;
    eng.setSampleRate(fs);
    for (int id = 0; id < kNumPatchParams; ++id) eng.setParam(id, p.get(id));

    // C major chord for 1.5 s, then 1.5 s tail; transport at 120 bpm.
    const int total = fs * 3, block = 256;
    std::vector<float> L(static_cast<size_t>(total)), R(static_cast<size_t>(total));
    std::vector<NoteEvent> ev;
    TransportInfo t;
    t.playing = true;
    double peak = 0, sumSq = 0;
    bool bad = false;
    for (int pos = 0; pos < total; pos += block) {
        ev.clear();
        if (pos == 0) for (int n : { 48, 52, 55, 60 }) ev.push_back({ 0, NoteEvent::NoteOn, n, 0.8f });
        if (pos <= fs * 3 / 2 && pos + block > fs * 3 / 2) for (int n : { 48, 52, 55, 60 }) ev.push_back({ fs * 3 / 2 - pos, NoteEvent::NoteOff, n, 0.f });
        const int n = std::min(block, total - pos);
        eng.process(&L[size_t(pos)], &R[size_t(pos)], n, ev, t);
        t.ppq += n / (fs * 0.5);
        for (int i = pos; i < pos + n; ++i) {
            if (!std::isfinite(L[size_t(i)]) || !std::isfinite(R[size_t(i)])) bad = true;
            peak = std::max(peak, double(std::max(std::fabs(L[size_t(i)]), std::fabs(R[size_t(i)]))));
            sumSq += double(L[size_t(i)]) * L[size_t(i)];
        }
    }
    const double rms = std::sqrt(sumSq / total);
    std::printf("  %03d %-24s peak %6.3f  rms %6.4f %s\n", index, p.name.c_str(), peak, rms, bad ? "NaN!" : "");
    CHECK(!bad, "%s produced NaN/Inf", p.name.c_str());
    CHECK(peak > 1e-4, "%s is silent", p.name.c_str());
    CHECK(peak < 4.0, "%s peak %.2f is far too hot", p.name.c_str(), peak);
    wchar_t file[64];
    swprintf(file, 64, L"\\render_%03d.wav", index);
    writeWav(outDir + file, L, R, fs);
}

static double peakOf(const Patch& p, int note = 60)
{
    const int fs = 48000, total = fs, block = 256;
    SynthEngine eng;
    eng.setSampleRate(fs);
    for (int id = 0; id < kNumPatchParams; ++id) eng.setParam(id, p.get(id));
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    std::vector<NoteEvent> ev;
    TransportInfo t;
    double peak = 0;
    for (int pos = 0; pos < total; pos += block) {
        ev.clear();
        if (pos == 0) ev.push_back({ 0, NoteEvent::NoteOn, note, 0.8f });
        eng.process(L.data(), R.data(), block, ev, t);
        for (int i = 0; i < block; ++i) peak = std::max(peak, double(std::fabs(L[size_t(i)])));
    }
    return peak;
}

static void diagnose(const Patch& base)
{
    struct Variant { const char* what; std::vector<std::pair<int, int>> set; };
    const Variant vs[] = {
        { "as is", {} },
        { "no delay/chorus/fx", { { kDlyOn, 0 }, { kChoOn, 0 }, { kFxOn, 0 } } },
        { "eq flat", { { kEqTone, 64 }, { kEqLevel, 64 } } },
        { "filter open", { { kFltFreq, 127 }, { kFltAmt, 64 } } },
        { "lfos off", { { kLfo1On, 0 }, { kLfo2On, 0 } } },
        { "amp gain max", { { kAmpGain, 127 } } },
        { "osc mix 0", { { kOscMix, 0 } } },
    };
    std::printf("  diagnose '%s':", base.name.c_str());
    for (const auto& v : vs) {
        Patch p = base;
        for (auto& kv : v.set) p.set(kv.first, kv.second);
        std::printf("  [%s] %.3f", v.what, peakOf(p));
    }
    std::printf("\n");
}

static void collectFiles(const std::wstring& dir, const wchar_t* ext, std::vector<std::wstring>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        const std::wstring full = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) collectFiles(full, ext, out);
        else if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, ext) == 0) out.push_back(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }

static void testValidation(const std::wstring& presetsDir, const std::wstring& zipDir)
{
    std::printf("[4] Compatibility validation\n");

    // --- Genuine Synth1 content must be accepted.
    std::vector<std::wstring> sy1, zips, fxps, fxbs;
    collectFiles(presetsDir, L".sy1", sy1);
    collectFiles(zipDir, L".zip", zips);
    collectFiles(presetsDir, L".fxp", fxps);
    collectFiles(presetsDir, L".fxb", fxbs);
    int ok = 0, bad = 0;
    std::map<std::string, int> reasons;
    for (auto& f : sy1) {
        std::vector<uint8_t> b;
        Patch p;
        std::string why;
        if (readFileBytes(f, b) && parseSy1(std::string(b.begin(), b.end()), p, &why)) ++ok;
        else { ++bad; ++reasons[why]; }
    }
    std::printf("  .sy1 library: %d accepted, %d rejected\n", ok, bad);
    for (auto& r : reasons) std::printf("      %5d x %s\n", r.second, r.first.c_str());

    int zOk = 0, zBad = 0, entriesRejected = 0;
    for (auto& z : zips) {
        std::vector<ZipBankFolder> folders;
        std::string why;
        int rej = 0;
        if (readZipBank(z, folders, &why, &rej)) ++zOk;
        else { ++zBad; std::printf("      zip rejected: %ls (%s)\n", z.c_str() + z.find_last_of(L'\\') + 1, why.c_str()); }
        entriesRejected += rej;
    }
    std::printf("  zip banks: %d accepted, %d rejected, %d bad entries skipped\n", zOk, zBad, entriesRejected);

    for (auto* list : { &fxps, &fxbs }) {
        for (auto& f : *list) {
            std::vector<uint8_t> b;
            readFileBytes(f, b);
            std::string why;
            Patch p;
            std::vector<Patch> bank;
            const bool isFxb = list == &fxbs;
            const bool r = isFxb ? parseFxb(b, bank, &why) : parseFxp(b, p, &why);
            std::printf("      %s %-40ls %s\n", r ? "accept" : "REJECT", f.c_str() + f.find_last_of(L'\\') + 1, r ? "" : why.c_str());
        }
    }

    // --- Foreign / corrupt data must be rejected.
    Patch init = Patch::makeInit();
    auto expectReject = [&](const char* what, bool accepted, const std::string& why) {
        std::printf("  %-44s -> %s %s\n", what, accepted ? "accepted" : "rejected:", accepted ? "" : why.c_str());
        CHECK(!accepted, "%s was accepted", what);
    };
    {   // fxp from another plugin (same layout, foreign fxID)
        auto f = writeFxp(init);
        f[16] = 'X'; f[17] = 'f'; f[18] = 's'; f[19] = 'X';
        Patch p; std::string why;
        expectReject("fxp with foreign fxID 'XfsX'", parseFxp(f, p, &why), why);
    }
    {   // fxp with right ID but foreign opaque chunk
        auto f = writeFxp(init);
        std::memcpy(&f[60], "SerumPresetData\0", 16);
        Patch p; std::string why;
        expectReject("fxp 'S1Vs' with foreign chunk data", parseFxp(f, p, &why), why);
    }
    {   // truncated fxp
        auto f = writeFxp(init);
        f.resize(f.size() / 2);
        Patch p; std::string why;
        expectReject("truncated fxp", parseFxp(f, p, &why), why);
    }
    {   // FxCk with another plugin's parameter count
        std::vector<uint8_t> f;
        be32(f, 0x43636E4B); be32(f, 48 + 8 * 4); be32(f, 0x4678436B); be32(f, 1); be32(f, 0x53315673); be32(f, 1); be32(f, 8);
        f.insert(f.end(), 28, 0);
        for (int i = 0; i < 8; ++i) be32(f, 0x3F000000);
        Patch p; std::string why;
        expectReject("FxCk with 8 parameters", parseFxp(f, p, &why), why);
    }
    {   // FxBk bank from another plugin
        std::vector<uint8_t> f;
        be32(f, 0x43636E4B); be32(f, 152); be32(f, 0x4678426B); be32(f, 1); be32(f, 0x58667358); be32(f, 1); be32(f, 0);
        f.insert(f.end(), 128 + 4, 0);
        std::vector<Patch> b; std::string why;
        expectReject("FxBk bank with foreign fxID", parseFxb(f, b, &why), why);
    }
    {   // random binary named .sy1
        std::string s(400, '\0');
        Rng r;
        for (auto& c : s) c = char(r.next());
        Patch p; std::string why;
        expectReject("binary data as .sy1", parseSy1(s, p, &why), why);
    }
    {   // plain text
        Patch p; std::string why;
        expectReject("text file as .sy1", parseSy1("Hello\nthis is a readme\nnot a patch\n", p, &why), why);
    }
    {   // too few parameters (e.g. another synth's id,value text)
        Patch p; std::string why;
        expectReject(".sy1 with 5 parameters", parseSy1("Foreign\n0,1\n1,2\n2,3\n3,4\n4,1\n", p, &why), why);
    }
    {   // JSON / XML presets
        Patch p; std::string why;
        expectReject("JSON preset as .sy1", parseSy1("{\"name\":\"pad\",\"cutoff\":0.5}\n", p, &why), why);
    }
}

// ---------------------------------------------------------------------------
// Screenshot: hosts the real editor (IPlugView) in a window and captures it.
// ---------------------------------------------------------------------------
static bool savePng(HBITMAP bmp, const std::wstring& path)
{
    Gdiplus::Bitmap b(bmp, nullptr);
    CLSID png;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    std::vector<uint8_t> buf(size);
    auto* enc = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, enc);
    for (UINT i = 0; i < num; ++i) if (wcscmp(enc[i].MimeType, L"image/png") == 0) png = enc[i].Clsid;
    return b.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

static void pump(DWORD ms)
{
    const ULONGLONG until = GetTickCount64() + ms;
    MSG m;
    while (GetTickCount64() < until) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(10);
    }
}

static void capture(HWND host, const std::wstring& path)
{
    RECT rc;
    GetClientRect(host, &rc);
    HDC wdc = GetDC(host);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mdc, bmp);
    PrintWindow(host, mdc, PW_CLIENTONLY | 2 /*PW_RENDERFULLCONTENT*/);
    SelectObject(mdc, old);
    std::printf("  %s %ls\n", savePng(bmp, path) ? "saved" : "FAILED", path.c_str());
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(host, wdc);
}

static int screenshot(const std::wstring& binary, const std::wstring& outDir)
{
    std::printf("[5] Editor screenshot\n");
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiTok = 0;
    Gdiplus::GdiplusStartup(&gdiTok, &gsi, nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HMODULE h = LoadLibraryW(binary.c_str());
    if (!h) { std::printf("  cannot load plugin\n"); return 1; }
    if (auto init = reinterpret_cast<bool (*)()>(GetProcAddress(h, "InitDll"))) init();
    auto gf = reinterpret_cast<Steinberg::IPluginFactory* (*)()>(GetProcAddress(h, "GetPluginFactory"));
    Steinberg::IPluginFactory* factory = gf();
    Steinberg::TUID cid;
    cs::kControllerUID.toTUID(cid);
    Steinberg::Vst::IEditController* ctl = nullptr;
    factory->createInstance(cid, Steinberg::Vst::IEditController::iid, reinterpret_cast<void**>(&ctl));
    if (!ctl) { std::printf("  cannot create controller\n"); return 1; }
    ctl->initialize(nullptr);
    Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view) { std::printf("  createView failed\n"); return 1; }

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CSShotHost";
    RegisterClassW(&wc);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"ClassicSynth snapshot", WS_POPUP | WS_VISIBLE,
                                40, 40, 720, 480, nullptr, nullptr, wc.hInstance, nullptr);
    view->attached(host, Steinberg::kPlatformTypeHWND);
    Steinberg::ViewRect r;
    view->getSize(&r);
    SetWindowPos(host, HWND_TOPMOST, 40, 40, r.getWidth(), r.getHeight(), SWP_SHOWWINDOW);
    std::printf("  view size %dx%d\n", r.getWidth(), r.getHeight());

    pump(2200);   // past the splash
    capture(host, outDir + L"\\editor_main.png");

    HWND child = GetWindow(host, GW_CHILD);
    auto click = [&](float x, float y) {   // DIP coordinates on the 720-wide design canvas
        Steinberg::ViewRect cur;
        view->getSize(&cur);
        const float s = cur.getWidth() / 720.f;
        const LPARAM pt = MAKELPARAM(int(x * s), int(y * s));
        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, pt);
        SendMessageW(child, WM_LBUTTONUP, 0, pt);
        pump(300);
    };
    auto followSize = [&] {                // what a host does after resizeView()
        Steinberg::ViewRect cur;
        view->getSize(&cur);
        SetWindowPos(host, nullptr, 40, 40, cur.getWidth(), cur.getHeight(), SWP_NOZORDER);
        pump(600);
        return cur;
    };

    // Keyboard strip switch: "keys" button at (652..672, 8..32) toggles 720x580 <-> 720x480.
    const bool keysInitially = r.getHeight() == 580;
    CHECK(r.getWidth() == 720 && (r.getHeight() == 580 || r.getHeight() == 480), "initial size %dx%d", r.getWidth(), r.getHeight());
    for (int i = 0; i < 2; ++i) {
        click(662, 20);
        const Steinberg::ViewRect cur = followSize();
        const bool keys = cur.getHeight() == 580;
        std::printf("  keys %s -> %dx%d\n", keys ? "on" : "off", cur.getWidth(), cur.getHeight());
        CHECK(cur.getWidth() == 720 && cur.getHeight() == (keys ? 580 : 480), "keys toggle size %dx%d", cur.getWidth(), cur.getHeight());
        if (!keys) capture(host, outDir + L"\\editor_nokeys.png");
    }
    {
        const Steinberg::ViewRect cur = followSize();
        if (cur.getHeight() != 580) { click(662, 20); followSize(); }   // zoom checks below run with the keyboard shown
    }

    // Zoom menu: button at (630..650, 8..32); items are 17 DIP rows starting at y = 37.
    const int expectW[3] = { 720, 1024, 1920 }, expectH[3] = { 580, 825, 1547 };
    const wchar_t* shots[3] = { L"\\editor_zoom_normal.png", L"\\editor_zoom_large.png", L"\\editor_zoom_xl.png" };
    for (int z : { 1, 2, 0 }) {
        click(640, 20);
        if (z == 1) capture(host, outDir + L"\\editor_zoom_menu.png");
        click(640, 37.f + z * 17.f + 8.f);
        const Steinberg::ViewRect cur = followSize();
        std::printf("  zoom %d -> %dx%d\n", z, cur.getWidth(), cur.getHeight());
        CHECK(cur.getWidth() == expectW[z] && cur.getHeight() == expectH[z], "zoom %d size %dx%d", z, cur.getWidth(), cur.getHeight());
        capture(host, outDir + shots[z]);
    }

    // Open the preset browser: click PRESETS (544..604, 8..32 in DIPs).
    click(574, 20);
    pump(1200);
    capture(host, outDir + L"\\editor_browser.png");
    if (!keysInitially) {   // the switch is remembered in settings.ini: put it back
        SendMessageW(child, WM_KEYDOWN, VK_ESCAPE, 0);
        pump(300);
        click(662, 20);
    }

    view->removed();
    view->release();
    ctl->terminate();
    ctl->release();
    DestroyWindow(host);
    factory->release();
    Gdiplus::GdiplusShutdown(gdiTok);
    std::printf("\n%s (%d failures)\n", gFailures ? "FAILED" : "ALL PASSED", gFailures);
    return gFailures ? 1 : 0;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 5 && std::wstring(argv[4]) == L"--screenshot")
        return screenshot(argv[1], argv[3]);
    if (argc >= 6 && std::wstring(argv[4]) == L"--validate") {
        testValidation(argv[5], argc >= 7 ? argv[6] : L"");
        std::printf("\n%s (%d failures)\n", gFailures ? "FAILED" : "ALL PASSED", gFailures);
        return gFailures ? 1 : 0;
    }
    if (argc >= 5 && std::wstring(argv[4]) == L"--diagnose") {
        std::vector<ZipBankFolder> folders;
        readZipBank(argv[2], folders);
        for (int idx : { 70, 90, 45, 85, 15 }) diagnose(folders[0].patches[size_t(idx)]);
        return 0;
    }
    if (argc < 4) { std::printf("usage: EngineTest <ClassicSynth.vst3 binary> <bank.zip> <outdir>\n"); return 2; }
    testFactory(argv[1]);
    auto patches = testFormats(argv[2]);

    std::printf("[3] Rendering\n");
    render(Patch::makeInit(), argv[3], 0);
    for (size_t i = 0; i < patches.size(); i += std::max<size_t>(1, patches.size() / 24))
        render(patches[i], argv[3], int(i) + 1);
    testMixedInput(argv[1]);

    std::printf("\n%s (%d failures)\n", gFailures ? "FAILED" : "ALL PASSED", gFailures);
    return gFailures ? 1 : 0;
}
