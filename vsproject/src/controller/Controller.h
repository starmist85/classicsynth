#pragma once

#include "presets/Patch.h"
#include "presets/PresetLibrary.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <functional>
#include <string>

namespace cs {

class Editor;

class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IEditController*>(new Controller()); }

    // EditController
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // IMidiMapping
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) override;

    // --- API for the editor
    int  value(int id) const;                          // raw patch value
    void setValueFromUI(int id, int value);            // performEdit with begin/end
    void beginGesture(int id);
    void setValueInGesture(int id, int value);
    void endGesture(int id);

    Patch currentPatch() const;
    void  loadPatch(const Patch& p);                   // pushes every parameter to the host
    void  auditionPatch(const Patch& p);               // plays C3 with p (does not change params)
    void  panic();                                     // hard-stop the audio engine
    void  playNote(int pitch, float velocity);         // on-screen keyboard (velocity 0 = note off)
    // On-screen wheels: id = kMidiPitchBend or kMidiCCBase + cc, normalized value;
    // held = the wheel is being dragged (it then has priority over host MIDI).
    void   setMidiController(int id, double value, bool held);
    double midiController(int id) { return getParamNormalized(Steinberg::Vst::ParamID(id)); }
    const std::string& programName() const { return mProgramName; }
    void  setProgramName(const std::string& n);

    PresetLibrary& library() { return mLibrary; }
    bool splashShown = false;                          // splash only on the first editor open

    // Editor zoom: 0 = 100 %, 1 = 1024 px wide, 2 = 1920 px wide; and whether the
    // keyboard strip is shown. Stored in the project (controller state) and
    // remembered as the default for new instances.
    static constexpr int kNumZooms = 3;
    static float zoomScale(int index);
    static const wchar_t* zoomLabel(int index);
    int  zoom() const { return mZoom; }
    void setZoom(int index);
    bool keyboard() const { return mKeyboard; }
    void setKeyboard(bool on);

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    void editorAttached(Editor* e) { mEditor = e; }
    void editorDetached(Editor* e) { if (mEditor == e) mEditor = nullptr; }

    OBJ_METHODS(Controller, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

private:
    void sendProgramName();

    std::string   mProgramName = "init";
    PresetLibrary mLibrary;
    Editor*       mEditor = nullptr;
    int           mZoom = 0;
    bool          mKeyboard = true;
};

} // namespace cs
