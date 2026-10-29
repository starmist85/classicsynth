#include "ui/Editor.h"
#include "controller/Controller.h"
#include "params/ParamTable.h"
#include "ui/Theme.h"
#include "version.h"

#include "pluginterfaces/base/keycodes.h"
#include "pluginterfaces/gui/iplugview.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cs {

using namespace theme;

static const wchar_t* kWindowClass = L"StarmistClassicSynthEditor";
static constexpr UINT_PTR kTimerId = 1;
static constexpr float kMenuRow = 17.f;

static float canvasHeight(bool keys) { return kHeight + (keys ? kKeysHeight : 0.f); }

static ViewRect makeRect(float scale, bool keys)
{
    return ViewRect(0, 0, int32(std::lround(kWidth * scale)), int32(std::lround(canvasHeight(keys) * scale)));
}

float Editor::height() const { return canvasHeight(mKeys); }
bool  Editor::shown(const Widget& w) const { return mKeys || w.rect.top < kHeight; }

// Ends a drag in progress (a held on-screen note is released).
void Editor::releaseCapture()
{
    if (!mCapture) return;
    Widget* w = mCapture;
    mCapture = nullptr;
    w->mouseUp(0, 0, *this);
    if (mHwnd && GetCapture() == mHwnd) ReleaseCapture();
}

// ===========================================================================
// Construction / layout
// ===========================================================================
Editor::Editor(Controller* controller)
    : CPluginView(nullptr)
    , mCtl(controller)
    , mBrowser(*controller, mHwnd, [this] { invalidate(); })
{
    // Size comes from the zoom setting (exact pixels), known before the host asks.
    mScale = Controller::zoomScale(controller->zoom());
    mKeys = controller->keyboard();
    rect = makeRect(mScale, mKeys);
    mCache.fill(-9999);
    buildLayout();
}

Editor::~Editor() = default;

