# Synth1 V1.13 (x64) — reverse-engineering notes

Target: `original/Synth1 VST64.dll` (650,240 bytes, SHA-256 `51c6fe60…f32c7`), "V1.13 64bit beta3 2014.7".
Tooling: REA 6.7.0 (MCP + CLI) driving Ghidra 12.1.4 headless; 981 application procedures
batch-decompiled through REA's `batch_decompile`. Image base `0x180000000`.

Everything below was recovered from the binary unless marked *(chosen)*, which means
the value could not be fully pinned down and ClassicSynth uses a reasoned substitute.

## 1. Plugin identity (VST2 `AudioEffectX`)

`VSTPluginMain` @0x18001ac10 → `createEffectInstance` @0x1800184a0 → `Synth1VST::Synth1VST` @0x180017a40

| Property | Value |
|---|---|
| uniqueID | `0x53315673` = **'S1Vs'** (used as fxID in .fxp/.fxb) |
| numParams | 255 (patch IDs 0..98 + info slots) |
| numPrograms | 1, or 0x3201 when the "host select program" option is on |
| I/O | 0 inputs, 2 outputs |
| flags | `programsAreChunks` (0x20), `isSynth` (0x100), `canReplacing` (0x10) |
| vendor / product | "Daichi" / "Synth1 Soft Synth", effect name "Synth1 VSTi" |

Classes (RTTI): `Synth1VST : AudioEffectX`, `Synth1GUI : AEffEditor`, `CSynthesizer1`, `CVoice`,
`CVco`, `CVcf`, `CVca`, `CEnvelopedObject`, `CLFO`, `CArpeggiator`, `CMixDevice`, `CEffector`,
`CDistortion`, `CDecimator`, `CRingMod`, `CCompressor`/`SimpleComp`/`EnvelopeDetector`, `CPhaser`,
`CEq`, `CPan`, `CDelay`, `CChorus`, `CBank`, `CPatch`, `CSoundData`, `CKeyboardInfo`.

## 2. Audio routing

From the `CSynthesizer1` constructor @0x180025110 (`connect(dst, src)` = FUN_18001b5c0):

```
CArpeggiator ─► CSynthesizer1 ─► 32 × CVoice (0x4A8 bytes each)
                                   ├─ +0x050 CVco   (OSC1 + sub, OSC2, mod env)
                                   ├─ +0x220 CVcf   (filter + filter env)
                                   └─ +0x3F0 CVca   (amp env)
32 voices ─► CMixDevice ─► CEffector ─► CEq (peak, init 1 kHz, Q 8) ─► CEq (tone LP/HP, init 10 kHz)
          ─► CPan (LFO-modulated) ─► CDelay (with its own tone CEq) ─► CChorus ─► out L/R
Two global CLFO instances @+0xab70 / +0xacd0 feed voices, filter, amp and pan.
```

## 3. Parameters

Table @.data `0x180093f10`: 98 entries × 40 bytes `{int id, int min, int max, int flags, char* name, int automatable, char* shortName}`.
Patch ID 94 (polyphony 1..32) only appears in the patch format. Full list with ranges and labels:
`src/params/ParamTable.cpp`.

### Value curves — `CSynthesizer1::setParameter` @0x180028fe0 (x = v/127)

| Param | Curve |
|---|---|
| env attack (mod/filter/amp) | `0.0005 · 56000^x` s (0.5 ms … 28 s) |
| env decay / release | `0.01 · 6000^x` s (10 ms … 60 s) |
| amp sustain, amp gain | `2048^(x^0.3) / 2048` |
| filter sustain | linear x |
| osc2 pitch | `((v·120)/127 − 60)` semitones (±5 oct) |
| osc2 fine, osc1,2 fine | dead zone 62..66 → 0, else `v−62` / `v−66` cents |
| filter freq | cutoff index `x·1023` into a 1024-step table `15 Hz · b^(i/1024)` (FUN_1800381f0) |
| filter amount | `(v−63)/64` bipolar, applied in the 0..1024 index space |
| filter key track | ratio `(trk+1)^((note−48)/12)` (FUN_180038070) |
| sub gain | `v·4/127`, mixed as `g/(g+1)` with auto level compensation |
| EQ freq / level / Q | `50·320^x` Hz / `(v−64)/127·50` dB / `10^x` |
| EQ tone, delay tone | v ≤ 64: high-cut `200·110^(v/64)` Hz; v > 64: low-cut `10·800^((v−64)/64)` Hz |
| delay time | table (.data 0x180093a90) in 1/24 quarter: `0,1,2,3,4,6,8,9,12,16,18,21,24,32,36,42,48,72,84,96`; seconds = `t·60/(bpm·24)`; 0 = 0.1 ms |
| delay spread | grows as `1000^|x|` (FUN_180025ae0) *(scale chosen: up to ≈100 ms)* |
| chorus time | x < 0.5: `0.05·300^((2x)^0.6)` ms, else `30x` ms |
| chorus depth / rate / fdbk | `(100^x−1)/99` / `v==0 ? 0.01 : 0.02·20000^x` Hz / `(v−64)/64·0.99` |
| LFO speed (free) | `1500^x / 12` Hz (CLFO FUN_180015210) |
| LFO speed (tempo) | 30-step table (.rdata 0x180073cf0): `1536…1` ticks (16 bars … 1/32T) |
| LFO depth | pitch dests: `(10^x−1)/9`, others linear; ranges per destination (FUN_180015080) |
| midi ctrl slots | `value += (max−min+1) · source · sens`, sens = `(v−63)/63` |

