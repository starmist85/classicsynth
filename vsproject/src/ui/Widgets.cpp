#include "ui/Widgets.h"
#include "dsp/DspCommon.h"
#include "params/ParamDisplay.h"
#include "params/ParamTable.h"
#include "presets/PresetLibrary.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace cs {

using namespace theme;

static constexpr float kArcStart = 2.35619449f;            // 135 deg (lower left)
static constexpr float kArcSweep = 4.71238898f;            // 270 deg
static constexpr float kDragPixels = 180.f;                 // full range drag distance

std::wstring valueText(int id, int value) { return widen(formatParamValue(id, value)); }
std::wstring optionText(int id, int value)
{
    const ParamDef* d = findParam(id);
    if (d && d->kind == Kind::List && d->labels) return widen(d->labels[value - d->minValue]);
    return valueText(id, value);
}

static D2D1_RECT_F labelRect(const D2D1_RECT_F& r, float top)
{
    return { r.left - 4.f, top, r.right + 4.f, top + 12.f };
}

// ---------------------------------------------------------------------------
void Section::draw(Gfx& g) const
{
    g.fillRound(rect, kRadius, kPanel);
    g.strokeRound(rect, kRadius, kPanelBorder, 1.f);
    const D2D1_RECT_F bar{ rect.left + 1.f, rect.top + 1.f, rect.right - 1.f, rect.top + 17.f };
    g.fillRound(bar, kRadius - 1.f, kPanelTitle);
    g.fillRect({ bar.left, bar.top + 8.f, bar.right, bar.bottom }, kPanelTitle);
    // Accent tick + title.
    g.fillRound({ rect.left + 7.f, rect.top + 6.f, rect.left + 10.f, rect.top + 13.f }, 1.5f, kAccent);
    g.text(title, { rect.left + 15.f, rect.top + 1.f, rect.right - 6.f, rect.top + 17.f }, Font::Title, kTitle, Align::Left);
}

// ---------------------------------------------------------------------------
// Knob
// ---------------------------------------------------------------------------
void Knob::draw(Gfx& g, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    const int v = h.value(param);
    const float norm = float(toNormalized(*d, v));
    const float cx = (rect.left + rect.right) * 0.5f;
    const float r = mDiameter * 0.5f;
    const float cy = rect.top + r + 1.f;

    const bool bip = d->kind == Kind::Bipolar;
    const float aVal = kArcStart + kArcSweep * norm;
    const float aMid = kArcStart + kArcSweep * 0.5f;

    // Track and value arc.
    g.arc(cx, cy, r, kArcStart, kArcStart + kArcSweep, hover ? kTrackHover : kTrack, 2.6f);
    if (bip) g.arc(cx, cy, r, std::min(aMid, aVal), std::max(aMid, aVal), kAccentBright, 2.6f);
    else     g.arc(cx, cy, r, kArcStart, aVal, kAccentBright, 2.6f);

    // Body: lifted surface (brighter than the panel) with a soft top highlight.
    const float br = r - 4.f;
    g.fillCircle(cx, cy + 0.8f, br, rgb(0x000000, 0.45f));
    g.fillCircle(cx, cy, br, hover ? kSurfaceHi : kSurface);
    g.arc(cx, cy, br - 0.6f, 3.6f, 5.8f, rgb(0x5A5A6E), 1.f);

    // Pointer.
    const float px = cx + std::cos(aVal) * (br - 2.f), py = cy + std::sin(aVal) * (br - 2.f);
    const float qx = cx + std::cos(aVal) * (br * 0.35f), qy = cy + std::sin(aVal) * (br * 0.35f);
    g.line(qx, qy, px, py, kText, 2.f);

    g.text(label, labelRect(rect, cy + r + 1.f), Font::Small, mDragging ? kText : kTextDim);
}

void Knob::mouseDown(float, float y, unsigned mods, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    if (mods & kCtrl) { doubleClick(h); return; }
    mDragging = true;
    mStartY = y;
    mStartVal = float(h.value(param));
    mDragVal = mStartVal;
    h.beginEdit(param);
    h.showValue(param);
}

void Knob::mouseDrag(float, float y, unsigned mods, UiHost& h)
{
    if (!mDragging) return;
    const ParamDef* d = findParam(param);
    const float span = float(d->maxValue - d->minValue);
    const float scale = (mods & kShift) ? 0.15f : 1.f;
    const float nv = clampv(mStartVal + (mStartY - y) / kDragPixels * span * scale, float(d->minValue), float(d->maxValue));
    if (mods & kShift) { mStartVal = nv; mStartY = y; }   // fine mode accumulates
    const int iv = int(std::lround(nv));
    if (iv != h.value(param)) h.setValue(param, iv);
    h.showValue(param);
}

