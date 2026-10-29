#pragma once
// Preset library: factory banks shipped in the VST3 bundle plus user banks in
// Documents\Starmist Audio\ClassicSynth\Presets.
//
//   <bundle>\Contents\Resources\Presets\...   factory (read only)
//   <Documents>\Starmist Audio\ClassicSynth\Presets\...   user
//
// A bank is any folder that directly contains .sy1/.fxp files (any depth), any
// .zip (Synth1 external bank) or any .fxb. Discovery only enumerates the file
// system; patches are parsed when a bank is opened, and a background indexer
// loads every bank so program names can be searched across the whole library.

#include "presets/Patch.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cs {

struct PresetBank {
    enum class Kind { Folder, Zip, Fxb };
    Kind kind = Kind::Folder;
    bool factory = false;
    std::wstring name;          // display name (path relative to its root)
    std::wstring path;          // folder, zip or fxb path

    // Loaded content (guarded by PresetLibrary's mutex once indexing runs).
    bool loaded = false;
    std::vector<Patch> patches;
    std::vector<int> numbers;   // program numbers (1-based)

    bool writable() const { return kind == Kind::Folder && !factory; }
};

class PresetLibrary {
public:
    ~PresetLibrary();

    void scan();                                  // discovery only (fast)
    bool scanned() const { return mScanned; }

    std::mutex& mutex() { return mLock; }
    std::vector<std::shared_ptr<PresetBank>> banks();   // snapshot of the list
    bool ensureLoaded(PresetBank& bank);          // parse on demand

    // Background indexing of all banks (for library-wide search).
    void startIndexing();
    float indexProgress() const { return mIndexTotal ? float(mIndexDone) / float(mIndexTotal) : 1.f; }
    bool  indexing() const { return mIndexing; }

    static std::wstring userRoot();               // creates the folder if needed
    static std::wstring factoryRoot();            // inside the plugin bundle

    // Saves into a writable folder bank at program number (1-based) as NNN.sy1.
    bool saveToBank(PresetBank& bank, int number, const Patch& p);
    std::shared_ptr<PresetBank> createUserBank(const std::wstring& name);

    // Remembered selection (persists while the plugin is loaded).
    std::wstring currentBankPath;
    int currentProgram = -1;

private:
    void discover(const std::wstring& root, const std::wstring& rel, bool factory, int depth);
    void stopIndexing();

    std::mutex mLock;
    std::vector<std::shared_ptr<PresetBank>> mBanks;
    bool mScanned = false;

    std::thread mIndexer;
    std::atomic<bool> mStop{ false }, mIndexing{ false };
    std::atomic<int> mIndexDone{ 0 }, mIndexTotal{ 0 };
};

std::wstring widen(const std::string& s);      // UTF-8, falls back to Shift-JIS (CP932)
std::string  narrowAnsi(const std::wstring& s); // for .sy1 names (system code page)

} // namespace cs
