#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace cs {

// Class IDs. Never change these after release - hosts store them in projects.
static const Steinberg::FUID kProcessorUID(0x5A71C3E2, 0x4B8D4F19, 0x9E2A6C51, 0xD3B07F46);
static const Steinberg::FUID kControllerUID(0x1C94B7A8, 0x62E34D0B, 0xA5F8193C, 0x7E4D26B1);

// Message IDs between controller and processor.
inline constexpr const char* kMsgProgramName = "CS.ProgramName";
inline constexpr const char* kMsgAttrName    = "name";
// Panic: hard-stop all voices and clear effect tails.
inline constexpr const char* kMsgPanic       = "CS.Panic";
// Preset browser audition: binary attr "values" = int32[kNumPatchParams].
inline constexpr const char* kMsgAudition    = "CS.Audition";
inline constexpr const char* kMsgAttrValues  = "values";
inline constexpr int         kAuditionNote   = 60;     // C3 (Yamaha/Cubase naming, middle C)
inline constexpr double      kAuditionSeconds = 1.0;
// On-screen keyboard: int attr "pitch", float attr "velocity" (0 = note off).
inline constexpr const char* kMsgNote        = "CS.Note";
inline constexpr const char* kMsgAttrPitch   = "pitch";
inline constexpr const char* kMsgAttrVelocity = "velocity";
// On-screen wheels: int attr "id" (kMidiPitchBend / kMidiCCBase + cc), float attr "value" (normalized),
// int attr "held" (1 while dragged: host MIDI for that controller is ignored until released).
inline constexpr const char* kMsgMidiCtrl    = "CS.MidiCtrl";
inline constexpr const char* kMsgAttrId      = "id";
inline constexpr const char* kMsgAttrValue   = "value";
inline constexpr const char* kMsgAttrHeld    = "held";

} // namespace cs
