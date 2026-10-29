#include "ui/PresetBrowser.h"
#include "controller/Controller.h"
#include "presets/PresetFormats.h"
#include "ui/Theme.h"

#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace cs {

using namespace theme;

namespace {
constexpr float kRowH = 18.f;
const D2D1_RECT_F kBankArea{ 32.f, 62.f, 262.f, 422.f };
const D2D1_RECT_F kProgArea{ 270.f, 62.f, 688.f, 422.f };
const D2D1_RECT_F kSearch{ 196.f, 29.f, 436.f, 49.f };

enum ButtonId {
    kBtnOpen = 1, kBtnSave, kBtnInit, kBtnClose,
    kBtnSaveToBank = 10, kBtnExportFxp, kBtnExportSy1, kBtnExportFxb, kBtnSlotDown, kBtnSlotUp, kBtnNewBank, kBtnBack,
};

struct HotButton { D2D1_RECT_F r; int id; const wchar_t* label; bool accent; };

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
float clampScroll(float v, float maxScroll) { return v < 0.f ? 0.f : (v > maxScroll ? maxScroll : v); }
bool isOpenedFile(const PresetBank& b) { return b.name.rfind(L"(file) ", 0) == 0; }

std::wstring lower(std::wstring s)
{
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

std::wstring numberText(int n)
{
    wchar_t b[16];
    swprintf(b, 16, L"%03d", n);
    return b;
}

// Buttons for the current mode (shared by draw and hit testing).
std::vector<HotButton> buttonsFor(bool saveMode)
{
    std::vector<HotButton> v = {
        { { 444.f, 29.f, 526.f, 49.f }, kBtnOpen,  L"Open File…", false },
        { { 532.f, 29.f, 590.f, 49.f }, kBtnInit,  L"Init", false },
        { { 596.f, 29.f, 654.f, 49.f }, kBtnSave,  saveMode ? L"Browse" : L"Save…", saveMode },
        { { 660.f, 29.f, 686.f, 49.f }, kBtnClose, L"✕", false },
    };
    if (saveMode) {
        const float x0 = 470.f, x1 = 670.f;
        v.push_back({ { x0, 172.f, x0 + 30.f, 194.f }, kBtnSlotDown, L"−", false });
        v.push_back({ { x1 - 30.f, 172.f, x1, 194.f }, kBtnSlotUp, L"+", false });
        v.push_back({ { x0, 214.f, x1, 238.f }, kBtnSaveToBank, L"Save to user bank", true });
        v.push_back({ { x0, 250.f, x1, 272.f }, kBtnNewBank, L"New user bank", false });
        v.push_back({ { x0, 296.f, x1, 318.f }, kBtnExportFxp, L"Export program (.fxp)…", false });
        v.push_back({ { x0, 324.f, x1, 346.f }, kBtnExportSy1, L"Export program (.sy1)…", false });
        v.push_back({ { x0, 352.f, x1, 374.f }, kBtnExportFxb, L"Export bank (.fxb)…", false });
    }
    return v;
}
} // namespace

// ===========================================================================
void PresetBrowser::open(bool saveMode)
{
    PresetLibrary& lib = mCtl.library();
    if (!lib.scanned()) lib.scan();
    lib.startIndexing();
    refreshBanks();

    mVisible = true;
    mSaveMode = saveMode;
    mFocus = saveMode ? Field::Name : Field::None;
    mStatus.clear();
    mSaveName = widen(mCtl.programName());

    // Re-select the current bank.
    int sel = -1;
    for (size_t i = 0; i < mBanks.size(); ++i)
        if (mBanks[i]->path == lib.currentBankPath) sel = int(i);
    if (sel < 0 && !mBanks.empty()) sel = 0;
    selectBank(sel);
    if (sel >= 0 && mBanks[size_t(sel)]->path == lib.currentBankPath) mSelected = lib.currentProgram;

    if (saveMode) {
        mSaveBank = (mBank >= 0 && mBanks[size_t(mBank)]->writable()) ? mBank : -1;
        if (mSaveBank < 0)
            for (size_t i = 0; i < mBanks.size(); ++i) if (mBanks[i]->writable()) { mSaveBank = int(i); break; }
        if (mSaveBank >= 0) mSaveSlot = nextFreeSlot(*mBanks[size_t(mSaveBank)]);
    }
    mInvalidate();
}

void PresetBrowser::close()
{
    mVisible = false;
    mFocus = Field::None;
    mInvalidate();
}

void PresetBrowser::refreshBanks()
{
    // Keep banks opened from files (they are not part of the library).
    std::vector<std::shared_ptr<PresetBank>> opened;
    for (auto& b : mBanks) if (isOpenedFile(*b)) opened.push_back(b);
    mBanks = opened;
    auto lib = mCtl.library().banks();
    mBanks.insert(mBanks.end(), lib.begin(), lib.end());
    rebuildRows();
}

void PresetBrowser::rebuildRows()
{
    mRows.clear();
    const wchar_t* lastGroup = nullptr;
    for (size_t i = 0; i < mBanks.size(); ++i) {
        const wchar_t* group = isOpenedFile(*mBanks[i]) ? L"OPENED" : mBanks[i]->factory ? L"FACTORY" : L"USER";
        if (group != lastGroup) { mRows.push_back({ true, group, -1 }); lastGroup = group; }
        mRows.push_back({ false, isOpenedFile(*mBanks[i]) ? mBanks[i]->name.substr(7) : mBanks[i]->name, int(i) });
    }
}

void PresetBrowser::selectBank(int index)
{
    mBank = index;
    mSelected = -1;
    mProgScroll = 0;
    if (index >= 0 && index < int(mBanks.size())) mCtl.library().ensureLoaded(*mBanks[size_t(index)]);
    if (mSaveMode && index >= 0 && mBanks[size_t(index)]->writable()) {
        mSaveBank = index;
        mSaveSlot = nextFreeSlot(*mBanks[size_t(index)]);
    }
    rebuildResults();
}

void PresetBrowser::rebuildResults()
{
    mResults.clear();
    if (mSearch.empty()) {
        if (mBank >= 0 && mBank < int(mBanks.size()) && mBanks[size_t(mBank)]->loaded)
            for (size_t p = 0; p < mBanks[size_t(mBank)]->patches.size(); ++p) mResults.push_back({ mBank, int(p) });
    } else {
        const std::wstring q = lower(mSearch);
        std::lock_guard<std::mutex> lock(mCtl.library().mutex());
        for (size_t b = 0; b < mBanks.size() && mResults.size() < 3000; ++b) {
            const PresetBank& bank = *mBanks[b];
            if (!bank.loaded) continue;
            const bool bankMatch = lower(bank.name).find(q) != std::wstring::npos;
            for (size_t p = 0; p < bank.patches.size(); ++p)
                if (bankMatch || lower(widen(bank.patches[p].name)).find(q) != std::wstring::npos)
                    mResults.push_back({ int(b), int(p) });
        }
    }
    if (mSelected >= int(mResults.size())) mSelected = -1;
    mInvalidate();
}

int PresetBrowser::nextFreeSlot(PresetBank& b)
{
    mCtl.library().ensureLoaded(b);
    int n = 1;
    while (std::find(b.numbers.begin(), b.numbers.end(), n) != b.numbers.end() && n < 999) ++n;
    return n;
}

void PresetBrowser::loadProgram(int bank, int program, bool andClose)
{
    if (bank < 0 || bank >= int(mBanks.size())) return;
    PresetBank& b = *mBanks[size_t(bank)];
    if (program < 0 || program >= int(b.patches.size())) return;
    mCtl.loadPatch(b.patches[size_t(program)]);
    mCtl.library().currentBankPath = b.path;
    mCtl.library().currentProgram = program;
    if (andClose) close();
}

std::wstring PresetBrowser::currentLocation()
{
    PresetLibrary& lib = mCtl.library();
    if (lib.currentBankPath.empty()) return L"No bank selected";
    for (auto& b : lib.banks()) {
        if (b->path != lib.currentBankPath) continue;
        std::wstring s = b->name;
        if (lib.currentProgram >= 0 && lib.currentProgram < int(b->numbers.size()))
            s += L"  ·  " + numberText(b->numbers[size_t(lib.currentProgram)]);
        return s;
    }
    return L"";
}

void PresetBrowser::stepProgram(int delta)
{
    PresetLibrary& lib = mCtl.library();
    if (!lib.scanned()) lib.scan();
    auto banks = lib.banks();
    std::shared_ptr<PresetBank> bank;
    for (auto& b : banks) if (b->path == lib.currentBankPath) bank = b;
    if (!bank) {
        for (auto& b : banks) { lib.ensureLoaded(*b); if (!b->patches.empty()) { bank = b; break; } }
        if (!bank) return;
        lib.currentBankPath = bank->path;
        lib.currentProgram = -1;
    }
    lib.ensureLoaded(*bank);
    const int n = int(bank->patches.size());
    if (n == 0) return;
    int idx = lib.currentProgram < 0 ? (delta > 0 ? 0 : n - 1) : (lib.currentProgram + delta + n) % n;
    lib.currentProgram = idx;
    mCtl.loadPatch(bank->patches[size_t(idx)]);
}

void PresetBrowser::randomProgram()
{
    PresetLibrary& lib = mCtl.library();
    if (!lib.scanned()) lib.scan();
    lib.startIndexing();
    auto banks = lib.banks();
    if (banks.empty()) return;

    static uint32_t seed = uint32_t(GetTickCount64()) | 1u;
    auto rnd = [&](uint32_t n) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return n ? seed % n : 0u; };