void Editor::buildLayout()
{
    mSections.clear();
    mWidgets.clear();

    auto cell = [](const D2D1_RECT_F& s, float cw, float rh, int col, int row, int span = 1) {
        const float x = s.left + 6.f + col * cw;
        const float y = s.top + 19.f + row * rh;
        return D2D1_RECT_F{ x, y, x + cw * span, y + rh };
    };
    auto place = [](std::unique_ptr<Widget> w, D2D1_RECT_F r, float contentH, std::vector<std::unique_ptr<Widget>>& into) {
        const float pad = std::max(0.f, ((r.bottom - r.top) - contentH) * 0.5f);
        r.top += pad;
        r.bottom = r.top + contentH;
        w->rect = r;
        into.push_back(std::move(w));
    };
    auto knob = [&](int id, const wchar_t* l, D2D1_RECT_F r, float d) { place(std::make_unique<Knob>(id, l, d), r, d + 15.f, mWidgets); };
    auto sel  = [&](int id, const wchar_t* l, D2D1_RECT_F r) { r.left += 1; r.right -= 1; place(std::make_unique<Selector>(id, l), r, 36.f, mWidgets); };
    auto tog  = [&](int id, const wchar_t* l, D2D1_RECT_F r) { place(std::make_unique<Toggle>(id, l), r, 36.f, mWidgets); };
    auto num  = [&](int id, const wchar_t* l, D2D1_RECT_F r) { place(std::make_unique<NumberBox>(id, l), r, 36.f, mWidgets); };

    // Three columns, mirroring the Synth1 V1.13 panel (original/images/synth.png):
    //   left:   Oscillators, LFO              (+ Wheel/MIDI along the bottom of left+middle)
    //   middle: Amplifier, Filter, Arpeggiator
    //   right:  Effect, Equalizer/Pan, Tempo Delay, Chorus/Flanger, Voice
    // List parameters that Synth1 shows as knobs (delay time, chorus time, arp beat)
    // are knobs here too; their value bubble shows the tempo label.

    // ---------------- Left column ----------------
    const D2D1_RECT_F osc{ 6, 74, 292, 252 };
    mSections.push_back({ L"OSCILLATORS", osc });
    {
        const float cw = 34, rh = 52, d = 26;
        sel(kOsc1Shape, L"osc 1", cell(osc, cw, rh, 0, 0));
        knob(kOsc1Detune, L"det", cell(osc, cw, rh, 1, 0), d);
        knob(kOsc1FM, L"FM", cell(osc, cw, rh, 2, 0), d);
        knob(kSubGain, L"sub", cell(osc, cw, rh, 3, 0), d);
        sel(kSubShape, L"sub wav", cell(osc, cw, rh, 4, 0));
        sel(kSubOct, L"sub oct", cell(osc, cw, rh, 5, 0));
        num(kOscKeyShift, L"key shift", cell(osc, cw, rh, 6, 0, 2));

        sel(kOsc2Shape, L"osc 2", cell(osc, cw, rh, 0, 1));
        tog(kOsc2Ring, L"ring", cell(osc, cw, rh, 1, 1));
        tog(kOsc2Sync, L"sync", cell(osc, cw, rh, 2, 1));
        tog(kOsc2Track, L"track", cell(osc, cw, rh, 3, 1));
        knob(kOsc2Pitch, L"pitch", cell(osc, cw, rh, 4, 1), d);
        knob(kOsc2Fine, L"fine", cell(osc, cw, rh, 5, 1), d);
        knob(kOscMix, L"mix", cell(osc, cw, rh, 6, 1), d);
        knob(kOscPW, L"p/w", cell(osc, cw, rh, 7, 1), d);

        tog(kModEnvOn, L"m.env", cell(osc, cw, rh, 0, 2));
        sel(kModEnvDest, L"dest.", cell(osc, cw, rh, 1, 2));
        knob(kModEnvA, L"A", cell(osc, cw, rh, 2, 2), d);
        knob(kModEnvD, L"D", cell(osc, cw, rh, 3, 2), d);
        knob(kModEnvAmt, L"amt", cell(osc, cw, rh, 4, 2), d);
        knob(kOscPhase, L"phase", cell(osc, cw, rh, 6, 2), d);
        knob(kOscFine, L"tune", cell(osc, cw, rh, 7, 2), d);
    }

    const D2D1_RECT_F lfo{ 6, 258, 292, 392 };
    mSections.push_back({ L"LFO", lfo });
    for (int i = 0; i < 2; ++i) {
        const float cw = 34, rh = 56, d = 26;
        const int c = i * 4;
        tog(i ? kLfo2On : kLfo1On, i ? L"lfo 2" : L"lfo 1", cell(lfo, cw, rh, c + 0, 0));
        sel(i ? kLfo2Type : kLfo1Type, L"wave", cell(lfo, cw, rh, c + 1, 0));
        sel(i ? kLfo2Dest : kLfo1Dest, L"dst", cell(lfo, cw, rh, c + 2, 0));
        knob(i ? kLfo2Speed : kLfo1Speed, L"spd", cell(lfo, cw, rh, c + 3, 0), d);
        knob(i ? kLfo2Depth : kLfo1Depth, L"amt", cell(lfo, cw, rh, c + 0, 1), d);
        tog(i ? kLfo2Tempo : kLfo1Tempo, L"tempo", cell(lfo, cw, rh, c + 1, 1));
        tog(i ? kLfo2Key : kLfo1Key, L"key", cell(lfo, cw, rh, c + 2, 1));
    }

    // ---------------- Middle column ----------------
    const D2D1_RECT_F amp{ 298, 74, 494, 142 };
    mSections.push_back({ L"AMPLIFIER", amp });
    {
        const float cw = 30.5f, rh = 49, d = 24;
        knob(kAmpA, L"A", cell(amp, cw, rh, 0, 0), d);
        knob(kAmpD, L"D", cell(amp, cw, rh, 1, 0), d);
        knob(kAmpS, L"S", cell(amp, cw, rh, 2, 0), d);
        knob(kAmpR, L"R", cell(amp, cw, rh, 3, 0), d);
        knob(kAmpGain, L"gain", cell(amp, cw, rh, 4, 0), d);
        knob(kAmpVel, L"vel", cell(amp, cw, rh, 5, 0), d);
    }

    const D2D1_RECT_F flt{ 298, 148, 494, 274 };
    mSections.push_back({ L"FILTER", flt });
    {
        const float cw = 30.5f, rh = 52, d = 24;
        knob(kFltA, L"A", cell(flt, cw, rh, 0, 0), d);
        knob(kFltD, L"D", cell(flt, cw, rh, 1, 0), d);
        knob(kFltS, L"S", cell(flt, cw, rh, 2, 0), d);
        knob(kFltR, L"R", cell(flt, cw, rh, 3, 0), d);
        knob(kFltAmt, L"amt", cell(flt, cw, rh, 4, 0), d);
        sel(kFltType, L"type", cell(flt, cw, rh, 5, 0));
        knob(kFltFreq, L"frq", cell(flt, cw, rh, 0, 1), d);
        knob(kFltRes, L"res", cell(flt, cw, rh, 1, 1), d);
        knob(kFltSat, L"sat", cell(flt, cw, rh, 2, 1), d);
        knob(kFltTrk, L"trk", cell(flt, cw, rh, 3, 1), d);
        tog(kFltVel, L"vel", cell(flt, cw, rh, 4, 1));
    }

    const D2D1_RECT_F arp{ 298, 280, 494, 392 };
    mSections.push_back({ L"ARPEGGIATOR", arp });
    {
        const float cw = 46, rh = 46, d = 26;
        tog(kArpOn, L"on", cell(arp, cw, rh, 0, 0));
        sel(kArpType, L"type", cell(arp, cw, rh, 1, 0));
        sel(kArpRange, L"range", cell(arp, cw, rh, 2, 0));
        knob(kArpBeat, L"beat", cell(arp, cw, rh, 3, 0), d);
        knob(kArpGate, L"gate", cell(arp, cw, rh, 3, 1), d);
    }

    // ---------------- Wheel / MIDI (bottom of left + middle) ----------------
    const D2D1_RECT_F wheel{ 6, 398, 494, 474 };
    mSections.push_back({ L"WHEEL / MIDI", wheel });
    for (int i = 0; i < 2; ++i) {
        const float x = wheel.left + 8.f + i * 242.f, y = wheel.top + 19.f;
        place(std::make_unique<Selector>(i ? kMidiSrc2 : kMidiSrc1, i ? L"src 2" : L"src 1"),
              { x, y, x + 84, y + 52 }, 36.f, mWidgets);
        place(std::make_unique<Knob>(i ? kMidiSens2 : kMidiSens1, L"amount", 24.f), { x + 86, y, x + 124, y + 52 }, 39.f, mWidgets);
        place(std::make_unique<Selector>(i ? kMidiAssign2 : kMidiAssign1, L"destination"),
              { x + 128, y, x + 234, y + 52 }, 36.f, mWidgets);
    }

    // ---------------- Right column ----------------
    const D2D1_RECT_F fx{ 500, 74, 714, 136 };
    mSections.push_back({ L"EFFECT", fx });
    {
        const float cw = 40, rh = 43, d = 22;
        tog(kFxOn, L"on", cell(fx, cw, rh, 0, 0));
        sel(kFxType, L"type", cell(fx, cw, rh, 1, 0));
        knob(kFxCtl1, L"ctl1", cell(fx, cw, rh, 2, 0), d);
        knob(kFxCtl2, L"ctl2", cell(fx, cw, rh, 3, 0), d);
        knob(kFxLevel, L"level", cell(fx, cw, rh, 4, 0), d);
    }

    const D2D1_RECT_F eq{ 500, 142, 714, 204 };
    mSections.push_back({ L"EQUALIZER / PAN", eq });
    {
        const float cw = 40, rh = 43, d = 22;
        knob(kEqFreq, L"freq", cell(eq, cw, rh, 0, 0), d);
        knob(kEqLevel, L"level", cell(eq, cw, rh, 1, 0), d);
        knob(kEqQ, L"Q", cell(eq, cw, rh, 2, 0), d);
        knob(kEqTone, L"tone", cell(eq, cw, rh, 3, 0), d);
        knob(kPan, L"L-R", cell(eq, cw, rh, 4, 0), d);
    }

    const D2D1_RECT_F dly{ 500, 210, 714, 272 };
    mSections.push_back({ L"TEMPO DELAY", dly });
    {
        const float cw = 28.8f, rh = 43, d = 22;
        tog(kDlyOn, L"on", cell(dly, cw, rh, 0, 0));
        sel(kDlyType, L"type", cell(dly, cw, rh, 1, 0));
        knob(kDlyTime, L"time", cell(dly, cw, rh, 2, 0), d);
        knob(kDlySpread, L"sprd", cell(dly, cw, rh, 3, 0), d);
        knob(kDlyFdbk, L"fdbk", cell(dly, cw, rh, 4, 0), d);
        knob(kDlyTone, L"tone", cell(dly, cw, rh, 5, 0), d);
        knob(kDlyDryWet, L"d/w", cell(dly, cw, rh, 6, 0), d);
    }

    const D2D1_RECT_F cho{ 500, 278, 714, 340 };
    mSections.push_back({ L"CHORUS / FLANGER", cho });
    {
        const float cw = 28.8f, rh = 43, d = 22;
        tog(kChoOn, L"on", cell(cho, cw, rh, 0, 0));
        sel(kChoType, L"type", cell(cho, cw, rh, 1, 0));
        knob(kChoTime, L"time", cell(cho, cw, rh, 2, 0), d);
        knob(kChoDepth, L"depth", cell(cho, cw, rh, 3, 0), d);
        knob(kChoRate, L"rate", cell(cho, cw, rh, 4, 0), d);
        knob(kChoFdbk, L"fdbk", cell(cho, cw, rh, 5, 0), d);
        knob(kChoLevel, L"level", cell(cho, cw, rh, 6, 0), d);
    }

    const D2D1_RECT_F voice{ 500, 346, 714, 474 };
    mSections.push_back({ L"VOICE", voice });
    {
        const float cw = 33.6f, rh = 54, d = 24;
        sel(kPlayMode, L"mode", cell(voice, cw, rh, 0, 0));
        num(kPolyphony, L"poly", cell(voice, cw, rh, 1, 0));
        knob(kPortaTime, L"porta", cell(voice, cw, rh, 2, 0), d);
        tog(kPortaAuto, L"auto", cell(voice, cw, rh, 3, 0));
        num(kPbRange, L"p.b.", cell(voice, cw, rh, 4, 0));
        tog(kUniOn, L"unison", cell(voice, cw, rh, 0, 1));
        num(kUniVoices, L"num", cell(voice, cw, rh, 1, 1));
        knob(kUniDetune, L"det", cell(voice, cw, rh, 2, 1), d);
        knob(kUniPhase, L"phase", cell(voice, cw, rh, 3, 1), d);
        knob(kUniSpread, L"sprd", cell(voice, cw, rh, 4, 1), d);
        num(kUniPitch, L"pitch", cell(voice, cw, rh, 5, 1));
    }

    // ---------------- Keyboard strip (below the main panel, optional) ----------------
    {
        const float top = kHeight;
        Controller* ctl = mCtl;
        auto wheel = [&](const wchar_t* l, bool spring, int id, float x) {
            auto w = std::make_unique<Wheel>(l, spring,
                [ctl, id] { return float(ctl->midiController(id)); },
                [ctl, id](float v, bool held) { ctl->setMidiController(id, v, held); });
            w->rect = { x, top + 8.f, x + 22.f, top + 94.f };
            mWidgets.push_back(std::move(w));
        };
        wheel(L"pitch", true, kMidiPitchBend, 14.f);
        wheel(L"mod", false, kMidiCCBase + 1, 44.f);
        // 61 keys, C1..C6 (C3 = MIDI 60).
        auto kb = std::make_unique<Keyboard>(36, 61, [ctl](int note, float vel) { ctl->playNote(note, vel); });
        kb->rect = { 76.f, top + 6.f, kWidth - 6.f, top + kKeysHeight - 6.f };
        mWidgets.push_back(std::move(kb));
    }

    // ---------------- Header ----------------
    mLcdRect = { 222, 8, 518, 60 };   // fills the space between the logo and the ▲▼ buttons
    auto button = [&](const wchar_t* l, D2D1_RECT_F r, std::function<void()> fn, Button::Style st = Button::Style::Normal) {
        auto b = std::make_unique<Button>(l, std::move(fn), st);
        b->rect = r;
        mWidgets.push_back(std::move(b));
        return static_cast<Button*>(mWidgets.back().get());
    };
    button(L"▲", { 522, 8, 540, 32 }, [this] { mBrowser.stepProgram(-1); invalidate(); });
    button(L"▼", { 522, 36, 540, 60 }, [this] { mBrowser.stepProgram(1); invalidate(); });
    button(L"PRESETS", { 544, 8, 604, 32 }, [this] { mBrowser.open(false); }, Button::Style::Accent);
    button(L"SAVE", { 544, 36, 604, 60 }, [this] { mBrowser.open(true); });
    button(L"dice", { 608, 8, 628, 32 }, [this] { mBrowser.randomProgram(); invalidate(); });   // random preset
    button(L"zoom", { 630, 8, 650, 32 }, [this] {
        std::vector<std::wstring> items;
        for (int i = 0; i < Controller::kNumZooms; ++i) {
            const float s = Controller::zoomScale(i);
            wchar_t buf[64];
            swprintf(buf, 64, L"%s  (%ld × %ld)", Controller::zoomLabel(i), std::lround(kWidth * s), std::lround(height() * s));
            items.push_back(buf);
        }
        Controller* ctl = mCtl;
        openMenu({ 630, 8, 650, 32 }, items, ctl->zoom(), [ctl](int i) { ctl->setZoom(i); });
    });
    button(L"keys", { 652, 8, 672, 32 }, [this] { mCtl->setKeyboard(!mCtl->keyboard()); })   // keyboard strip on/off
        ->isOn = [this] { return mKeys; };
    button(L"PANIC", { 608, 36, 672, 60 }, [this] { mCtl->panic(); }, Button::Style::Danger);
    {
        auto vol = std::make_unique<Knob>(kMasterVolume, L"vol", 24.f);
        vol->rect = { 674, 12, 704, 52 };
        mWidgets.push_back(std::move(vol));
    }
}

