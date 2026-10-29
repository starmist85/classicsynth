#include "presets/Patch.h"

namespace cs {

Patch Patch::makeInit()
{
    Patch p;
    p.name = "init";
    p.fillMissingWithDefaults();
    return p;
}

void Patch::set(int id, int v)
{
    if (id < 0 || id >= kNumPatchParams) return;
    if (const ParamDef* d = findParam(id)) {
        // MIDI source slots: Synth1 files hold status codes (>= 0xB000), the
        // engine/UI an index 0..13. Assign slots: unknown targets -> none (-1).
        if ((id == kMidiSrc1 || id == kMidiSrc2) && v > d->maxValue) v = midiSourceFromSynth1Code(v);
        if ((id == kMidiAssign1 || id == kMidiAssign2) && (v < -1 || v >= kNumPatchParams || !findParam(v))) v = -1;
        v = clampValue(*d, v);
    }
    values[size_t(id)] = v;
    present[size_t(id)] = true;
}

int Patch::get(int id) const
{
    if (id < 0 || id >= kNumPatchParams) return 0;
    if (present[size_t(id)]) return values[size_t(id)];
    const ParamDef* d = findParam(id);
    return d ? d->defValue : 0;
}

void Patch::fillMissingWithDefaults()
{
    for (int id = 0; id < kNumPatchParams; ++id) {
        if (present[size_t(id)]) continue;
        const ParamDef* d = findParam(id);
        values[size_t(id)] = d ? d->defValue : 0;
        present[size_t(id)] = true;
    }
}

} // namespace cs
