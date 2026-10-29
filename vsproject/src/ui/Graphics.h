#pragma once
// Thin Direct2D / DirectWrite wrapper used by the editor. All drawing is in
// DIPs on a 720 x 480 canvas (+100 with the keyboard strip); the render target DPI handles scaling.

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <string>

namespace cs {

template <class T> void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

enum class Align { Left, Center, Right };
enum class Font { Tiny = 0, Small, Normal, Bold, Title, Large, Count };

class Gfx {
public:
    Gfx() = default;
    ~Gfx();

    bool create(HWND hwnd, float scale);
    void destroy();
    void resize(UINT w, UINT h);
    bool begin();
    bool end();          // returns false if the device was lost (recreated on next begin)

    ID2D1RenderTarget* rt() const { return mRt; }

    // Primitives
    void clear(const D2D1_COLOR_F& c);
    void fillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void fillRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void strokeRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float w = 1.f);
    void fillCircle(float cx, float cy, float r, const D2D1_COLOR_F& c);
    void strokeCircle(float cx, float cy, float r, const D2D1_COLOR_F& c, float w = 1.f);
    void line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w = 1.f);
    void arc(float cx, float cy, float r, float a0, float a1, const D2D1_COLOR_F& c, float w);   // radians, clockwise
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, const D2D1_COLOR_F& c);
    void verticalGradient(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom);

    void text(const std::wstring& s, const D2D1_RECT_F& r, Font f, const D2D1_COLOR_F& c, Align a = Align::Center,
              bool vcenter = true);
    float textWidth(const std::wstring& s, Font f);

    void pushClip(const D2D1_RECT_F& r);
    void popClip();

    // Bitmaps from the module's RCDATA resources.
    void drawLogo(const D2D1_RECT_F& r);
    void drawBackground(const D2D1_RECT_F& r, float opacity);
    void drawSplash(const D2D1_RECT_F& r, float opacity);
    float logoAspect() const { return mLogoAspect; }

private:
    bool createDeviceResources();
    void releaseDeviceResources();
    ID2D1SolidColorBrush* brush(const D2D1_COLOR_F& c);

    HWND mHwnd = nullptr;
    float mScale = 1.f;
    ID2D1Factory* mFactory = nullptr;
    IDWriteFactory* mDw = nullptr;
    IWICImagingFactory* mWic = nullptr;
    ID2D1HwndRenderTarget* mRt = nullptr;
    ID2D1SolidColorBrush* mBrush = nullptr;
    IWICFormatConverter* loadImage(int resourceId, float* aspect);
    ID2D1Bitmap* mLogo = nullptr;
    ID2D1Bitmap* mBg = nullptr;
    ID2D1Bitmap* mSplash = nullptr;
    IWICFormatConverter* mLogoSource = nullptr;
    IWICFormatConverter* mBgSource = nullptr;
    IWICFormatConverter* mSplashSource = nullptr;
    float mLogoAspect = 1000.f / 204.f;
    IDWriteTextFormat* mFonts[int(Font::Count)] = {};
};

} // namespace cs
