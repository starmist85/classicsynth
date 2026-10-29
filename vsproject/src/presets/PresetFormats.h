#pragma once
// Preset file formats compatible with Synth1:
//
//  .sy1   text patch:   <name>\n color=<c>\n ver=<n>\n <id>,<value>\n ...
//  chunk  Synth1 VST chunk (0xA40 bytes) recovered from makeVSTChunkData()
//         @0x180027db0 / parse @0x180027b90:
//           +0x000 "Synth1 VST Chunk Data\0"
//           +0x020 int32 chunk version (2)
//           +0x024 int32 host-select-program flag
//           +0x028 char  program name[...]
//           +0x230 int32 patch version (0x71 = 113)
//           +0x234 int32 slot count (0x100)
//           +0x238 + id*8 { int32 present, int32 value } for id 0..255
//                  (slot 250 = version, 251 = colour, 252/254 = program info)
//  .fxp   VST2 program:  'CcnK' .. 'FPCh' (opaque chunk) or 'FxCk' (floats), fxID 'S1Vs'
//  .fxb   VST2 bank:     'CcnK' .. 'FBCh' (opaque chunk) or 'FxBk' (list of FxCk)
//         Synth1's getChunk() ignores isPreset, so an FBCh written by a host for
//         Synth1 holds one program chunk. ClassicSynth banks store N program
//         chunks behind a "ClassicSynth Bank" header inside the FBCh.
//  .zip   external bank: folders of NNN.sy1 files (read only).

#include "presets/Patch.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs {

inline constexpr uint32_t kSynth1FxID = 0x53315673;   // 'S1Vs' (AudioEffect::setUniqueID)
inline constexpr int      kSynth1ChunkSize = 0xA40;

// ---- Version upgrade (CSynthesizer1 FUN_180026a10): converts patches saved by
// older Synth1 versions (ver < 113) to the V1.13 value curves. Fall-through:
// 100 -> 105/106 -> 107/108 -> 109 -> 111. Called by the parsers below.
void upgradePatchVersion(Patch& p);

// Compatibility checks
// --------------------
// Every loader validates structure and identity and rejects data that was not
// written by Synth1 / ClassicSynth ("why" receives a short reason):
//   .sy1   first line = name; every other line must be "color=..", "ver=<100..199>"
//          or "<id>,<value>" with id 0..255; at least kMinCoreParams of the
//          V1.05 core IDs (0..72) must be present.
//   chunk  "Synth1 VST Chunk Data" header, size >= 0x550, chunk version 1..2,
//          slot count 0x100, present flags 0/1, core-parameter minimum.
//   .fxp   'CcnK' + 'FPCh'/'FxCk', fxID 'S1Vs', consistent byte sizes; FxCk must
//          declare Synth1's parameter count and hold finite 0..1 floats.
//   .fxb   'CcnK' + 'FBCh'/'FxBk', fxID 'S1Vs', consistent sizes; every program
//          is validated as above.
//   .zip   each .sy1 validated; the zip is rejected when none is compatible.
// Out-of-range values inside an otherwise valid Synth1 file are tolerated and
// clamped (genuine Synth1 libraries contain them), see Patch::set.
inline constexpr int kMinCoreParams = 40;

// ---- .sy1
bool parseSy1(const std::string& text, Patch& out, std::string* why = nullptr);
std::string writeSy1(const Patch& p);

// ---- Synth1 VST chunk
bool parseSynth1Chunk(const uint8_t* data, size_t size, Patch& out, std::string* why = nullptr);
std::vector<uint8_t> writeSynth1Chunk(const Patch& p, int programIndex = 0);

// ---- .fxp / .fxb
bool parseFxp(const std::vector<uint8_t>& file, Patch& out, std::string* why = nullptr);
std::vector<uint8_t> writeFxp(const Patch& p);
bool parseFxb(const std::vector<uint8_t>& file, std::vector<Patch>& out, std::string* why = nullptr);
std::vector<uint8_t> writeFxb(const std::vector<Patch>& bank);

// ---- .zip (all compatible .sy1 entries, grouped by folder)
struct ZipBankFolder {
    std::string folder;                 // path inside the zip ("" = root)
    std::vector<int> programNumbers;    // from NNN.sy1 (1-based), or sequence
    std::vector<Patch> patches;
};
bool readZipBank(const std::wstring& zipPath, std::vector<ZipBankFolder>& out, std::string* why = nullptr,
                 int* rejectedEntries = nullptr);

// ---- helpers
bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out);
bool writeFileBytes(const std::wstring& path, const std::vector<uint8_t>& data);

// Synth1 VST float (0..1) -> raw value, following the bucketing of FUN_180025c10.
int synth1FloatToValue(int id, float f);
float synth1ValueToFloat(int id, int v);

} // namespace cs
