#include "controller/Controller.h"
#include "params/ParamDisplay.h"
#include "pluginids.h"
#include "processor/Processor.h"
#include "ui/Editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace cs {

// Parameter whose host-visible text uses the Synth1 value curves.
class SynthParameter : public Parameter {
public:
    SynthParameter(const ParamDef& d)
        : mDef(d)
    {
        UString(info.title, str16BufferSize(String128)).fromAscii(d.name);
        UString(info.shortTitle, str16BufferSize(String128)).fromAscii(d.shortName);
        info.id = ParamID(d.id);
        info.stepCount = d.maxValue - d.minValue;
        info.defaultNormalizedValue = cs::toNormalized(d, d.defValue);
        info.flags = d.automatable ? ParameterInfo::kCanAutomate : 0;
        if (d.kind == Kind::List) info.flags |= ParameterInfo::kIsList;
        valueNormalized = info.defaultNormalizedValue;
    }

    void toString(ParamValue normValue, String128 string) const override
    {
        const std::string s = formatParamValue(mDef.id, cs::fromNormalized(mDef, normValue));
        UString(string, 128).fromAscii(s.c_str());
    }

    bool fromString(const TChar* string, ParamValue& normValue) const override
    {
        char buf[128];
        UString(const_cast<TChar*>(string), 128).toAscii(buf, 128);
        int v = 0;
        if (!parseParamValue(mDef.id, buf, v)) return false;
        normValue = cs::toNormalized(mDef, v);
        return true;
    }

private:
    const ParamDef& mDef;
};

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------
static std::wstring settingsFile()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, appData))) return {};
    std::wstring dir = std::wstring(appData) + L"\\Starmist Audio\\ClassicSynth";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

float Controller::zoomScale(int index)
{
    static const float kScales[kNumZooms] = { 1.f, 1024.f / 720.f, 1920.f / 720.f };
    return kScales[index < 0 || index >= kNumZooms ? 0 : index];
}

const wchar_t* Controller::zoomLabel(int index)
{
    static const wchar_t* kLabels[kNumZooms] = { L"Normal", L"Large", L"Extra large" };
    return kLabels[index < 0 || index >= kNumZooms ? 0 : index];
}

void Controller::setZoom(int index)
{
    if (index < 0 || index >= kNumZooms) return;
    mZoom = index;
    const std::wstring ini = settingsFile();
    if (!ini.empty()) WritePrivateProfileStringW(L"editor", L"zoom", std::to_wstring(index).c_str(), ini.c_str());
    if (mEditor) mEditor->applySize();
}

void Controller::setKeyboard(bool on)
{
    mKeyboard = on;
    const std::wstring ini = settingsFile();
    if (!ini.empty()) WritePrivateProfileStringW(L"editor", L"keyboard", on ? L"1" : L"0", ini.c_str());
    if (mEditor) mEditor->applySize();
}

// Controller-only state (stored in the host project): editor zoom (v1) + keyboard strip (v2).
tresult PLUGIN_API Controller::setState(IBStream* state)
{
    IBStreamer st(state, kLittleEndian);
    char magic[4];
    int32 version = 0, zoom = 0, keyboard = 1;
    if (st.readRaw(magic, 4) != 4 || std::memcmp(magic, "CSed", 4) != 0) return kResultFalse;
    if (!st.readInt32(version) || !st.readInt32(zoom)) return kResultFalse;
    if (zoom >= 0 && zoom < kNumZooms) mZoom = zoom;
    if (version >= 2 && st.readInt32(keyboard)) mKeyboard = keyboard != 0;
    if (mEditor) mEditor->applySize();
    return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* state)
{
    IBStreamer st(state, kLittleEndian);
    st.writeRaw("CSed", 4);
    st.writeInt32(2);
    st.writeInt32(mZoom);
    st.writeInt32(mKeyboard ? 1 : 0);
    return kResultOk;
}

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    const tresult r = EditController::initialize(context);
    if (r != kResultOk) return r;

    // Default zoom = last size chosen in any instance.
    const std::wstring ini = settingsFile();
    if (!ini.empty()) {
        mZoom = std::clamp(int(GetPrivateProfileIntW(L"editor", L"zoom", 0, ini.c_str())), 0, kNumZooms - 1);
        mKeyboard = GetPrivateProfileIntW(L"editor", L"keyboard", 1, ini.c_str()) != 0;
    }

    for (int i = 0; i < numHostParams(); ++i)
        parameters.addParameter(new SynthParameter(*paramAt(i)));

    // Hidden MIDI controller inputs (CC 0..127, channel aftertouch, pitch bend).
    for (int cc = 0; cc < 128; ++cc) {
        char name[32];
        std::snprintf(name, sizeof(name), "MIDI CC %d", cc);
        String128 title;
        UString(title, 128).fromAscii(name);
        parameters.addParameter(title, nullptr, 0, cc == 7 ? 100.0 / 127.0 : 0.0,
                                ParameterInfo::kIsHidden, ParamID(kMidiCCBase + cc));
    }
    parameters.addParameter(STR16("Aftertouch"), nullptr, 0, 0.0, ParameterInfo::kIsHidden, ParamID(kMidiAftertouch));
    parameters.addParameter(STR16("Pitch Bend"), nullptr, 0, 0.5, ParameterInfo::kIsHidden, ParamID(kMidiPitchBend));

    // Meters (processor -> controller output parameters).
    parameters.addParameter(STR16("CPU Load"), STR16("%"), 0, 0.0,
                            ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, ParamID(kMeterCpu));
    parameters.addParameter(STR16("Active Voices"), nullptr, 0, 0.0,
                            ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, ParamID(kMeterVoices));
    parameters.addParameter(STR16("Output Level L"), STR16("dB"), 0, 0.0,
                            ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, ParamID(kMeterOutL));
    parameters.addParameter(STR16("Output Level R"), STR16("dB"), 0, 0.0,
                            ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, ParamID(kMeterOutR));
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    ComponentState cs;
    if (!cs.read(state)) return kResultFalse;
    for (int id = 0; id <= kMasterVolume; ++id)
        if (const ParamDef* d = findParam(id)) setParamNormalized(ParamID(id), toNormalized(*d, cs.values[size_t(id)]));
    mProgramName = cs.name;
    if (mEditor) mEditor->onProgramChanged();
    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name)
{
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new Editor(this);
    return nullptr;
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16, CtrlNumber ctrl, ParamID& id)
{
    if (busIndex != 0) return kResultFalse;
    if (ctrl >= 0 && ctrl < 128) { id = ParamID(kMidiCCBase + ctrl); return kResultTrue; }
    if (ctrl == ControllerNumbers::kAfterTouch) { id = ParamID(kMidiAftertouch); return kResultTrue; }
    if (ctrl == ControllerNumbers::kPitchBend)  { id = ParamID(kMidiPitchBend); return kResultTrue; }
    return kResultFalse;
}

