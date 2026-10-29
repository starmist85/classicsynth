# Starmist Audio – ClassicSynth (VST3)

A VST3 re-implementation of Synth1 V1.13 with a modern 720 × 480 dark UI, plus an
optional 100 px strip with pitch/mod wheels and a 61-key keyboard (header "keys" switch).
Personal-use build. Reverse-engineering notes: [docs/ReverseEngineering.md](docs/ReverseEngineering.md).

## Build

Requirements: Visual Studio 2022 (or Build Tools 2022) with the C++ desktop workload;
.NET 8 SDK for the MSI (WiX v5 restores from NuGet).

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1          # Release plugin + MSI
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -NoMsi   # plugin only
```

Or open `vsproject\ClassicSynth.sln` in VS 2022 and build `Release|x64`.

Output: `vsproject\build\<Config>\ClassicSynth.vst3\` (bundle) — copy it to
`C:\Program Files\Common Files\VST3` or install the MSI from `vsproject\installer\bin\`.
Development bundles link the repository's `presets` and `zipbank` folders as factory
presets (directory junctions under `Contents\Resources\Presets`).

## Test

```powershell
powershell -ExecutionPolicy Bypass -File vsproject\tests\build-and-run.ps1
# extra modes (after the above):
vsproject\build\tests\EngineTest.exe x x x --validate <presets dir> <zipbank dir>
vsproject\build\tests\EngineTest.exe <bundle binary> x <out dir> --screenshot
```

* factory/registration check of the built `.vst3`
* `.sy1` / `.fxp` / chunk / `.fxb` round trips on the factory bank
* renders presets to WAV (`build\tests\render_*.wav`) and checks for NaN / silence / clipping
* compatibility validation against the whole library and crafted foreign files
* editor screenshots (`editor_main.png`, `editor_browser.png`)

## Layout

```
src/params      parameter table (Synth1 IDs), value display
src/dsp         oscillators, filter, envelopes, LFO, voice, arpeggiator, effects, engine
src/presets     .sy1 / VST chunk / .fxp / .fxb / .zip, version upgrade, library
src/processor   VST3 audio processor (+ preset audition engine, meters, panic)
src/controller  VST3 edit controller (parameters, MIDI mapping, presets)
src/ui          Direct2D editor, widgets, preset browser
resources/      logo, background, splash, icon, version info
installer/      WiX v5 MSI (install folder chooser, repair/remove)
external/       Steinberg VST3 SDK (MIT), miniz (MIT)
```
