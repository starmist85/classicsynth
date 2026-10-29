#include "presets/PresetLibrary.h"
#include "presets/PresetFormats.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cs {

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    // Synth1 patch names are ANSI (often Shift-JIS); accept UTF-8 first.
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, flags, s.data(), int(s.size()), nullptr, 0);
    if (n <= 0) { cp = 932; flags = 0; n = MultiByteToWideChar(cp, flags, s.data(), int(s.size()), nullptr, 0); }
    if (n <= 0) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, s.data(), int(s.size()), nullptr, 0); }
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(cp, flags, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string narrowAnsi(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_ACP, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n, 0)), '\0');
    if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring stem(const std::wstring& file)
{
    const size_t dot = file.rfind(L'.');
    return dot == std::wstring::npos ? file : file.substr(0, dot);
}

static bool hasExt(const std::wstring& name, const wchar_t* ext)
{
    const size_t n = wcslen(ext);
    return name.size() > n && _wcsicmp(name.c_str() + name.size() - n, ext) == 0;
}

// "Ami Evan\Ami Evan Bank" -> "Ami Evan › Bank"; "X\X" -> "X". Archives often
// wrap a folder of the same name, which would otherwise repeat in the list.
static std::wstring prettyBankName(const std::wstring& rel)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= rel.size()) {
        const size_t sep = rel.find(L'\\', start);
        const std::wstring part = rel.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start);
        if (!part.empty()) parts.push_back(part);
        if (sep == std::wstring::npos) break;
        start = sep + 1;
    }
    std::wstring out;
    std::wstring prev;
    for (auto p : parts) {
        if (!prev.empty()) {
            if (_wcsicmp(p.c_str(), prev.c_str()) == 0) continue;             // exact repeat
            if (p.size() > prev.size() && _wcsnicmp(p.c_str(), prev.c_str(), prev.size()) == 0) {
                std::wstring rest = p.substr(prev.size());                     // "Ami Evan Bank" -> "Bank"
                while (!rest.empty() && (rest[0] == L' ' || rest[0] == L'_' || rest[0] == L'-')) rest.erase(0, 1);
                if (!rest.empty()) p = rest;
            }
        }
        if (!out.empty()) out += L" › ";
        out += p;
        prev = p;
    }
    return out.empty() ? rel : out;
}

PresetLibrary::~PresetLibrary() { stopIndexing(); }

std::wstring PresetLibrary::userRoot()
{
    PWSTR docs = nullptr;
    std::wstring root = L"C:";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs))) {
        root = docs;
        CoTaskMemFree(docs);
    }
    root += L"\\Starmist Audio\\ClassicSynth\\Presets";
    SHCreateDirectoryExW(nullptr, root.c_str(), nullptr);
    return root;
}

std::wstring PresetLibrary::factoryRoot()
{
    // <bundle>\Contents\x86_64-win\ClassicSynth.vst3 -> <bundle>\Contents\Resources\Presets
    wchar_t buf[MAX_PATH * 2];
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), buf, DWORD(std::size(buf)));
    std::wstring p(buf);
    for (int i = 0; i < 2; ++i) {
        const size_t s = p.find_last_of(L"\\/");
        if (s == std::wstring::npos) break;
        p.resize(s);
    }
    return p + L"\\Resources\\Presets";
}

void PresetLibrary::discover(const std::wstring& root, const std::wstring& rel, bool factory, int depth)
{
    if (depth > 8) return;
    const std::wstring dir = rel.empty() ? root : root + L"\\" + rel;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> subdirs, zips, fxbs;
    bool hasPrograms = false;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (n == L"__MACOSX" || n.rfind(L"._", 0) == 0) continue;   // macOS archive junk
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { subdirs.push_back(n); continue; }
        if (hasExt(n, L".sy1") || hasExt(n, L".fxp")) hasPrograms = true;
        else if (hasExt(n, L".zip")) zips.push_back(n);
        else if (hasExt(n, L".fxb")) fxbs.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    auto add = [&](PresetBank::Kind kind, const std::wstring& name, const std::wstring& path) {
        auto b = std::make_shared<PresetBank>();
        b->kind = kind;
        b->factory = factory;
        b->name = prettyBankName(name);
        b->path = path;
        mBanks.push_back(b);
    };

    if (hasPrograms) add(PresetBank::Kind::Folder, rel.empty() ? std::wstring(factory ? L"Factory" : L"User") : rel, dir);
    std::sort(zips.begin(), zips.end());
    for (auto& z : zips) add(PresetBank::Kind::Zip, (rel.empty() ? L"" : rel + L"\\") + stem(z), dir + L"\\" + z);
    std::sort(fxbs.begin(), fxbs.end());
    for (auto& f : fxbs) add(PresetBank::Kind::Fxb, (rel.empty() ? L"" : rel + L"\\") + stem(f) + L" (.fxb)", dir + L"\\" + f);

    if (depth < 0) return;   // caller asked for this folder only
    std::sort(subdirs.begin(), subdirs.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    for (auto& s : subdirs) discover(root, rel.empty() ? s : rel + L"\\" + s, factory, depth + 1);
}

void PresetLibrary::scan()
{
    stopIndexing();
    std::lock_guard<std::mutex> lock(mLock);
    mBanks.clear();
    // Each top-level folder of the factory root (e.g. "Library", "Zip Banks")
    // is its own root, so bank names do not carry that container name.
    const std::wstring fac = factoryRoot();
    discover(fac, L"", true, -1);  // files directly in the root only (depth -1 = no recursion)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((fac + L"\\*").c_str(), &fd);
        std::vector<std::wstring> containers;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring n = fd.cFileName;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L"..") containers.push_back(n);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(containers.begin(), containers.end());
        for (auto& c : containers) discover(fac + L"\\" + c, L"", true, 0);
    }

    const std::wstring user = userRoot();
    const size_t before = mBanks.size();
    discover(user, L"", false, 0);
    if (mBanks.size() == before) {
        // First run: create an empty writable bank.
        CreateDirectoryW((user + L"\\User Bank 01").c_str(), nullptr);
        auto b = std::make_shared<PresetBank>();
        b->kind = PresetBank::Kind::Folder;
        b->name = L"User Bank 01";
        b->path = user + L"\\User Bank 01";
        b->loaded = true;
        mBanks.push_back(b);
    }
    mScanned = true;
}

