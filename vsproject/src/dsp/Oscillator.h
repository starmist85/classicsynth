#pragma once
// Band-limited wavetable oscillator core.
//
// Follows the design Daichi describes for Synth1 (synthprog article): 2048-point
// float tables, several tables per frequency range so no partial exceeds
// Nyquist, 32-bit fixed-point phase (11-bit index + 21-bit fraction), pulse
// built from the difference of two phase-offset saws, M-sequence noise.

#include <cstdint>

namespace cs {

class WaveTables {
public:
    static constexpr int kSizeBits = 11;
    static constexpr int kSize     = 1 << kSizeBits;   // 2048
    static constexpr int kLevels   = 11;               // 1024 .. 1 harmonics

    static const WaveTables& instance();

    // Picks the table level whose harmonic count stays below Nyquist.
    static inline int levelFor(uint32_t phaseInc)
    {
        // harmonics allowed = 0.5 / (inc / 2^32) = 2^31 / inc
        int level = 0;
        uint32_t limit = 0x200000u;      // inc at which 1024 harmonics hit Nyquist (2^31/1024)
        while (level < kLevels - 1 && phaseInc > limit) { limit <<= 1; ++level; }
        return level;
    }

    static inline float read(const float* t, uint32_t phase)
    {
        const uint32_t i = phase >> (32 - kSizeBits);
        const float f = float(phase & ((1u << (32 - kSizeBits)) - 1)) * (1.0f / float(1u << (32 - kSizeBits)));
        const float a = t[i];
        return a + (t[i + 1] - a) * f;
    }

    const float* saw(int level) const  { return mSaw[level]; }
    const float* tri(int level) const  { return mTri[level]; }
    const float* sine() const          { return mSine; }

private:
    WaveTables();
    float mSaw[kLevels][kSize + 1];
    float mTri[kLevels][kSize + 1];
    float mSine[kSize + 1];
};

// 23-bit maximal-length LFSR noise (M-sequence), as used by Synth1.
struct MSequenceNoise {
    uint32_t reg = 0x7FFFFFu;
    inline float next()
    {
        const uint32_t bit = ((reg >> 22) ^ (reg >> 17)) & 1u;
        reg = ((reg << 1) | bit) & 0x7FFFFFu;
        return float(int32_t(reg) - 0x400000) * (1.0f / 4194304.0f);
    }
};

enum Osc1Wave { kWaveSine = 0, kWaveTri = 1, kWaveSaw = 2, kWavePulse = 3 };
enum Osc2Wave { kO2Tri = 1, kO2Saw = 2, kO2Pulse = 3, kO2Noise = 4 };

inline uint32_t hzToPhaseInc(double hz, double fs)
{
    double inc = hz / fs;
    if (inc < 0) inc = 0;
    if (inc > 0.499) inc = 0.499;
    return uint32_t(inc * 4294967296.0);
}

// Renders one sample of a band-limited waveform.
//   wave: 0 sine, 1 tri, 2 saw, 3 pulse/square  (pulse duty in 0..1 as 32-bit phase offset)
inline float renderWave(int wave, uint32_t phase, uint32_t inc, uint32_t dutyOffset)
{
    const WaveTables& wt = WaveTables::instance();
    switch (wave) {
    case kWaveSine:
        return WaveTables::read(wt.sine(), phase);
    case kWaveTri:
        return WaveTables::read(wt.tri(WaveTables::levelFor(inc)), phase);
    case kWaveSaw:
        return WaveTables::read(wt.saw(WaveTables::levelFor(inc)), phase);
    default: {
        // Pulse = saw(p) - saw(p + d) minus its DC term (2d - 1).
        const float* t = wt.saw(WaveTables::levelFor(inc));
        const float d = float(dutyOffset) * (1.0f / 4294967296.0f);
        return WaveTables::read(t, phase) - WaveTables::read(t, phase + dutyOffset) - (2.f * d - 1.f);
    }
    }
}

} // namespace cs