    // Weight by program count among indexed banks so big banks are not
    // under-represented; fall back to a random bank while indexing.
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(lib.mutex());
        for (auto& b : banks) if (b->loaded) total += b->patches.size();
    }
    std::shared_ptr<PresetBank> pick;
    int program = -1;
    if (total > 0) {
        size_t r = rnd(uint32_t(total));
        std::lock_guard<std::mutex> lock(lib.mutex());
        for (auto& b : banks) {
            if (!b->loaded) continue;
            if (r < b->patches.size()) { pick = b; program = int(r); break; }
            r -= b->patches.size();
        }
    }
    for (int tries = 0; !pick && tries < 16; ++tries) {
        auto b = banks[rnd(uint32_t(banks.size()))];
        lib.ensureLoaded(*b);
        if (!b->patches.empty()) { pick = b; program = int(rnd(uint32_t(b->patches.size()))); }
    }
    if (!pick) return;
    lib.currentBankPath = pick->path;
    lib.currentProgram = program;
    mCtl.loadPatch(pick->patches[size_t(program)]);
}

// ===========================================================================
// File dialogs
// ===========================================================================
static bool fileDialog(HWND owner, bool save, const wchar_t* defExt, const std::vector<COMDLG_FILTERSPEC>& filters,
                       const std::wstring& defName, std::wstring& out)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return false;
    dlg->SetFileTypes(UINT(filters.size()), filters.data());
    if (defExt) dlg->SetDefaultExtension(defExt);
    if (!defName.empty()) dlg->SetFileName(defName.c_str());
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { out = path; CoTaskMemFree(path); ok = true; }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

