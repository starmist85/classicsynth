# Builds and runs the ClassicSynth test harness (requires a Release build of the plugin).
#   powershell -ExecutionPolicy Bypass -File vsproject\tests\build-and-run.ps1
param([string]$Bank = "$PSScriptRoot\..\..\original\zipbank\factory preset.zip")
$ErrorActionPreference = 'Stop'
$proj = Resolve-Path "$PSScriptRoot\.."
$out = Join-Path $proj 'build\tests'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$src = @(
    'tests\EngineTest.cpp', 'src\params\ParamTable.cpp', 'src\params\ParamDisplay.cpp',
    'src\dsp\Oscillator.cpp', 'src\dsp\Filter.cpp', 'src\dsp\Lfo.cpp', 'src\dsp\Voice.cpp',
    'src\dsp\Arpeggiator.cpp', 'src\dsp\Effects.cpp', 'src\dsp\SynthEngine.cpp',
    'src\presets\Patch.cpp', 'src\presets\PresetFormats.cpp',
    'external\vst3sdk\pluginterfaces\base\coreiids.cpp', 'external\vst3sdk\pluginterfaces\base\funknown.cpp',
    'external\vst3sdk\public.sdk\source\vst\vstinitiids.cpp'
) | ForEach-Object { '"' + (Join-Path $proj $_) + '"' }
$miniz = '"' + (Join-Path $proj 'external\miniz\miniz.c') + '"'
$inc = "/I`"$proj\src`" /I`"$proj\external\miniz`" /I`"$proj\external\vst3sdk`""
$cmd = "`"$vcvars`" >nul && cd /d `"$out`" && cl /nologo /c /O2 /DMINIZ_NO_TIME /TC $miniz && " +
       "cl /nologo /std:c++20 /EHsc /O2 /arch:AVX2 /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DRELEASE=1 $inc " +
       ($src -join ' ') + " miniz.obj ole32.lib user32.lib gdi32.lib gdiplus.lib /Fe:EngineTest.exe"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'test build failed' }

$bin = Join-Path $proj 'build\Release\ClassicSynth.vst3\Contents\x86_64-win\ClassicSynth.vst3'
& (Join-Path $out 'EngineTest.exe') $bin (Resolve-Path $Bank) $out
