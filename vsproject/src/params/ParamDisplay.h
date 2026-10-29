#pragma once

#include <string>

namespace cs {

// Human-readable value text for a parameter, using the value curves recovered
// from Synth1's CSynthesizer1::setParameter (0x180028fe0) and the GUI display
// formatter (0x180034950).
std::string formatParamValue(int id, int value);

// Parses text typed by the user back into a raw value. Returns false if unknown.
bool parseParamValue(int id, const std::string& text, int& outValue);

} // namespace cs