static std::wstring safeFileName(std::wstring n)
{
    for (auto& c : n) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return n.empty() ? L"preset" : n;
}

void PresetBrowser::openFile()
{
    std::wstring path;
    if (!fileDialog(GetAncestor(mHwnd, GA_ROOT), false, nullptr,
                    { { L"Synth1 / VST presets", L"*.fxp;*.fxb;*.sy1;*.zip" }, { L"All files", L"*.*" } }, L"", path))
        return;
    const size_t dot = path.rfind(L'.');
    const std::wstring ext = dot == std::wstring::npos ? L"" : lower(path.substr(dot));
    std::vector<uint8_t> bytes;

    const size_t slashPos = path.find_last_of(L"\\/");
    const std::wstring fileName = path.substr(slashPos + 1);
    if (!readFileBytes(path, bytes)) { mStatus = L"Could not read " + fileName; mInvalidate(); return; }

    // Compatibility check first: only Synth1 / ClassicSynth data is accepted.
    std::string why;
    if (ext == L".fxp" || ext == L".sy1") {
        Patch p;
        const bool ok = ext == L".fxp" ? parseFxp(bytes, p, &why) : parseSy1(std::string(bytes.begin(), bytes.end()), p, &why);
        if (!ok) { mStatus = fileName + L": " + widen(why); mInvalidate(); return; }
        mCtl.loadPatch(p);
        mCtl.library().currentBankPath.clear();
        close();
        return;
    }
    if (ext == L".fxb") {
        std::vector<Patch> tmp;
        if (!parseFxb(bytes, tmp, &why)) { mStatus = fileName + L": " + widen(why); mInvalidate(); return; }
    } else if (ext == L".zip") {
        std::vector<ZipBankFolder> tmp;
        if (!readZipBank(path, tmp, &why)) { mStatus = fileName + L": " + widen(why); mInvalidate(); return; }
    } else {
        mStatus = L"Unsupported file type: " + fileName + L" (use .fxp, .fxb, .sy1 or .zip)";
        mInvalidate();
        return;
    }

    // .fxb / .zip -> open as a temporary bank at the top of the list.
    auto bank = std::make_shared<PresetBank>();
    bank->kind = ext == L".zip" ? PresetBank::Kind::Zip : PresetBank::Kind::Fxb;
    bank->path = path;
    const size_t slash = path.find_last_of(L"\\/");
    bank->name = L"(file) " + path.substr(slash + 1);
    mCtl.library().ensureLoaded(*bank);
    if (bank->patches.empty()) { mStatus = L"No programs found in " + path; mInvalidate(); return; }
    mBanks.insert(mBanks.begin(), bank);
    rebuildRows();
    mSearch.clear();
    selectBank(0);
    mStatus = L"Opened " + bank->name.substr(7);
}