std::vector<std::shared_ptr<PresetBank>> PresetLibrary::banks()
{
    std::lock_guard<std::mutex> lock(mLock);
    return mBanks;
}

static void loadFolder(PresetBank& b)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((b.path + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    struct Item { int number; Patch patch; };
    std::vector<Item> items;
    int seq = 1000;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring file = fd.cFileName;
        if (file.rfind(L"._", 0) == 0) continue;   // macOS resource forks
        const std::wstring full = b.path + L"\\" + file;
        std::vector<uint8_t> bytes;
        Patch p;
        bool ok = false;
        // Incompatible files (other plugins, corrupt data) are skipped.
        if (hasExt(file, L".sy1"))      ok = readFileBytes(full, bytes) && parseSy1(std::string(bytes.begin(), bytes.end()), p);
        else if (hasExt(file, L".fxp")) ok = readFileBytes(full, bytes) && parseFxp(bytes, p);
        if (!ok) continue;
        if (p.name.empty()) p.name = narrowAnsi(stem(file));
        int number = _wtoi(file.c_str());
        if (number <= 0) number = ++seq;
        items.push_back({ number, std::move(p) });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& c) { return a.number < c.number; });
    for (auto& it : items) { b.numbers.push_back(it.number); b.patches.push_back(std::move(it.patch)); }
}

static void loadZip(PresetBank& b)
{
    std::vector<ZipBankFolder> folders;
    if (!readZipBank(b.path, folders)) return;
    const bool multi = folders.size() > 1;
    int seq = 0;
    for (auto& f : folders) {
        std::string sub = f.folder;
        const size_t s = sub.find_last_of('/');
        if (s != std::string::npos) sub = sub.substr(s + 1);
        for (size_t i = 0; i < f.patches.size(); ++i) {
            Patch p = std::move(f.patches[i]);
            if (multi && !sub.empty()) p.name = sub + " / " + p.name;
            b.patches.push_back(std::move(p));
            b.numbers.push_back(multi ? ++seq : f.programNumbers[i]);
        }
    }
}

static void loadFxb(PresetBank& b)
{
    std::vector<uint8_t> bytes;
    if (!readFileBytes(b.path, bytes) || !parseFxb(bytes, b.patches)) return;
    for (size_t i = 0; i < b.patches.size(); ++i) b.numbers.push_back(int(i) + 1);
}

bool PresetLibrary::ensureLoaded(PresetBank& bank)
{
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (bank.loaded) return true;
    }
    PresetBank tmp;
    tmp.kind = bank.kind;
    tmp.path = bank.path;
    switch (bank.kind) {
    case PresetBank::Kind::Folder: loadFolder(tmp); break;
    case PresetBank::Kind::Zip:    loadZip(tmp); break;
    case PresetBank::Kind::Fxb:    loadFxb(tmp); break;
    }
    std::lock_guard<std::mutex> lock(mLock);
    if (!bank.loaded) {
        bank.patches = std::move(tmp.patches);
        bank.numbers = std::move(tmp.numbers);
        bank.loaded = true;
    }
    return true;
}

void PresetLibrary::startIndexing()
{
    if (mIndexing) return;
    stopIndexing();
    auto list = banks();
    mIndexTotal = int(list.size());
    mIndexDone = 0;
    mStop = false;
    mIndexing = true;
    mIndexer = std::thread([this, list] {
        for (auto& b : list) {
            if (mStop) break;
            ensureLoaded(*b);
            ++mIndexDone;
        }
        mIndexing = false;
    });
}

void PresetLibrary::stopIndexing()
{
    mStop = true;
    if (mIndexer.joinable()) mIndexer.join();
    mIndexing = false;
}

bool PresetLibrary::saveToBank(PresetBank& b, int number, const Patch& p)
{
    if (!b.writable()) return false;
    ensureLoaded(b);
    wchar_t file[32];
    swprintf(file, 32, L"\\%03d.sy1", number);
    const std::string text = writeSy1(p);
    if (!writeFileBytes(b.path + file, std::vector<uint8_t>(text.begin(), text.end()))) return false;

    std::lock_guard<std::mutex> lock(mLock);
    auto it = std::find(b.numbers.begin(), b.numbers.end(), number);
    if (it != b.numbers.end()) b.patches[size_t(it - b.numbers.begin())] = p;
    else {
        size_t pos = 0;
        while (pos < b.numbers.size() && b.numbers[pos] < number) ++pos;
        b.numbers.insert(b.numbers.begin() + ptrdiff_t(pos), number);
        b.patches.insert(b.patches.begin() + ptrdiff_t(pos), p);
    }
    return true;
}

std::shared_ptr<PresetBank> PresetLibrary::createUserBank(const std::wstring& name)
{
    const std::wstring path = userRoot() + L"\\" + name;
    CreateDirectoryW(path.c_str(), nullptr);
    std::lock_guard<std::mutex> lock(mLock);
    for (auto& b : mBanks) if (b->path == path) return b;
    auto b = std::make_shared<PresetBank>();
    b->kind = PresetBank::Kind::Folder;
    b->name = name;
    b->path = path;
    b->loaded = true;
    mBanks.push_back(b);
    return b;
}

} // namespace cs