void Knob::mouseUp(float, float, UiHost& h)
{
    if (!mDragging) return;
    mDragging = false;
    h.endEdit(param);
}

void Knob::wheel(float delta, unsigned mods, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    const int step = (mods & kShift) ? 1 : std::max(1, (d->maxValue - d->minValue) / 64);
    const int v = clampValue(*d, h.value(param) + (delta > 0 ? step : -step));
    h.beginEdit(param);
    h.setValue(param, v);
    h.endEdit(param);
    h.showValue(param);
}

void Knob::doubleClick(UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    h.beginEdit(param);
    h.setValue(param, d->defValue);
    h.endEdit(param);
    h.showValue(param);
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------
void Toggle::draw(Gfx& g, UiHost& h)
{
    const bool on = h.value(param) != 0;
    const float cx = (rect.left + rect.right) * 0.5f;
    const D2D1_RECT_F pill{ cx - 15.f, rect.top + 4.f, cx + 15.f, rect.top + 20.f };
    if (on) {
        g.fillRound({ pill.left - 2.f, pill.top - 2.f, pill.right + 2.f, pill.bottom + 2.f }, 9.f, kAccentGlow);
        g.verticalGradient(pill, 8.f, kAccentBright, kAccent);
    } else {
        g.fillRound(pill, 8.f, hover ? kSurfaceHi : kSurface);
        g.strokeRound(pill, 8.f, kTrack, 1.f);
    }
    // LED
    g.fillCircle(pill.left + 8.f, (pill.top + pill.bottom) * 0.5f, 3.f, on ? rgb(0xF2E8FF) : kAccentDim);
    g.text(on ? L"ON" : L"OFF", { pill.left + 12.f, pill.top, pill.right - 2.f, pill.bottom }, Font::Tiny, on ? kText : kTextFaint);
    g.text(label, labelRect(rect, pill.bottom + 3.f), Font::Small, kTextDim);
}

void Toggle::mouseDown(float, float, unsigned, UiHost& h)
{
    h.beginEdit(param);
    h.setValue(param, h.value(param) ? 0 : 1);
    h.endEdit(param);
    h.showValue(param);
}

// ---------------------------------------------------------------------------
// Selector
// ---------------------------------------------------------------------------
void Selector::draw(Gfx& g, UiHost& h)
{
    const int v = h.value(param);
    const D2D1_RECT_F box{ rect.left + 1.f, rect.top + 4.f, rect.right - 1.f, rect.top + 20.f };
    g.fillRound(box, 3.f, kLcd);
    g.strokeRound(box, 3.f, hover ? kAccent : kLcdBorder, 1.f);
    // Narrow boxes (one grid cell) drop the caret and use the compact font so
    // labels like "osc1,2" or "legato" stay readable.
    const bool compact = (box.right - box.left) < 46.f;
    const std::wstring text = optionText(param, v);
    if (compact) {
        g.text(text, { box.left + 1.f, box.top, box.right - 1.f, box.bottom }, Font::Tiny, kLcdText);
    } else {
        g.text(text, { box.left + 3.f, box.top, box.right - 9.f, box.bottom }, Font::Small, kLcdText);
        const float cx = box.right - 6.f, cy = (box.top + box.bottom) * 0.5f;
        g.triangle(cx - 3.f, cy - 1.5f, cx + 3.f, cy - 1.5f, cx, cy + 2.f, kLcdTextDim);
    }
    g.text(label, labelRect(rect, box.bottom + 3.f), Font::Small, kTextDim);
}

void Selector::mouseDown(float, float, unsigned, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    std::vector<std::wstring> items;
    for (int v = d->minValue; v <= d->maxValue; ++v) items.push_back(optionText(param, v));
    const int id = param;
    const int minV = d->minValue;
    UiHost* host = &h;
    h.openMenu({ rect.left, rect.top + 4.f, rect.right, rect.top + 20.f }, items, h.value(param) - d->minValue,
               [host, id, minV](int index) {
                   host->beginEdit(id);
                   host->setValue(id, minV + index);
                   host->endEdit(id);
                   host->showValue(id);
               });
}

void Selector::wheel(float delta, unsigned, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    int v = h.value(param) + (delta > 0 ? -1 : 1);
    if (v < d->minValue) v = d->maxValue;
    if (v > d->maxValue) v = d->minValue;
    h.beginEdit(param);
    h.setValue(param, v);
    h.endEdit(param);
    h.showValue(param);
}

void Selector::doubleClick(UiHost& h) { (void)h; }

// ---------------------------------------------------------------------------
// NumberBox
// ---------------------------------------------------------------------------
void NumberBox::draw(Gfx& g, UiHost& h)
{
    const int v = h.value(param);
    const D2D1_RECT_F box{ rect.left + 3.f, rect.top + 4.f, rect.right - 3.f, rect.top + 20.f };
    g.fillRound(box, 3.f, kLcd);
    g.strokeRound(box, 3.f, (hover || mDragging) ? kAccent : kLcdBorder, 1.f);
    g.text(valueText(param, v), box, Font::Bold, kLcdText);
    g.text(label, labelRect(rect, box.bottom + 3.f), Font::Small, kTextDim);
}

void NumberBox::mouseDown(float, float y, unsigned mods, UiHost& h)
{
    if (mods & kCtrl) { doubleClick(h); return; }
    mDragging = true;
    mStartY = y;
    mStartVal = float(h.value(param));
    h.beginEdit(param);
    h.showValue(param);
}

void NumberBox::mouseDrag(float, float y, unsigned, UiHost& h)
{
    if (!mDragging) return;
    const ParamDef* d = findParam(param);
    const int v = clampValue(*d, int(std::lround(mStartVal + (mStartY - y) / 8.f)));
    if (v != h.value(param)) h.setValue(param, v);
    h.showValue(param);
}

void NumberBox::mouseUp(float, float, UiHost& h)
{
    if (!mDragging) return;
    mDragging = false;
    h.endEdit(param);
}

void NumberBox::wheel(float delta, unsigned, UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    h.beginEdit(param);
    h.setValue(param, clampValue(*d, h.value(param) + (delta > 0 ? 1 : -1)));
    h.endEdit(param);
    h.showValue(param);
}

void NumberBox::doubleClick(UiHost& h)
{
    const ParamDef* d = findParam(param);
    if (!d) return;
    h.beginEdit(param);
    h.setValue(param, d->defValue);
    h.endEdit(param);
    h.showValue(param);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
void Button::draw(Gfx& g, UiHost&)
{
    const bool lit = isOn && isOn();
    switch (lit ? Style::Accent : mStyle) {
    case Style::Accent:
        g.verticalGradient(rect, 4.f, hover ? kAccentBright : kAccent, kAccentDim);
        g.strokeRound(rect, 4.f, hover ? kAccentBright : kAccent, 1.f);
        break;
    case Style::Danger:
        g.verticalGradient(rect, 4.f, hover ? rgb(0xB0303F) : rgb(0x7E2230), rgb(0x4A141D));
        g.strokeRound(rect, 4.f, hover ? rgb(0xE0566A) : rgb(0x8E2A39), 1.f);
        break;
    default:
        g.fillRound(rect, 4.f, hover ? kSurfaceHi : kSurface);
        g.strokeRound(rect, 4.f, hover ? kAccent : kTrack, 1.f);
        break;
    }
    if (label == L"zoom") {
        // Vector magnifier with a plus.
        const float cx = (rect.left + rect.right) * 0.5f - 1.5f, cy = (rect.top + rect.bottom) * 0.5f - 1.5f;
        const D2D1_COLOR_F c = hover ? kText : rgb(0xCFCCDA);
        g.strokeCircle(cx, cy, 5.f, c, 1.6f);
        g.line(cx + 3.6f, cy + 3.6f, cx + 7.f, cy + 7.f, c, 2.2f);
        g.line(cx - 2.6f, cy, cx + 2.6f, cy, c, 1.3f);
        g.line(cx, cy - 2.6f, cx, cy + 2.6f, c, 1.3f);
        return;
    }
    if (label == L"dice") {
        // Vector die (five pips) - no font dependency.
        const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f, s = 6.f;
        const D2D1_RECT_F die{ cx - s, cy - s, cx + s, cy + s };
        g.fillRound(die, 3.f, hover ? kText : rgb(0xCFCCDA));
        const D2D1_COLOR_F pip = mStyle == Style::Normal && !lit ? (hover ? kSurfaceHi : kSurface) : kAccentDim;
        const float o = s * 0.5f;
        for (auto [dx, dy] : { std::pair{ -o, -o }, { o, -o }, { 0.f, 0.f }, { -o, o }, { o, o } })
            g.fillCircle(cx + dx, cy + dy, 1.4f, pip);
        return;
    }
    if (label == L"keys") {
        // Vector piano octave fragment: three white keys, two black.
        const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
        const D2D1_COLOR_F c = (hover || lit) ? kText : rgb(0xCFCCDA);
        const D2D1_COLOR_F gap = lit ? kAccentDim : (hover ? kSurfaceHi : kSurface);
        const float w = 4.f, top = cy - 6.f, bottom = cy + 6.f, left = cx - 1.5f * w;
        for (int i = 0; i < 3; ++i)
            g.fillRect({ left + i * w + 0.5f, top, left + (i + 1) * w - 0.5f, bottom }, c);
        for (int i = 1; i < 3; ++i)
            g.fillRect({ left + i * w - 1.5f, top, left + i * w + 1.5f, top + 7.f }, gap);
        return;
    }
    g.text(label, rect, Font::Bold, hover ? kText : rgb(0xCFCCDA));
}

void Button::mouseDown(float, float, unsigned, UiHost&)
{
    if (mOnClick) mOnClick();
}

// ---------------------------------------------------------------------------
// Wheel
// ---------------------------------------------------------------------------
static D2D1_RECT_F wheelSlot(const D2D1_RECT_F& r) { return { r.left, r.top, r.right, r.bottom - 14.f }; }

void Wheel::draw(Gfx& g, UiHost&)
{
    const float v = std::clamp(mDragging ? mDragVal : mGet(), 0.f, 1.f);
    const D2D1_RECT_F slot = wheelSlot(rect);
    g.fillRound(slot, 4.f, kLcd);
    g.strokeRound(slot, 4.f, hover || mDragging ? kAccent : kLcdBorder, 1.f);

    // Cylinder: dark at the rims, lit in the middle.
    const D2D1_RECT_F body{ slot.left + 3.f, slot.top + 3.f, slot.right - 3.f, slot.bottom - 3.f };
    const float mid = (body.top + body.bottom) * 0.5f;
    g.verticalGradient({ body.left, body.top, body.right, mid }, 0.f, rgb(0x15151B), rgb(0x50505F));
    g.verticalGradient({ body.left, mid, body.right, body.bottom }, 0.f, rgb(0x50505F), rgb(0x15151B));

    // Ridges roll with the value; the accent bar marks the position.
    const float travel = (body.bottom - body.top) - 10.f;
    const float pos = (v - 0.5f) * travel;                 // + = up
    g.pushClip(body);
    const float spacing = 4.f;
    float phase = std::fmod(-pos, spacing);
    if (phase < 0.f) phase += spacing;
    for (float y = body.top + phase; y < body.bottom; y += spacing) {
        const float edge = std::fabs(y - mid) / ((body.bottom - body.top) * 0.5f);
        g.line(body.left, y, body.right, y, rgb(0x0A0A0E, 0.35f + 0.5f * edge), 1.f);
    }
    const float y = mid - pos;
    g.fillRect({ body.left, y - 1.5f, body.right, y + 1.5f }, mDragging ? kAccentBright : rgb(0xB79BE6));
    g.popClip();
    if (mSpring) g.line(slot.left - 4.f, mid, slot.left - 1.f, mid, kTextFaint, 1.f);   // centre detent

    g.text(label, { rect.left - 8.f, slot.bottom + 1.f, rect.right + 8.f, rect.bottom }, Font::Tiny, kTextDim);
}

void Wheel::mouseDown(float, float y, unsigned, UiHost& h)
{
    mDragging = true;
    mStartY = y;
    mStartVal = mDragVal = std::clamp(mGet(), 0.f, 1.f);
    mSet(mDragVal, true);   // take ownership from incoming MIDI
    h.invalidate();
}

void Wheel::mouseDrag(float, float y, unsigned mods, UiHost& h)
{
    if (!mDragging) return;
    const D2D1_RECT_F slot = wheelSlot(rect);
    const float px = (slot.bottom - slot.top) * ((mods & kShift) ? 4.f : 1.f);   // shift = fine
    const float v = std::clamp(mStartVal + (mStartY - y) / px, 0.f, 1.f);
    if (v == mDragVal) return;
    mDragVal = v;
    mSet(v, true);
    h.invalidate();
}

void Wheel::mouseUp(float, float, UiHost& h)
{
    if (!mDragging) return;
    mDragging = false;
    mSet(mSpring ? rest() : mDragVal, false);
    h.invalidate();
}

void Wheel::wheel(float delta, unsigned mods, UiHost& h)
{
    if (mSpring || mDragging) return;   // a sprung wheel has nowhere to stay
    const float step = (mods & kShift) ? 0.01f : 0.05f;
    mSet(std::clamp(mGet() + (delta > 0 ? step : -step), 0.f, 1.f), false);
    h.invalidate();
}

void Wheel::doubleClick(UiHost& h)
{
    mSet(rest(), false);
    h.invalidate();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
static constexpr bool kIsBlack[12]  = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
static constexpr int  kWhiteIdx[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };   // black: white key to its left

int Keyboard::whiteCount() const
{
    int n = 0;
    for (int k = mLow; k < mLow + mCount; ++k) n += kIsBlack[k % 12] ? 0 : 1;
    return n;
}

D2D1_RECT_F Keyboard::keyRect(int note) const
{
    const float ww = (rect.right - rect.left) / float(whiteCount());
    const int wi = (note / 12 - mLow / 12) * 7 + kWhiteIdx[note % 12] - kWhiteIdx[mLow % 12];
    if (!kIsBlack[note % 12]) {
        const float x = rect.left + wi * ww;
        return { x, rect.top, x + ww, rect.bottom };
    }
    const float bw = ww * 0.58f, cx = rect.left + (wi + 1) * ww;
    return { cx - bw * 0.5f, rect.top, cx + bw * 0.5f, rect.top + (rect.bottom - rect.top) * 0.6f };
}

int Keyboard::keyAt(float x, float y, float* velocity) const
{
    // Clamped so a glide that leaves the keyboard stays on the nearest key.
    x = std::clamp(x, rect.left, rect.right - 0.5f);
    y = std::clamp(y, rect.top, rect.bottom - 0.5f);
    auto hit = [&](int k) {
        const D2D1_RECT_F r = keyRect(k);
        if (x < r.left || x >= r.right || y < r.top || y >= r.bottom) return false;
        if (velocity) *velocity = std::clamp(0.3f + 0.7f * (y - r.top) / (r.bottom - r.top), 0.05f, 1.f);
        return true;
    };
    for (int k = mLow; k < mLow + mCount; ++k) if (kIsBlack[k % 12] && hit(k)) return k;
    for (int k = mLow; k < mLow + mCount; ++k) if (!kIsBlack[k % 12] && hit(k)) return k;
    return -1;
}

void Keyboard::draw(Gfx& g, UiHost&)
{
    g.fillRect(rect, kLcd);
    for (int k = mLow; k < mLow + mCount; ++k) {
        if (kIsBlack[k % 12]) continue;
        const D2D1_RECT_F kr = keyRect(k);
        const D2D1_RECT_F r{ kr.left + 0.5f, kr.top, kr.right - 0.5f, kr.bottom };
        const bool down = k == mNote;
        if (down) g.verticalGradient(r, 2.f, kAccentBright, kAccent);
        else      g.verticalGradient(r, 2.f, rgb(0xBDBAC8), rgb(0xF0EEF5));
        if (k % 12 == 0)
            g.text(L"C" + std::to_wstring(k / 12 - 2), { r.left, r.bottom - 14.f, r.right, r.bottom - 2.f }, Font::Tiny,
                   down ? kText : (k == 60 ? kAccent : kTextFaint));
    }
    for (int k = mLow; k < mLow + mCount; ++k) {
        if (!kIsBlack[k % 12]) continue;
        const D2D1_RECT_F r = keyRect(k);
        if (k == mNote) g.verticalGradient(r, 2.f, kAccent, kAccentDim);
        else            g.verticalGradient(r, 2.f, rgb(0x3C3C48), rgb(0x111116));
    }
    g.fillRect({ rect.left, rect.top, rect.right, rect.top + 2.f }, rgb(0x000000, 0.45f));   // key-bed shadow
}

void Keyboard::mouseDown(float x, float y, unsigned, UiHost& h)
{
    float vel = 0.f;
    if (mNote >= 0) mOnNote(mNote, 0.f);
    mNote = keyAt(x, y, &vel);
    if (mNote >= 0) mOnNote(mNote, vel);
    h.invalidate();
}

void Keyboard::mouseDrag(float x, float y, unsigned, UiHost& h)
{
    float vel = 0.f;
    const int k = keyAt(x, y, &vel);
    if (k == mNote) return;
    if (mNote >= 0) mOnNote(mNote, 0.f);
    mNote = k;
    if (mNote >= 0) mOnNote(mNote, vel);
    h.invalidate();
}

void Keyboard::mouseUp(float, float, UiHost& h)
{
    if (mNote >= 0) mOnNote(mNote, 0.f);
    mNote = -1;
    h.invalidate();
}

} // namespace cs
