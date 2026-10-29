#include "ui/Graphics.h"
#include "resource.h"

#include <cmath>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cs {

Gfx::~Gfx() { destroy(); }

bool Gfx::create(HWND hwnd, float scale)
{
    mHwnd = hwnd;
    mScale = scale;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &mFactory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&mDw)))) return false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // usually already initialized by the host
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mWic));

    struct Spec { float size; DWRITE_FONT_WEIGHT weight; };
    const Spec specs[int(Font::Count)] = {
        { 8.5f, DWRITE_FONT_WEIGHT_NORMAL },     // Tiny
        { 9.5f, DWRITE_FONT_WEIGHT_NORMAL },     // Small
        { 10.5f, DWRITE_FONT_WEIGHT_NORMAL },    // Normal
        { 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD }, // Bold
        { 9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD },  // Title
        { 15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD },  // Large
    };
    for (int i = 0; i < int(Font::Count); ++i) {
        mDw->CreateTextFormat(L"Segoe UI", nullptr, specs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, specs[i].size, L"en-us", &mFonts[i]);
        if (mFonts[i]) {
            mFonts[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            IDWriteInlineObject* ellipsis = nullptr;
            mDw->CreateEllipsisTrimmingSign(mFonts[i], &ellipsis);
            mFonts[i]->SetTrimming(&trim, ellipsis);
            safeRelease(ellipsis);
        }
    }

    // Decode embedded images once (WIC sources survive device loss).
    mLogoSource = loadImage(IDR_LOGO_PNG, &mLogoAspect);
    mBgSource = loadImage(IDR_BG_PNG, nullptr);
    mSplashSource = loadImage(IDR_SPLASH_PNG, nullptr);
    return createDeviceResources();
}

IWICFormatConverter* Gfx::loadImage(int resourceId, float* aspect)
{
    if (!mWic) return nullptr;
    HMODULE mod = reinterpret_cast<HMODULE>(&__ImageBase);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res) return nullptr;
    HGLOBAL h = LoadResource(mod, res);
    const DWORD size = SizeofResource(mod, res);
    const void* data = LockResource(h);
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (data && SUCCEEDED(mWic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size)) &&
        SUCCEEDED(mWic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        SUCCEEDED(mWic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
        UINT w = 0, hgt = 0;
        frame->GetSize(&w, &hgt);
        if (aspect && w && hgt) *aspect = float(w) / float(hgt);
    } else {
        safeRelease(conv);
    }
    safeRelease(frame);
    safeRelease(dec);
    safeRelease(stream);
    return conv;
}

void Gfx::destroy()
{
    releaseDeviceResources();
    for (auto& f : mFonts) safeRelease(f);
    safeRelease(mLogoSource);
    safeRelease(mBgSource);
    safeRelease(mSplashSource);
    safeRelease(mWic);
    safeRelease(mDw);
    safeRelease(mFactory);
}

bool Gfx::createDeviceResources()
{
    if (mRt) return true;
    RECT rc;
    GetClientRect(mHwnd, &rc);
    const float dpi = 96.f * mScale;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    if (FAILED(mFactory->CreateHwndRenderTarget(props,
            D2D1::HwndRenderTargetProperties(mHwnd, D2D1::SizeU(UINT(rc.right - rc.left), UINT(rc.bottom - rc.top))), &mRt)))
        return false;
    mRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    mRt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &mBrush);
    if (mLogoSource) mRt->CreateBitmapFromWicBitmap(mLogoSource, nullptr, &mLogo);
    if (mBgSource) mRt->CreateBitmapFromWicBitmap(mBgSource, nullptr, &mBg);
    if (mSplashSource) mRt->CreateBitmapFromWicBitmap(mSplashSource, nullptr, &mSplash);
    return true;
}

void Gfx::releaseDeviceResources()
{
    safeRelease(mLogo);
    safeRelease(mBg);
    safeRelease(mSplash);
    safeRelease(mBrush);
    safeRelease(mRt);
}

void Gfx::resize(UINT w, UINT h)
{
    if (mRt) mRt->Resize(D2D1::SizeU(w, h));
}

bool Gfx::begin()
{
    if (!createDeviceResources()) return false;
    mRt->BeginDraw();
    mRt->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

bool Gfx::end()
{
    if (!mRt) return false;
    const HRESULT hr = mRt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) { releaseDeviceResources(); return false; }
    return SUCCEEDED(hr);
}