int Controller::value(int id) const
{
    const ParamDef* d = findParam(id);
    if (!d) return 0;
    return fromNormalized(*d, const_cast<Controller*>(this)->getParamNormalized(ParamID(id)));
}

void Controller::beginGesture(int id) { beginEdit(ParamID(id)); }
void Controller::endGesture(int id)   { endEdit(ParamID(id)); }

void Controller::setValueInGesture(int id, int v)
{
    const ParamDef* d = findParam(id);
    if (!d) return;
    const double n = toNormalized(*d, v);
    setParamNormalized(ParamID(id), n);
    performEdit(ParamID(id), n);
}

void Controller::setValueFromUI(int id, int v)
{
    beginGesture(id);
    setValueInGesture(id, v);
    endGesture(id);
}

Patch Controller::currentPatch() const
{
    Patch p;
    p.name = mProgramName;
    for (int id = 0; id < kNumPatchParams; ++id) p.set(id, value(id));
    return p;
}

void Controller::loadPatch(const Patch& p)
{
    for (int id = 0; id < kNumPatchParams; ++id) {
        const ParamDef* d = findParam(id);
        if (!d) continue;
        const double n = toNormalized(*d, p.get(id));
        beginEdit(ParamID(id));
        setParamNormalized(ParamID(id), n);
        performEdit(ParamID(id), n);
        endEdit(ParamID(id));
    }
    setProgramName(p.name);
}

void Controller::auditionPatch(const Patch& p)
{
    int32 values[kNumPatchParams];
    for (int id = 0; id < kNumPatchParams; ++id) values[id] = p.get(id);
    if (IPtr<IMessage> msg = owned(allocateMessage())) {
        msg->setMessageID(kMsgAudition);
        msg->getAttributes()->setBinary(kMsgAttrValues, values, uint32(sizeof(values)));
        sendMessage(msg);
    }
}

void Controller::panic()
{
    if (IPtr<IMessage> msg = owned(allocateMessage())) {
        msg->setMessageID(kMsgPanic);
        sendMessage(msg);
    }
}

void Controller::playNote(int pitch, float velocity)
{
    if (IPtr<IMessage> msg = owned(allocateMessage())) {
        msg->setMessageID(kMsgNote);
        msg->getAttributes()->setInt(kMsgAttrPitch, pitch);
        msg->getAttributes()->setFloat(kMsgAttrVelocity, velocity);
        sendMessage(msg);
    }
}

// Sent as a message rather than performEdit so the hidden MIDI parameters
// never end up in host automation; the local value drives the wheel display.
void Controller::setMidiController(int id, double value, bool held)
{
    value = std::clamp(value, 0.0, 1.0);
    setParamNormalized(ParamID(id), value);
    if (IPtr<IMessage> msg = owned(allocateMessage())) {
        msg->setMessageID(kMsgMidiCtrl);
        msg->getAttributes()->setInt(kMsgAttrId, id);
        msg->getAttributes()->setFloat(kMsgAttrValue, value);
        msg->getAttributes()->setInt(kMsgAttrHeld, held ? 1 : 0);
        sendMessage(msg);
    }
}

void Controller::setProgramName(const std::string& n)
{
    mProgramName = n;
    sendProgramName();
    if (mEditor) mEditor->onProgramChanged();
}

void Controller::sendProgramName()
{
    if (IPtr<IMessage> msg = owned(allocateMessage())) {
        msg->setMessageID(kMsgProgramName);
        msg->getAttributes()->setBinary(kMsgAttrName, mProgramName.data(), uint32(mProgramName.size()));
        sendMessage(msg);
    }
}

} // namespace cs