### Option lists (GUI string pool .rdata 0x180076090…)

* OSC1: sine, tri, saw, pulse — OSC2 (1..4): tri, saw, pulse, noise — sub: sine, tri, saw, square
* Filter: LP12, LP24, HP12, BP12, LPDL
* LFO wave (index): 0 saw, 1 tri, 2 square, 3 S&H, 4 smooth random, 5 sine (slider order 0,1,5,2,3,4)
* LFO dest (1..7): osc2, osc1,2, filter, amp, p/w, FM, pan
* Effect: a.d.1, a.d.2, d.d., deci., r.m., comp., ph.1, ph.2, ph.3, ph.4
* Delay: ST, X, PP — Chorus: 1..4 stages — Arp: updown, up, down, random / 1..4 oct / 19 beats
* Play mode: poly, mono, legato — MIDI sources: CC1..CC11, channel aftertouch, pitch bend

### Envelope (`CEnvelopedObject` @0x18000c6e0)

States 0 idle, 1 attack, 2 decay, 3 sustain, 4 release. Stage lengths are sample counts.
Attack is a linear ramp; decay/release use a piecewise slope that steepens at 1/2, 1/3, 1/4,
1/10 and 1/20 of the level (FUN_18000c780) — modelled as an exponential reaching 1/20 at the stage time.

### Oscillators

Per Daichi's own notes (daichilab.sakura.ne.jp/synthprog): band-limited 2048-point wavetables with
several tables per range, 16:16 fixed-point phase, pulse = difference of two offset saws, M-sequence
noise, FM = OSC2 modulating OSC1 phase, ~72 specialised render variants for ring/sync/FM combinations.
The binary's CVco region (0x18003b880…0x180052370) indeed contains ~100 large SSE render variants.
Saw is the *descending* type since V1.11 (+1 at phase 0).

## 4. Preset formats

### `.sy1` (text, FUN_18001e710 writer / FUN_18001e1e0 reader)
```
<program name>
color=<name>
ver=<n>            (113 for V1.13)
<id>,<value>       (one per parameter; older files simply omit newer IDs)
```

### VST chunk — `makeVSTChunkData()` @0x180027db0 (0xA40 bytes)
```
+0x000  "Synth1 VST Chunk Data\0"
+0x020  int32 2                     chunk version
+0x024  int32 host-select-program flag
+0x028  char  name[]
+0x230  int32 0x71                  patch version (113)
+0x234  int32 0x100                 slot count
+0x238  { int32 present, int32 value } × 256, slot = parameter ID
        slot 250 = version, 251 = colour, 252/254 = program info
```
`getChunk()` @0x180019280 ignores `isPreset` and always returns this single-program chunk, so a
host-saved `.fxb` for Synth1 holds one program. Validation @0x180027b90 also accepts the legacy
"Synth1 DXi Data Header V1.0" (0x814 bytes).

### `.zip` banks
Read-only external banks: any folder hierarchy of `NNN.sy1` (+ `readme.txt`), unzipped with zlib 1.2.5 / unzip 1.01.

## 5. ClassicSynth mapping

* VST3 parameter IDs = Synth1 patch IDs (0..98) + master volume (100). Hidden MIDI CC/AT/PB
  parameters (1000+) via `IMidiMapping`; meters (1200+) are read-only output parameters.
* `.fxp` / `.fxb` written by ClassicSynth use fxID 'S1Vs' and the exact Synth1 chunk, so
  Synth1 can load them; ClassicSynth banks store N chunks behind a "ClassicSynth Bank" header.