void PresetBrowser::doSaveToBank()
{
    if (mSaveBank < 0 || mSaveBank >= int(mBanks.size()) || !mBanks[size_t(mSaveBank)]->writable()) {
        mStatus = L"Select a user bank on the left (factory banks are read-only).";
        return;
    }
    Patch p = mCtl.currentPatch();
    p.name = narrowAnsi(mSaveName.empty() ? L"untitled" : mSaveName);
    PresetBank& b = *mBanks[size_t(mSaveBank)];
    if (!mCtl.library().saveToBank(b, mSaveSlot, p)) { mStatus = L"Saving failed (folder not writable?)"; return; }
    mCtl.setProgramName(p.name);
    mCtl.library().currentBankPath = b.path;
    for (size_t i = 0; i < b.numbers.size(); ++i) if (b.numbers[i] == mSaveSlot) mCtl.library().currentProgram = int(i);
    mStatus = L"Saved " + numberText(mSaveSlot) + L" to " + b.name;
    mSaveSlot = nextFreeSlot(b);
    if (mBank == mSaveBank) rebuildResults();
}

void PresetBrowser::doExport(int kind)
{
    Patch p = mCtl.currentPatch();
    if (!mSaveName.empty()) p.name = narrowAnsi(mSaveName);
    std::wstring path;
    const std::wstring base = safeFileName(widen(p.name));
    bool ok = false;
    if (kind == 0) {
        if (!fileDialog(GetAncestor(mHwnd, GA_ROOT), true, L"fxp", { { L"VST program (*.fxp)", L"*.fxp" } }, base, path)) return;
        ok = writeFileBytes(path, writeFxp(p));
    } else if (kind == 1) {
        if (!fileDialog(GetAncestor(mHwnd, GA_ROOT), true, L"sy1", { { L"Synth1 patch (*.sy1)", L"*.sy1" } }, base, path)) return;
        const std::string s = writeSy1(p);
        ok = writeFileBytes(path, std::vector<uint8_t>(s.begin(), s.end()));
    } else {
        std::vector<Patch> bank;
        std::wstring name = L"bank";
        if (mBank >= 0 && mBank < int(mBanks.size()) && !mBanks[size_t(mBank)]->patches.empty()) {
            bank = mBanks[size_t(mBank)]->patches;
            name = safeFileName(mBanks[size_t(mBank)]->name);
        } else {
            bank.push_back(p);
        }
        if (!fileDialog(GetAncestor(mHwnd, GA_ROOT), true, L"fxb", { { L"VST bank (*.fxb)", L"*.fxb" } }, name, path)) return;
        ok = writeFileBytes(path, writeFxb(bank));
    }
    mStatus = ok ? L"Exported " + path : L"Export failed: " + path;
}

