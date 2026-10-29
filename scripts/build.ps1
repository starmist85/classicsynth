# Builds the ClassicSynth VST3 (Release x64) and, optionally, the MSI installer.
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # plugin + MSI
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -NoMsi     # plugin only
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Debug -NoMsi
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$NoMsi
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
$proj = Join-Path $root 'vsproject'

# --- Locate MSBuild (Visual Studio 2022 or Build Tools 2022 with the C++ workload)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 / Build Tools not found (vswhere.exe missing).' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild with the C++ (v143) toolset was not found.' }

Write-Host "== Building ClassicSynth ($Config|x64)" -ForegroundColor Magenta
& $msbuild (Join-Path $proj 'ClassicSynth.sln') /m /nologo /v:minimal "/p:Configuration=$Config" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }
Write-Host "Bundle: $proj\build\$Config\ClassicSynth.vst3"

if ($NoMsi -or $Config -ne 'Release') { return }

# --- MSI (WiX v5 via the .NET SDK; the WiX packages restore from NuGet)
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet -and (Test-Path "$env:ProgramFiles\dotnet\dotnet.exe")) { $dotnet = "$env:ProgramFiles\dotnet\dotnet.exe" }
if (-not $dotnet) { throw '.NET SDK not found; install it or use -NoMsi.' }
Write-Host '== Building MSI' -ForegroundColor Magenta
& $dotnet build (Join-Path $proj 'installer\ClassicSynth.Installer.wixproj') -c Release -nologo
if ($LASTEXITCODE -ne 0) { throw "MSI build failed ($LASTEXITCODE)" }
Get-ChildItem (Join-Path $proj 'installer\bin') -Recurse -Filter *.msi | Select-Object FullName, Length
