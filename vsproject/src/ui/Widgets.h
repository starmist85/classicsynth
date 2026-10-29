#pragma once
// Retained-mode controls drawn with Direct2D. Coordinates are DIPs on the
// 720 x 480 canvas (+100 for the keyboard strip). Each control talks to the editor through UiHost.

#include "ui/Graphics.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cs {

class UiHost {
public:
    virtual ~UiHost() = default;
    virtual int  value(int id) = 0;
    virtual void beginEdit(int id) = 0;
    virtual void setValue(int id, int v) = 0;
    virtual void endEdit(int id) = 0;
    virtual void openMenu(const D2D1_RECT_F& anchor, const std::vector<std::wstring>& items, int selected,
                          std::function<void(int)> onPick) = 0;
    virtual void showValue(int id) = 0;      // value bubble + LCD readout
    virtual void invalidate() = 0;
};

enum Mods : unsigned { kShift = 1, kCtrl = 2, kAlt = 4 };

class Widget {
public:
    virtual ~Widget() = default;
    D2D1_RECT_F rect{};
    int param = -1;
    std::wstring label;
    bool hover = false;

    bool contains(float x, float y) const { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
    virtual void draw(Gfx& g, UiHost& h) = 0;
    virtual void mouseDown(float, float, unsigned, UiHost&) {}
    virtual void mouseDrag(float, float, unsigned, UiHost&) {}
    virtual void mouseUp(float, float, UiHost&) {}
    virtual void wheel(float, unsigned, UiHost&) {}
    virtual void doubleClick(UiHost&) {}
    virtual bool wantsCapture() const { return false; }
    virtual bool wantsDoubleClick() const { return true; }  // false: a double click is a second mouseDown
};

// Rotary knob. Bipolar params draw their arc from the centre.
class Knob : public Widget {
public:
    Knob(int id, const wchar_t* lbl, float diameter) : mDiameter(diameter) { param = id; label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    void mouseDrag(float x, float y, unsigned mods, UiHost& h) override;
    void mouseUp(float x, float y, UiHost& h) override;
    void wheel(float delta, unsigned mods, UiHost& h) override;
    void doubleClick(UiHost& h) override;
    bool wantsCapture() const override { return true; }
private:
    float mDiameter;
    float mStartY = 0, mStartVal = 0, mDragVal = 0;
    bool  mDragging = false;
};

// Pill toggle with an LED.
class Toggle : public Widget {
public:
    Toggle(int id, const wchar_t* lbl) { param = id; label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
};

// LCD-style option display; click opens a menu, wheel steps.
class Selector : public Widget {
public:
    Selector(int id, const wchar_t* lbl) { param = id; label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    void wheel(float delta, unsigned mods, UiHost& h) override;
    void doubleClick(UiHost& h) override;
};

// Numeric LCD box (key shift, polyphony, unison voices ...): drag or wheel.
class NumberBox : public Widget {
public:
    NumberBox(int id, const wchar_t* lbl) { param = id; label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    void mouseDrag(float x, float y, unsigned mods, UiHost& h) override;
    void mouseUp(float x, float y, UiHost& h) override;
    void wheel(float delta, unsigned mods, UiHost& h) override;
    void doubleClick(UiHost& h) override;
    bool wantsCapture() const override { return true; }
private:
    float mStartY = 0, mStartVal = 0;
    bool  mDragging = false;
};

// Plain push button with a callback. With isOn set it acts as a switch and
// draws lit (accent) while isOn() is true.
class Button : public Widget {
public:
    enum class Style { Normal, Accent, Danger };
    Button(const wchar_t* lbl, std::function<void()> onClick, Style style = Style::Normal)
        : mOnClick(std::move(onClick)), mStyle(style) { label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    std::function<bool()> isOn;
private:
    std::function<void()> mOnClick;
    Style mStyle;
};

// Pitch / modulation wheel (vertical thumbwheel). Value is normalized 0..1;
// a spring-loaded wheel (pitch) returns to 0.5 when released. set(value, held):
// held is true while dragging. During a drag the wheel shows its own value, not
// get(), which incoming MIDI may be changing at the same time.
class Wheel : public Widget {
public:
    Wheel(const wchar_t* lbl, bool spring, std::function<float()> get, std::function<void(float, bool)> set)
        : mSpring(spring), mGet(std::move(get)), mSet(std::move(set)) { label = lbl; }
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    void mouseDrag(float x, float y, unsigned mods, UiHost& h) override;
    void mouseUp(float x, float y, UiHost& h) override;
    void wheel(float delta, unsigned mods, UiHost& h) override;
    void doubleClick(UiHost& h) override;
    bool wantsCapture() const override { return true; }
private:
    float rest() const { return mSpring ? 0.5f : 0.f; }
    bool  mSpring;
    std::function<float()> mGet;
    std::function<void(float, bool)> mSet;
    float mStartY = 0, mStartVal = 0, mDragVal = 0;
    bool  mDragging = false;
};

// Piano keyboard. Click plays a note (velocity from how far down the key is
// hit); dragging glides across keys. onNote(note, 0) is a note-off.
class Keyboard : public Widget {
public:
    Keyboard(int lowNote, int numKeys, std::function<void(int, float)> onNote)
        : mLow(lowNote), mCount(numKeys), mOnNote(std::move(onNote)) {}
    void draw(Gfx& g, UiHost& h) override;
    void mouseDown(float x, float y, unsigned mods, UiHost& h) override;
    void mouseDrag(float x, float y, unsigned mods, UiHost& h) override;
    void mouseUp(float x, float y, UiHost& h) override;
    bool wantsCapture() const override { return true; }
    bool wantsDoubleClick() const override { return false; }   // fast repeats play notes
private:
    int  keyAt(float x, float y, float* velocity) const;
    D2D1_RECT_F keyRect(int note) const;
    int  whiteCount() const;
    int  mLow, mCount;
    std::function<void(int, float)> mOnNote;
    int  mNote = -1;
};

// Section panel (Oscillators, Filter, ...). Drawn under its controls.
struct Section {
    std::wstring title;
    D2D1_RECT_F rect;
    void draw(Gfx& g) const;
};

std::wstring valueText(int id, int value);
std::wstring optionText(int id, int value);

} // namespace cs