// ===========================================================================
// Drawing
// ===========================================================================
void PresetBrowser::draw(Gfx& g)
{
    if (!mVisible) return;
    const D2D1_RECT_F P = panelRect();
    g.fillRect({ 0, 0, kWidth, kHeight }, kScrim);
    g.fillRound({ P.left - 1, P.top + 2, P.right + 1, P.bottom + 4 }, 10.f, rgb(0x000000, 0.5f));
    g.fillRound(P, 9.f, kOverlay);
    g.strokeRound(P, 9.f, kOverlayBorder, 1.f);

    // Title + search
    g.fillRound({ P.left + 7, 30.f, P.left + 10, 48.f }, 1.5f, kAccent);
    g.text(mSaveMode ? L"SAVE PRESET" : L"PRESETS", { P.left + 16, 27.f, 190.f, 51.f }, Font::Bold, kTitle, Align::Left);
    g.fillRound(kSearch, 4.f, kLcd);
    g.strokeRound(kSearch, 4.f, mFocus == Field::Search ? kAccentBright : kLcdBorder, 1.f);
    const bool showCaret = mFocus == Field::Search && (GetTickCount64() / 500) % 2 == 0;
    if (mSearch.empty() && mFocus != Field::Search)
        g.text(L"⌕  Search all presets…", { kSearch.left + 8, kSearch.top, kSearch.right - 6, kSearch.bottom }, Font::Small, kTextFaint, Align::Left);
    else
        g.text(mSearch + (showCaret ? L"|" : L""), { kSearch.left + 8, kSearch.top, kSearch.right - 6, kSearch.bottom }, Font::Normal, kLcdText, Align::Left);

    for (const auto& b : buttonsFor(mSaveMode)) {
        const bool hov = mHoverButton == b.id;
        if (b.accent) g.verticalGradient(b.r, 4.f, hov ? kAccentBright : kAccent, kAccentDim);
        else          g.fillRound(b.r, 4.f, hov ? kSurfaceHi : kSurface);
        g.strokeRound(b.r, 4.f, hov ? kAccentBright : kTrack, 1.f);
        g.text(b.label, b.r, Font::Bold, hov ? kText : rgb(0xCFCCDA));
    }

    // Bank list
    g.fillRound(kBankArea, 5.f, kPanel);
    g.pushClip(kBankArea);
    float y = kBankArea.top + 4.f - mBankScroll;
    for (size_t i = 0; i < mRows.size(); ++i, y += kRowH) {
        if (y + kRowH < kBankArea.top || y > kBankArea.bottom) continue;
        const Row& r = mRows[i];
        const D2D1_RECT_F rr{ kBankArea.left + 4, y, kBankArea.right - 4, y + kRowH - 1 };
        if (r.header) {
            g.text(r.text, { rr.left + 4, rr.top, rr.right, rr.bottom }, Font::Title, kTitle, Align::Left);
            continue;
        }
        const bool sel = r.bank == mBank, hov = int(i) == mHoverRow;
        const bool target = mSaveMode && r.bank == mSaveBank;
        if (sel || target) g.fillRound(rr, 3.f, kRowSelected);
        else if (hov)      g.fillRound(rr, 3.f, kRowHover);
        const PresetBank& bank = *mBanks[size_t(r.bank)];
        const D2D1_COLOR_F col = mSaveMode && !bank.writable() ? kTextFaint : (sel ? kText : kTextDim);
        g.text(r.text, { rr.left + 10, rr.top, rr.right - 4, rr.bottom }, Font::Small, col, Align::Left);
    }
    g.popClip();

    if (!mSaveMode) {
        // Program grid (two columns)
        g.fillRound(kProgArea, 5.f, kPanel);
        g.pushClip(kProgArea);
        const float colW = (kProgArea.right - kProgArea.left - 8.f) / 2.f;
        const int rowsPerCol = int(std::ceil(mResults.size() / 2.0));
        for (size_t i = 0; i < mResults.size(); ++i) {
            const int col = int(i) / std::max(rowsPerCol, 1), row = int(i) % std::max(rowsPerCol, 1);
            const float yy = kProgArea.top + 4.f + row * kRowH - mProgScroll;
            if (yy + kRowH < kProgArea.top || yy > kProgArea.bottom) continue;
            const float xx = kProgArea.left + 4.f + col * colW;
            const D2D1_RECT_F rr{ xx, yy, xx + colW - 4.f, yy + kRowH - 1.f };
            const Hit& h = mResults[i];
            const PresetBank& bank = *mBanks[size_t(h.bank)];
            if (int(i) == mSelected) g.fillRound(rr, 3.f, kRowSelected);
            else if (int(i) == mHoverProg) g.fillRound(rr, 3.f, kRowHover);
            const int num = h.program < int(bank.numbers.size()) ? bank.numbers[size_t(h.program)] : h.program + 1;
            g.text(numberText(num), { rr.left + 4, rr.top, rr.left + 30, rr.bottom }, Font::Small, kLcdTextDim, Align::Left);
            std::wstring name = widen(bank.patches[size_t(h.program)].name);
            if (!mSearch.empty()) name += L"   · " + bank.name;
            g.text(name, { rr.left + 32, rr.top, rr.right - 4, rr.bottom }, Font::Small, int(i) == mSelected ? kText : rgb(0xCFCCDA), Align::Left);
        }
        if (mResults.empty()) {
            const wchar_t* msg = !mSearch.empty() ? (mCtl.library().indexing() ? L"Searching…" : L"No matches")
                                                  : L"This bank is empty";
            g.text(msg, kProgArea, Font::Normal, kTextFaint);
        }
        g.popClip();
    } else {
        // Save form
        const D2D1_RECT_F area = kProgArea;
        g.fillRound(area, 5.f, kPanel);
        const float x0 = 290.f;
        g.text(L"Name", { x0, 76.f, 460.f, 92.f }, Font::Small, kTextDim, Align::Left);
        const D2D1_RECT_F nameBox{ x0, 94.f, 670.f, 116.f };
        g.fillRound(nameBox, 4.f, kLcd);
        g.strokeRound(nameBox, 4.f, mFocus == Field::Name ? kAccentBright : kLcdBorder, 1.f);
        const bool caret = mFocus == Field::Name && (GetTickCount64() / 500) % 2 == 0;
        g.text(mSaveName + (caret ? L"|" : L""), { nameBox.left + 8, nameBox.top, nameBox.right - 6, nameBox.bottom }, Font::Normal, kLcdText, Align::Left);

        g.text(L"Target user bank", { x0, 128.f, 460.f, 144.f }, Font::Small, kTextDim, Align::Left);
        const std::wstring target = mSaveBank >= 0 ? mBanks[size_t(mSaveBank)]->name : L"(select a user bank on the left)";
        g.text(target, { x0, 146.f, 670.f, 164.f }, Font::Bold, mSaveBank >= 0 ? kText : kTextFaint, Align::Left);

        g.text(L"Program slot", { x0, 172.f, 460.f, 194.f }, Font::Small, kTextDim, Align::Left);
        const D2D1_RECT_F slotBox{ 504.f, 172.f, 636.f, 194.f };
        g.fillRound(slotBox, 4.f, kLcd);
        g.strokeRound(slotBox, 4.f, kLcdBorder, 1.f);
        g.text(numberText(mSaveSlot) + L".sy1", slotBox, Font::Bold, kLcdText);

        g.text(L"Export copies to any folder:", { x0, 278.f, 670.f, 294.f }, Font::Small, kTextDim, Align::Left);
        for (const auto& b : buttonsFor(true)) {
            if (b.id < kBtnSaveToBank) continue;
            const bool hov = mHoverButton == b.id;
            if (b.accent) g.verticalGradient(b.r, 4.f, hov ? kAccentBright : kAccent, kAccentDim);
            else          g.fillRound(b.r, 4.f, hov ? kSurfaceHi : kSurface);
            g.strokeRound(b.r, 4.f, hov ? kAccentBright : kTrack, 1.f);
            g.text(b.label, b.r, Font::Bold, hov ? kText : rgb(0xCFCCDA));
        }
    }

    // Footer
    std::wstring left = mStatus;
    if (left.empty()) {
        PresetLibrary& lib = mCtl.library();
        if (lib.indexing()) {
            wchar_t b[64];
            swprintf(b, 64, L"Indexing library… %d%%", int(lib.indexProgress() * 100.f));
            left = b;
        } else if (!mSearch.empty()) {
            left = std::to_wstring(mResults.size()) + L" matches";
        } else if (mBank >= 0) {
            left = mBanks[size_t(mBank)]->name + L"  ·  " + std::to_wstring(mResults.size()) + L" programs";
        }
    }
    g.text(left, { P.left + 10, 428.f, 470.f, 452.f }, Font::Small, kTextDim, Align::Left);
    g.text(mSaveMode ? L"Enter: save  ·  Esc: back"
                     : L"Click: preview (C3)  ·  Double-click: load  ·  Esc: close",
           { 400.f, 428.f, P.right - 10, 452.f }, Font::Small, kTextFaint, Align::Right);
}

