#pragma once
// Popup-overlay preset browser.
//   single click on a program  -> audition (plays C3 with that patch)
//   double click on a program  -> load it and close the browser
// Banks: factory + user (see PresetLibrary). Search covers the whole library
// (indexed in the background). Save panel: user bank slot / .fxp / .sy1 / .fxb.

#include "presets/PresetLibrary.h"
#include "ui/Graphics.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cs {

class Controller;

class PresetBrowser {
public:
    PresetBrowser(Controller& c, HWND& hwnd, std::function<void()> invalidate)
        : mCtl(c), mHwnd(hwnd), mInvalidate(std::move(invalidate)) {}

    bool visible() const { return mVisible; }
    void open(bool saveMode = false);
    void close();

    void draw(Gfx& g);
    void tick();                                   // ~30 Hz, for indexing progress
    // Input; all return true if handled (the browser is modal while open).
    bool mouseDown(float x, float y, bool doubleClick);
    void mouseMove(float x, float y);
    void wheel(float x, float y, float delta);
    bool keyChar(wchar_t ch);
    bool keyDown(int vk);

    // Header shortcuts (step through the current bank).
    void stepProgram(int delta);
    void randomProgram();                          // dice: random preset from the whole library
    std::wstring currentLocation();                // "Bank name · 012"

private:
    struct Hit { int bank = -1; int program = -1; };
    struct Row { bool header; std::wstring text; int bank; };
    enum class Field { None, Search, Name, Slot };

    void refreshBanks();
    void rebuildRows();
    void selectBank(int index);
    void rebuildResults();
    void loadProgram(int bank, int program, bool andClose);
    void openFile();
    void doSaveToBank();
    void doExport(int kind);                       // 0 fxp, 1 sy1, 2 fxb (bank)
    int  nextFreeSlot(PresetBank& b);

    D2D1_RECT_F panelRect() const { return { 24.f, 22.f, 696.f, 458.f }; }

    Controller& mCtl;
    HWND& mHwnd;
    std::function<void()> mInvalidate;
    bool mVisible = false;
    bool mSaveMode = false;

    std::vector<std::shared_ptr<PresetBank>> mBanks;
    std::vector<Row> mRows;                          // bank list rows (with group headers)
    int mBank = -1;                                  // selected bank index
    int mSelected = -1;                              // selected program (index into results)
    std::vector<Hit> mResults;                       // programs shown on the right
    float mBankScroll = 0, mProgScroll = 0;
    int mHoverRow = -1, mHoverProg = -1, mHoverButton = -1;
    std::wstring mSearch;
    Field mFocus = Field::None;
    float mLastIndexProgress = -1.f;

    // Save panel
    std::wstring mSaveName;
    int mSaveBank = -1;                              // index into mBanks (writable)
    int mSaveSlot = 1;
    std::wstring mStatus;
};

} // namespace cs