ID2D1SolidColorBrush* Gfx::brush(const D2D1_COLOR_F& c)
{
    mBrush->SetColor(c);
    return mBrush;
}

void Gfx::clear(const D2D1_COLOR_F& c) { mRt->Clear(c); }
void Gfx::fillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) { mRt->FillRectangle(r, brush(c)); }
void Gfx::fillRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c) { mRt->FillRoundedRectangle({ r, rad, rad }, brush(c)); }
void Gfx::strokeRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c, float w)
{
    const D2D1_RECT_F in{ r.left + w * 0.5f, r.top + w * 0.5f, r.right - w * 0.5f, r.bottom - w * 0.5f };
    mRt->DrawRoundedRectangle({ in, rad, rad }, brush(c), w);
}
void Gfx::fillCircle(float cx, float cy, float r, const D2D1_COLOR_F& c) { mRt->FillEllipse({ { cx, cy }, r, r }, brush(c)); }
void Gfx::strokeCircle(float cx, float cy, float r, const D2D1_COLOR_F& c, float w) { mRt->DrawEllipse({ { cx, cy }, r, r }, brush(c), w); }
void Gfx::line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w) { mRt->DrawLine({ x1, y1 }, { x2, y2 }, brush(c), w); }

void Gfx::arc(float cx, float cy, float r, float a0, float a1, const D2D1_COLOR_F& c, float w)
{
    if (std::fabs(a1 - a0) < 0.001f) return;
    if (a1 < a0) std::swap(a0, a1);
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(mFactory->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink))) {
        sink->BeginFigure({ cx + r * std::cos(a0), cy + r * std::sin(a0) }, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment({ cx + r * std::cos(a1), cy + r * std::sin(a1) }, { r, r }, 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      (a1 - a0) > 3.14159265f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        safeRelease(sink);
        ID2D1StrokeStyle* style = nullptr;
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
        mFactory->CreateStrokeStyle(sp, nullptr, 0, &style);
        mRt->DrawGeometry(geo, brush(c), w, style);
        safeRelease(style);
    }
    safeRelease(geo);
}

void Gfx::triangle(float x1, float y1, float x2, float y2, float x3, float y3, const D2D1_COLOR_F& c)
{
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(mFactory->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink))) {
        sink->BeginFigure({ x1, y1 }, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({ x2, y2 });
        sink->AddLine({ x3, y3 });
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        safeRelease(sink);
        mRt->FillGeometry(geo, brush(c));
    }
    safeRelease(geo);
}

void Gfx::verticalGradient(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom)
{
    D2D1_GRADIENT_STOP stops[2] = { { 0.f, top }, { 1.f, bottom } };
    ID2D1GradientStopCollection* coll = nullptr;
    if (FAILED(mRt->CreateGradientStopCollection(stops, 2, &coll))) return;
    ID2D1LinearGradientBrush* lg = nullptr;
    if (SUCCEEDED(mRt->CreateLinearGradientBrush({ { r.left, r.top }, { r.left, r.bottom } }, coll, &lg))) {
        mRt->FillRoundedRectangle({ r, radius, radius }, lg);
        safeRelease(lg);
    }
    safeRelease(coll);
}

void Gfx::text(const std::wstring& s, const D2D1_RECT_F& r, Font f, const D2D1_COLOR_F& c, Align a, bool vcenter)
{
    IDWriteTextFormat* fmt = mFonts[int(f)];
    if (!fmt || s.empty()) return;
    fmt->SetTextAlignment(a == Align::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : a == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    mRt->DrawText(s.c_str(), UINT32(s.size()), fmt, r, brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Gfx::textWidth(const std::wstring& s, Font f)
{
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(mDw->CreateTextLayout(s.c_str(), UINT32(s.size()), mFonts[int(f)], 2000.f, 100.f, &layout))) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    safeRelease(layout);
    return m.widthIncludingTrailingWhitespace;
}

void Gfx::pushClip(const D2D1_RECT_F& r) { mRt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }
void Gfx::popClip() { mRt->PopAxisAlignedClip(); }

void Gfx::drawLogo(const D2D1_RECT_F& r)
{
    if (mLogo) mRt->DrawBitmap(mLogo, r, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Gfx::drawBackground(const D2D1_RECT_F& r, float opacity)
{
    if (mBg) mRt->DrawBitmap(mBg, r, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Gfx::drawSplash(const D2D1_RECT_F& r, float opacity)
{
    if (mSplash) mRt->DrawBitmap(mSplash, r, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

} // namespace cs