void PresetBrowser::tick()
{
    if (!mVisible) return;
    PresetLibrary& lib = mCtl.library();
    const float p = lib.indexProgress();
    if (p != mLastIndexProgress) {
        mLastIndexProgress = p;
        if (!mSearch.empty() && (int(p * 100) % 5 == 0 || !lib.indexing())) rebuildResults();
        mInvalidate();
    }
    if (mFocus != Field::None) mInvalidate();   // caret blink
}

// ===========================================================================
// Input
// ===========================================================================
bool PresetBrowser::mouseDown(float x, float y, bool dbl)
{
    if (!mVisible) return false;
    const D2D1_RECT_F P = panelRect();
    if (!inside(P, x, y)) { if (!dbl) close(); return true; }

    for (const auto& b : buttonsFor(mSaveMode)) {
        if (!inside(b.r, x, y)) continue;
        mStatus.clear();
        switch (b.id) {
        case kBtnOpen:  openFile(); break;
        case kBtnInit:  mCtl.loadPatch(Patch::makeInit()); mCtl.library().currentBankPath.clear(); close(); break;
        case kBtnSave:  open(!mSaveMode); break;
        case kBtnClose: close(); break;
        case kBtnSaveToBank: doSaveToBank(); break;
        case kBtnExportFxp:  doExport(0); break;
        case kBtnExportSy1:  doExport(1); break;
        case kBtnExportFxb:  doExport(2); break;
        case kBtnSlotDown:   mSaveSlot = std::max(1, mSaveSlot - 1); break;
        case kBtnSlotUp:     mSaveSlot = std::min(999, mSaveSlot + 1); break;
        case kBtnNewBank: {
            int n = 1;
            std::wstring name;
            do { wchar_t b2[32]; swprintf(b2, 32, L"User Bank %02d", n++); name = b2; }
            while (std::any_of(mBanks.begin(), mBanks.end(), [&](auto& bk) { return !bk->factory && bk->name == name; }));
            auto bank = mCtl.library().createUserBank(name);
            refreshBanks();
            for (size_t i = 0; i < mBanks.size(); ++i) if (mBanks[i] == bank) selectBank(int(i));
            mStatus = L"Created " + name;
            break;
        }
        default: break;
        }
        mInvalidate();
        return true;
    }

    if (inside(kSearch, x, y)) { mFocus = Field::Search; SetFocus(mHwnd); mInvalidate(); return true; }
    if (mSaveMode && inside({ 290.f, 94.f, 670.f, 116.f }, x, y)) { mFocus = Field::Name; SetFocus(mHwnd); mInvalidate(); return true; }

    if (inside(kBankArea, x, y)) {
        const int row = int((y - kBankArea.top - 4.f + mBankScroll) / kRowH);
        if (row >= 0 && row < int(mRows.size()) && !mRows[size_t(row)].header) {
            if (!mSearch.empty()) mSearch.clear();
            selectBank(mRows[size_t(row)].bank);
        }
        mInvalidate();
        return true;
    }

    if (!mSaveMode && inside(kProgArea, x, y)) {
        const int rowsPerCol = std::max(1, int(std::ceil(mResults.size() / 2.0)));
        const float colW = (kProgArea.right - kProgArea.left - 8.f) / 2.f;
        const int col = x - kProgArea.left - 4.f >= colW ? 1 : 0;
        const int row = int((y - kProgArea.top - 4.f + mProgScroll) / kRowH);
        const int idx = col * rowsPerCol + row;
        if (row >= 0 && row < rowsPerCol && idx >= 0 && idx < int(mResults.size())) {
            const Hit& h = mResults[size_t(idx)];
            mSelected = idx;
            if (dbl) {
                loadProgram(h.bank, h.program, true);
            } else {
                mCtl.auditionPatch(mBanks[size_t(h.bank)]->patches[size_t(h.program)]);
            }
        }
        mInvalidate();
        return true;
    }
    mFocus = Field::None;
    mInvalidate();
    return true;
}