// ===========================================================================
// IPlugView
// ===========================================================================
tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type)
{
    return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
}

void Editor::attachedToParent()
{
    HWND parent = static_cast<HWND>(systemWindow);
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);

    WNDCLASSEXW wc{ sizeof(wc) };
    if (!GetClassInfoExW(inst, kWindowClass, &wc)) {
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Editor::wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);
    }

    mScale = Controller::zoomScale(mCtl->zoom());
    mKeys = mCtl->keyboard();
    rect = makeRect(mScale, mKeys);

    mHwnd = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, rect.getWidth(), rect.getHeight(), parent, nullptr, inst, nullptr);
    SetWindowLongPtrW(mHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    mGfx.create(mHwnd, mScale);
    SetTimer(mHwnd, kTimerId, 33, nullptr);
    mCtl->editorAttached(this);
    if (!mCtl->splashShown) {
        mCtl->splashShown = true;
        mSplashStart = GetTickCount64();
    }
}

void Editor::removedFromParent()
{
    releaseCapture();
    mCtl->editorDetached(this);
    if (mHwnd) {
        KillTimer(mHwnd, kTimerId);
        mGfx.destroy();
        SetWindowLongPtrW(mHwnd, GWLP_USERDATA, 0);
        DestroyWindow(mHwnd);
        mHwnd = nullptr;
    }
    mCapture = mHover = nullptr;
}

