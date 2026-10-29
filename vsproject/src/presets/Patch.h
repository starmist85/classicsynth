#pragma once

#include "params/ParamTable.h"

#include <array>
#include <string>

namespace cs {

// One program. Values use Synth1 patch IDs and raw ranges (see ParamTable).
struct Patch {
    std::string name = "init";
    std::string color = "default";
    int version = 113;                                // Synth1 patch version (ver=)
    std::array<int, kNumPatchParams> values{};
    std::array<bool, kNumPatchParams> present{};      // false -> use default

    static Patch makeInit();

    void set(int id, int v);
    int  get(int id) const;
    void fillMissingWithDefaults();
};

} // namespace cs