void PresetBrowser::mouseMove(float x, float y)
{
    if (!mVisible) return;
    int hb = -1, hr = -1, hp = -1;
    for (const auto& b : buttonsFor(mSaveMode)) if (inside(b.r, x, y)) hb = b.id;
    if (inside(kBankArea, x, y)) {
        const int row = int((y - kBankArea.top - 4.f + mBankScroll) / kRowH);
        if (row >= 0 && row < int(mRows.size()) && !mRows[size_t(row)].header) hr = row;
    }
    if (!mSaveMode && inside(kProgArea, x, y)) {
        const int rowsPerCol = std::max(1, int(std::ceil(mResults.size() / 2.0)));
        const float colW = (kProgArea.right - kProgArea.left - 8.f) / 2.f;
        const int col = x - kProgArea.left - 4.f >= colW ? 1 : 0;
        const int row = int((y - kProgArea.top - 4.f + mProgScroll) / kRowH);
        const int idx = col * rowsPerCol + row;
        if (row >= 0 && row < rowsPerCol && idx < int(mResults.size())) hp = idx;
    }
    if (hb != mHoverButton || hr != mHoverRow || hp != mHoverProg) {
        mHoverButton = hb; mHoverRow = hr; mHoverProg = hp;
        mInvalidate();
    }
}