tresult PLUGIN_API Editor::onSize(ViewRect* newSize)
{
    const tresult r = CPluginView::onSize(newSize);
    if (mHwnd && newSize) {
        SetWindowPos(mHwnd, nullptr, 0, 0, newSize->getWidth(), newSize->getHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
        mGfx.resize(UINT(newSize->getWidth()), UINT(newSize->getHeight()));
    }
    return r;
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor)
{
    // The editor uses fixed pixel sizes chosen with the zoom control
    // (720 / 1024 / 1920 px wide), so the host's DPI factor is not applied.
    return kResultTrue;
}

void Editor::applySize()
{
    const float scale = Controller::zoomScale(mCtl->zoom());
    const bool keys = mCtl->keyboard();
    const bool rescale = std::fabs(scale - mScale) >= 0.0001f;
    if (!rescale && keys == mKeys) return;
    mScale = scale;
    mKeys = keys;
    rect = makeRect(mScale, mKeys);
    mMenu.open = false;
    releaseCapture();
    if (mHover && !shown(*mHover)) { mHover->hover = false; mHover = nullptr; }
    if (!mHwnd) return;
    SetWindowPos(mHwnd, nullptr, 0, 0, rect.getWidth(), rect.getHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
    if (rescale) {
        mGfx.destroy();
        mGfx.create(mHwnd, mScale);
    } else {
        mGfx.resize(UINT(rect.getWidth()), UINT(rect.getHeight()));
    }
    if (plugFrame) plugFrame->resizeView(this, &rect);   // host resizes its window, then calls onSize
    invalidate();
}

tresult PLUGIN_API Editor::onKeyDown(char16 key, int16 keyCode, int16)
{
    if (!mBrowser.visible()) return kResultFalse;
    int vk = 0;
    switch (keyCode) {
    case KEY_BACK:   vk = VK_BACK; break;
    case KEY_RETURN:
    case KEY_ENTER:  vk = VK_RETURN; break;
    case KEY_ESCAPE: vk = VK_ESCAPE; break;
    case KEY_UP:     vk = VK_UP; break;
    case KEY_DOWN:   vk = VK_DOWN; break;
    default: break;
    }
    if (vk) return mBrowser.keyDown(vk) ? kResultTrue : kResultFalse;
    if (key >= 32) return mBrowser.keyChar(wchar_t(key)) ? kResultTrue : kResultFalse;
    return kResultFalse;
}

// ===========================================================================
// UiHost
// ===========================================================================
int  Editor::value(int id)              { return mCtl->value(id); }
void Editor::beginEdit(int id)          { mCtl->beginGesture(id); }
void Editor::setValue(int id, int v)    { mCtl->setValueInGesture(id, v); invalidate(); }
void Editor::endEdit(int id)            { mCtl->endGesture(id); }

void Editor::showValue(int id)
{
    mBubbleParam = id;
    mBubbleUntil = GetTickCount64() + 1200;
    invalidate();
}

void Editor::invalidate()
{
    if (mHwnd) InvalidateRect(mHwnd, nullptr, FALSE);
}

void Editor::openMenu(const D2D1_RECT_F& anchor, const std::vector<std::wstring>& items, int selected,
                      std::function<void(int)> onPick)
{
    Menu& m = mMenu;
    m.items = items;
    m.selected = selected;
    m.hover = selected;
    m.onPick = std::move(onPick);
    m.visibleRows = std::min<int>(int(items.size()), 18);
    float w = anchor.right - anchor.left;
    for (const auto& s : items) w = std::max(w, mGfx.textWidth(s, Font::Small) + 26.f);
    const float h = m.visibleRows * kMenuRow + 6.f;
    float x = std::clamp(anchor.left, 4.f, kWidth - w - 4.f);
    float y = anchor.bottom + 2.f;
    if (y + h > height() - 4.f) y = std::max(4.f, anchor.top - h - 2.f);
    m.rect = { x, y, x + w, y + h };
    m.first = std::clamp(selected - m.visibleRows / 2, 0, std::max(0, int(items.size()) - m.visibleRows));
    m.open = true;
    invalidate();
}

// ===========================================================================
// Rendering
// ===========================================================================
void Editor::render()
{
    if (!mGfx.begin()) return;
    mGfx.clear(kPage);
    mGfx.drawBackground({ 0, 0, kWidth, kHeight }, kBgOpacity);
    drawHeader();
    if (mKeys) drawKeyStrip();
    for (const auto& s : mSections) s.draw(mGfx);
    // LFO 1 | LFO 2 divider
    mGfx.line(148.f, 280.f, 148.f, 386.f, kPanelBorder, 1.f);
    // Wheel/MIDI slot divider and arrows
    mGfx.line(250.f, 420.f, 250.f, 468.f, kPanelBorder, 1.f);
    for (auto& w : mWidgets) if (shown(*w)) w->draw(mGfx, *this);
    drawMeters();
    drawBubble();
    mBrowser.draw(mGfx);
    if (mBrowser.visible() && mKeys) mGfx.fillRect({ 0, kHeight, kWidth, height() }, kScrim);   // browser covers 720x480
    drawMenu();
    drawSplash();
    mGfx.end();
}

float Editor::splashOpacity() const
{
    if (!mSplashStart) return 0.f;
    const float t = float(GetTickCount64() - mSplashStart) / 1000.f;
    if (t < 0.9f) return 1.f;                 // hold
    if (t < 1.5f) return 1.f - (t - 0.9f) / 0.6f;   // fade out
    return 0.f;
}

void Editor::drawSplash()
{
    const float a = splashOpacity();
    if (a <= 0.f) return;
    mGfx.fillRect({ 0, 0, kWidth, height() }, rgb(0x050507, 0.88f * a));
    const float s = 250.f, x = (kWidth - s) * 0.5f, y = std::round((height() - s - 62.f) * 0.5f);
    mGfx.fillRound({ x - 2, y - 2, x + s + 2, y + s + 2 }, 34.f, rgb(0x6A22B8, 0.45f * a));
    mGfx.drawSplash({ x, y, x + s, y + s }, a);
    mGfx.text(L"CLASSICSYNTH", { 0, y + s + 18, kWidth, y + s + 44 }, Font::Large, rgb(0xE4E2EC, a));
    mGfx.text(L"Starmist Audio  ·  v" CS_VERSION_STR_W, { 0, y + s + 44, kWidth, y + s + 62 }, Font::Small, rgb(0x9A97A8, a));
}

void Editor::drawHeader()
{
    mGfx.fillRect({ 0, 0, kWidth, 68 }, kHeader);
    mGfx.line(0, 67.5f, kWidth, 67.5f, kPanelBorder, 1.f);

    // Logo, 200 px wide max, top-left.
    const float logoW = 200.f, logoH = logoW / mGfx.logoAspect();
    mGfx.drawLogo({ 12.f, (68.f - logoH) * 0.5f, 12.f + logoW, (68.f - logoH) * 0.5f + logoH });

    // Program LCD
    const D2D1_RECT_F& L = mLcdRect;
    mGfx.fillRound(L, 5.f, kLcd);
    mGfx.strokeRound(L, 5.f, kLcdBorder, 1.f);
    mGfx.text(mBrowser.currentLocation(), { L.left + 8, L.top + 2, L.right - 66, L.top + 16 }, Font::Tiny, kLcdTextDim, Align::Left);
    wchar_t cpu[48];
    swprintf(cpu, 48, L"CPU %.1f%%", mCpu * 100.f);
    mGfx.text(cpu, { L.right - 64, L.top + 2, L.right - 8, L.top + 16 }, Font::Tiny, mCpu > 0.7f ? rgb(0xE0566A) : kLcdTextDim, Align::Right);
    const std::wstring name = widen(mCtl->programName());
    mGfx.text(name.empty() ? L"untitled" : name, { L.left + 8, L.top + 14, L.right - 8, L.top + 36 }, Font::Large, kLcdText, Align::Left);

    // Bottom line: hovered/edited parameter readout, voices + CPU bar.
    std::wstring info;
    const int infoParam = (GetTickCount64() < mBubbleUntil) ? mBubbleParam : (mHover ? mHover->param : -1);
    if (const ParamDef* d = findParam(infoParam))
        info = widen(d->name) + L":  " + valueText(infoParam, mCtl->value(infoParam));
    mGfx.text(info, { L.left + 8, L.top + 36, L.right - 70, L.bottom - 2 }, Font::Tiny, kLcdTextDim, Align::Left);
    wchar_t vc[24];
    swprintf(vc, 24, L"%d voices", int(std::lround(mVoices * 64.f)));
    mGfx.text(vc, { L.right - 70, L.top + 36, L.right - 8, L.bottom - 8 }, Font::Tiny, kLcdTextDim, Align::Right);
    const D2D1_RECT_F bar{ L.right - 62, L.bottom - 7, L.right - 8, L.bottom - 4 };
    mGfx.fillRound(bar, 1.5f, rgb(0x1C1A28));
    const float cw = (bar.right - bar.left) * std::clamp(mCpu, 0.f, 1.f);
    if (cw > 0.5f) mGfx.fillRound({ bar.left, bar.top, bar.left + cw, bar.bottom }, 1.5f, mCpu > 0.7f ? rgb(0xE0566A) : kAccentBright);
}

void Editor::drawKeyStrip()
{
    const D2D1_RECT_F strip{ 0, kHeight, kWidth, height() };
    mGfx.fillRect(strip, kHeader);
    mGfx.line(0, kHeight + 0.5f, kWidth, kHeight + 0.5f, kPanelBorder, 1.f);
    mGfx.line(70.5f, kHeight + 10.f, 70.5f, height() - 10.f, kPanelBorder, 1.f);   // wheels | keys
}

void Editor::drawMeters()
{
    // Stereo output meter right of the volume knob: -60 dB .. +6 dB.
    const float top = 9.f, bottom = 59.f;
    for (int c = 0; c < 2; ++c) {
        const float x0 = 706.f + c * 6.f, x1 = x0 + 4.f;
        mGfx.fillRound({ x0, top, x1, bottom }, 1.5f, rgb(0x1C1A28));
        const float h = (bottom - top) * std::clamp(mLevel[c], 0.f, 1.f);
        const float zeroDb = bottom - (bottom - top) * (60.f / 66.f);
        if (h > 0.5f) {
            const float yTop = bottom - h;
            mGfx.fillRect({ x0, std::max(yTop, zeroDb), x1, bottom }, kAccentBright);
            if (yTop < zeroDb) mGfx.fillRect({ x0, yTop, x1, zeroDb }, rgb(0xE0566A));   // over 0 dB
        }
        if (mHold[c] > 0.01f) {
            const float hy = bottom - (bottom - top) * mHold[c];
            mGfx.line(x0, hy, x1, hy, mHold[c] > 60.f / 66.f ? rgb(0xFF7085) : kText, 1.f);
        }
    }
}

void Editor::drawBubble()
{
    if (mBubbleParam < 0 || GetTickCount64() >= mBubbleUntil || mBrowser.visible()) return;
    const Widget* w = nullptr;
    for (auto& x : mWidgets) if (x->param == mBubbleParam) w = x.get();
    if (!w || dynamic_cast<const Selector*>(w)) return;
    const std::wstring t = valueText(mBubbleParam, mCtl->value(mBubbleParam));
    const float tw = mGfx.textWidth(t, Font::Bold) + 14.f;
    const float cx = (w->rect.left + w->rect.right) * 0.5f;
    float x = std::clamp(cx - tw * 0.5f, 4.f, kWidth - tw - 4.f);
    float y = w->rect.top - 20.f;
    if (y < 70.f && w->rect.top > 68.f) y = w->rect.bottom + 2.f;
    if (y < 2.f) y = w->rect.bottom + 2.f;
    const D2D1_RECT_F r{ x, y, x + tw, y + 18.f };
    mGfx.fillRound({ r.left, r.top + 1.5f, r.right, r.bottom + 1.5f }, 4.f, rgb(0x000000, 0.5f));
    mGfx.fillRound(r, 4.f, kOverlay);
    mGfx.strokeRound(r, 4.f, kAccent, 1.f);
    mGfx.text(t, r, Font::Bold, kText);
}

void Editor::drawMenu()
{
    if (!mMenu.open) return;
    const Menu& m = mMenu;
    mGfx.fillRound({ m.rect.left, m.rect.top + 2.f, m.rect.right, m.rect.bottom + 3.f }, 5.f, rgb(0x000000, 0.55f));
    mGfx.fillRound(m.rect, 5.f, kOverlay);
    mGfx.strokeRound(m.rect, 5.f, kOverlayBorder, 1.f);
    for (int i = 0; i < m.visibleRows; ++i) {
        const int idx = m.first + i;
        if (idx >= int(m.items.size())) break;
        const D2D1_RECT_F r{ m.rect.left + 3.f, m.rect.top + 3.f + i * kMenuRow, m.rect.right - 3.f, m.rect.top + 3.f + (i + 1) * kMenuRow };
        if (idx == m.selected) mGfx.fillRound(r, 3.f, kRowSelected);
        else if (idx == m.hover) mGfx.fillRound(r, 3.f, kRowHover);
        mGfx.text(m.items[size_t(idx)], { r.left + 8.f, r.top, r.right - 4.f, r.bottom }, Font::Small,
                  idx == m.selected ? kText : rgb(0xCFCCDA), Align::Left);
    }
    if (int(m.items.size()) > m.visibleRows) {
        // Scroll indicator
        const float trackH = m.rect.bottom - m.rect.top - 8.f;
        const float th = trackH * m.visibleRows / float(m.items.size());
        const float ty = m.rect.top + 4.f + trackH * m.first / float(m.items.size());
        mGfx.fillRound({ m.rect.right - 4.f, ty, m.rect.right - 2.f, ty + th }, 1.f, kAccent);
    }
}

// ===========================================================================
// Timer: poll parameter / meter changes
// ===========================================================================
void Editor::tick()
{
    bool dirty = false;
    for (int id = 0; id <= kMasterVolume; ++id) {
        if (!findParam(id)) continue;
        const int v = mCtl->value(id);
        if (v != mCache[size_t(id)]) { mCache[size_t(id)] = v; dirty = true; }
    }

    const float cpu = float(mCtl->getParamNormalized(Vst::ParamID(kMeterCpu)));
    const float voices = float(mCtl->getParamNormalized(Vst::ParamID(kMeterVoices)));
    if (std::fabs(cpu - mCpu) > 0.001f || voices != mVoices) { mCpu = cpu; mVoices = voices; dirty = true; }

    // Wheels also follow incoming MIDI pitch bend / CC1.
    if (mKeys) {
        const float wheels[2] = { float(mCtl->midiController(kMidiPitchBend)), float(mCtl->midiController(kMidiCCBase + 1)) };
        for (int i = 0; i < 2; ++i)
            if (wheels[i] != mWheels[i]) { mWheels[i] = wheels[i]; dirty = true; }
    }

    const unsigned long long now = GetTickCount64();
    for (int c = 0; c < 2; ++c) {
        const float in = float(mCtl->getParamNormalized(Vst::ParamID(c ? kMeterOutR : kMeterOutL)));
        // Fast attack, ~20 dB/s release (66 dB span -> 0.01 per 33 ms tick).
        const float lv = std::max(in, mLevel[c] - 0.01f);
        if (lv != mLevel[c]) { mLevel[c] = std::max(lv, 0.f); dirty = true; }
        if (in >= mHold[c]) { mHold[c] = in; mHoldTime[c] = now; }
        else if (now - mHoldTime[c] > 1500 && mHold[c] > 0.f) { mHold[c] = std::max(0.f, mHold[c] - 0.02f); dirty = true; }
    }

    const std::wstring loc = mBrowser.currentLocation();
    if (loc != mLastLocation) { mLastLocation = loc; dirty = true; }
    if (mBubbleParam >= 0 && now >= mBubbleUntil && now < mBubbleUntil + 100) dirty = true;
    if (mSplashStart) {
        dirty = true;
        if (splashOpacity() <= 0.f) mSplashStart = 0;
    }

    mBrowser.tick();
    if (dirty) invalidate();
}

// ===========================================================================
// Input
// ===========================================================================
unsigned Editor::currentMods() const
{
    unsigned m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= kShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= kCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) m |= kAlt;
    return m;
}

Widget* Editor::widgetAt(float x, float y)
{
    auto& list = mWidgets;
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (shown(**it) && (*it)->contains(x, y)) return it->get();
    return nullptr;
}

void Editor::onMouseDown(float x, float y, unsigned mods, bool dbl)
{
    if (splashOpacity() > 0.f) { mSplashStart = 0; invalidate(); return; }   // click skips the splash
    if (mMenu.open) {
        Menu& m = mMenu;
        if (x >= m.rect.left && x < m.rect.right && y >= m.rect.top && y < m.rect.bottom) {
            const int idx = m.first + int((y - m.rect.top - 3.f) / kMenuRow);
            if (idx >= 0 && idx < int(m.items.size()) && m.onPick) m.onPick(idx);
        }
        m.open = false;
        invalidate();
        return;
    }
    if (mBrowser.visible()) { mBrowser.mouseDown(x, y, dbl); return; }

    Widget* w = widgetAt(x, y);
    if (!w) return;
    if (dbl && w->wantsDoubleClick()) { w->doubleClick(*this); invalidate(); return; }
    w->mouseDown(x, y, mods, *this);
    if (w->wantsCapture()) { mCapture = w; SetCapture(mHwnd); }
    invalidate();
}

void Editor::onMouseMove(float x, float y, unsigned mods)
{
    if (mCapture) { mCapture->mouseDrag(x, y, mods, *this); return; }
    if (mMenu.open) {
        const Menu& m = mMenu;
        int h = -1;
        if (x >= m.rect.left && x < m.rect.right && y >= m.rect.top && y < m.rect.bottom)
            h = m.first + int((y - m.rect.top - 3.f) / kMenuRow);
        if (h != mMenu.hover) { mMenu.hover = h; invalidate(); }
        return;
    }
    if (mBrowser.visible()) { mBrowser.mouseMove(x, y); return; }

    Widget* w = widgetAt(x, y);
    if (w != mHover) {
        if (mHover) mHover->hover = false;
        mHover = w;
        if (mHover) mHover->hover = true;
        invalidate();
    }
}

void Editor::onMouseUp(float x, float y)
{
    if (mCapture) {
        mCapture->mouseUp(x, y, *this);
        mCapture = nullptr;
        ReleaseCapture();
        invalidate();
    }
}

void Editor::onWheel(float x, float y, float delta, unsigned mods)
{
    if (mMenu.open) {
        Menu& m = mMenu;
        m.first = std::clamp(m.first + (delta > 0 ? -2 : 2), 0, std::max(0, int(m.items.size()) - m.visibleRows));
        invalidate();
        return;
    }
    if (mBrowser.visible()) { mBrowser.wheel(x, y, delta); return; }
    if (Widget* w = widgetAt(x, y)) { w->wheel(delta, mods, *this); invalidate(); }
}

LRESULT CALLBACK Editor::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Editor::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    const float inv = 1.f / mScale;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(mHwnd, &ps);
        render();
        EndPaint(mHwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        mGfx.resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_TIMER:
        if (wp == kTimerId) tick();
        return 0;
    case WM_LBUTTONDOWN:
        onMouseDown(GET_X_LPARAM(lp) * inv, GET_Y_LPARAM(lp) * inv, currentMods(), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        onMouseDown(GET_X_LPARAM(lp) * inv, GET_Y_LPARAM(lp) * inv, currentMods(), true);
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, mHwnd, 0 };
        TrackMouseEvent(&tme);
        onMouseMove(GET_X_LPARAM(lp) * inv, GET_Y_LPARAM(lp) * inv, currentMods());
        return 0;
    }
    case WM_MOUSELEAVE:
        if (mHover && !mCapture) { mHover->hover = false; mHover = nullptr; invalidate(); }
        return 0;
    case WM_LBUTTONUP:
        onMouseUp(GET_X_LPARAM(lp) * inv, GET_Y_LPARAM(lp) * inv);
        return 0;
    case WM_CAPTURECHANGED:
        if (mCapture && reinterpret_cast<HWND>(lp) != mHwnd) { mCapture->mouseUp(0, 0, *this); mCapture = nullptr; }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(mHwnd, &pt);
        onWheel(pt.x * inv, pt.y * inv, float(GET_WHEEL_DELTA_WPARAM(wp)), currentMods());
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (mBrowser.visible() && mBrowser.keyDown(int(wp))) return 0;
        break;
    case WM_CHAR:
        if (mBrowser.visible() && mBrowser.keyChar(wchar_t(wp))) return 0;
        break;
    default:
        break;
    }
    return DefWindowProcW(mHwnd, msg, wp, lp);
}

} // namespace cs
