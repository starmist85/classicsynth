#pragma once
// ClassicSynth editor: 720 x 480 Direct2D view hosted in a Win32 child window,
// plus an optional 100 px keyboard strip (pitch/mod wheels, 61 keys) below it.
// Layout follows the original Synth1 panel groups (see original/images), in a
// dark theme with a darker-purple accent.

#include "ui/Graphics.h"
#include "ui/PresetBrowser.h"
#include "ui/Widgets.h"

#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace cs {

class Controller;

class Editor : public Steinberg::CPluginView, public Steinberg::IPlugViewContentScaleSupport, public UiHost {
public:
    explicit Editor(Controller* controller);
    ~Editor() override;

    // IPlugView
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) override;
    void attachedToParent() override;
    void removedFromParent() override;

    // IPlugViewContentScaleSupport
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

    // UiHost
    int  value(int id) override;
    void beginEdit(int id) override;
    void setValue(int id, int v) override;
    void endEdit(int id) override;
    void openMenu(const D2D1_RECT_F& anchor, const std::vector<std::wstring>& items, int selected,
                  std::function<void(int)> onPick) override;
    void showValue(int id) override;
    void invalidate() override;

    void onProgramChanged() { invalidate(); }
    void applySize();                                   // resize to the controller's zoom / keyboard setting

    OBJ_METHODS(Editor, CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(CPluginView)
    REFCOUNT_METHODS(CPluginView)

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void buildLayout();
    void render();
    void drawHeader();
    void drawKeyStrip();
    void drawMeters();
    float height() const;                               // canvas height in DIPs
    bool  shown(const Widget& w) const;                 // false for keyboard-strip widgets while it is hidden
    void  releaseCapture();
    void drawBubble();
    void drawMenu();
    void drawSplash();
    float splashOpacity() const;
    void tick();

    Widget* widgetAt(float x, float y);
    void onMouseDown(float x, float y, unsigned mods, bool dbl);
    void onMouseMove(float x, float y, unsigned mods);
    void onMouseUp(float x, float y);
    void onWheel(float x, float y, float delta, unsigned mods);
    unsigned currentMods() const;

    Controller* mCtl;
    HWND mHwnd = nullptr;
    Gfx mGfx;
    float mScale = 1.f;
    bool mKeys = true;                                  // keyboard strip shown

    std::vector<Section> mSections;
    std::vector<std::unique_ptr<Widget>> mWidgets;       // main panel + header
    Widget* mCapture = nullptr;
    Widget* mHover = nullptr;
    D2D1_RECT_F mLcdRect{};

    PresetBrowser mBrowser;

    struct Menu {
        bool open = false;
        D2D1_RECT_F rect{};
        std::vector<std::wstring> items;
        int selected = -1, hover = -1, first = 0, visibleRows = 0;
        std::function<void(int)> onPick;
    } mMenu;

    int mBubbleParam = -1;
    unsigned long long mBubbleUntil = 0;
    std::array<int, 101> mCache{};
    // Meter display state
    float mCpu = 0.f, mVoices = 0.f;
    float mWheels[2] = { -1.f, -1.f };                  // pitch bend, mod wheel (for repaint)
    float mLevel[2] = {}, mHold[2] = {};
    unsigned long long mHoldTime[2] = {};
    std::wstring mLastLocation;
    unsigned long long mSplashStart = 0;   // 0 = no splash
};

} // namespace cs