void PresetBrowser::wheel(float x, float y, float delta)
{
    if (!mVisible) return;
    const float step = kRowH * 3.f * (delta > 0 ? -1.f : 1.f);
    if (inside(kBankArea, x, y)) {
        const float maxScroll = std::max(0.f, mRows.size() * kRowH + 8.f - (kBankArea.bottom - kBankArea.top));
        mBankScroll = clampScroll(mBankScroll + step, maxScroll);
    } else if (inside(kProgArea, x, y)) {
        const int rowsPerCol = int(std::ceil(mResults.size() / 2.0));
        const float maxScroll = std::max(0.f, rowsPerCol * kRowH + 8.f - (kProgArea.bottom - kProgArea.top));
        mProgScroll = clampScroll(mProgScroll + step, maxScroll);
    }
    mouseMove(x, y);
    mInvalidate();
}

bool PresetBrowser::keyChar(wchar_t ch)
{
    if (!mVisible) return false;
    if (ch < 32 || ch == 127) return true;
    if (mFocus == Field::Search) { mSearch.push_back(ch); mProgScroll = 0; rebuildResults(); }
    else if (mFocus == Field::Name && mSaveName.size() < 31) mSaveName.push_back(ch);
    mInvalidate();
    return true;
}

bool PresetBrowser::keyDown(int vk)
{
    if (!mVisible) return false;
    switch (vk) {
    case VK_ESCAPE:
        if (mFocus == Field::Search && !mSearch.empty()) { mSearch.clear(); rebuildResults(); }
        else if (mSaveMode) open(false);
        else close();
        break;
    case VK_BACK:
        if (mFocus == Field::Search && !mSearch.empty()) { mSearch.pop_back(); rebuildResults(); }
        else if (mFocus == Field::Name && !mSaveName.empty()) mSaveName.pop_back();
        break;
    case VK_RETURN:
        if (mSaveMode) doSaveToBank();
        else if (mSelected >= 0 && mSelected < int(mResults.size()))
            loadProgram(mResults[size_t(mSelected)].bank, mResults[size_t(mSelected)].program, true);
        break;
    case VK_DOWN:
    case VK_UP:
        if (!mSaveMode && !mResults.empty()) {
            mSelected = std::clamp(mSelected + (vk == VK_DOWN ? 1 : -1), 0, int(mResults.size()) - 1);
            const Hit& h = mResults[size_t(mSelected)];
            mCtl.auditionPatch(mBanks[size_t(h.bank)]->patches[size_t(h.program)]);
        }
        break;
    default:
        return mFocus != Field::None;   // swallow typing keys while editing
    }
    mInvalidate();
    return true;
}

} // namespace cs
