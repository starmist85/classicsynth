#pragma once
// Shared DSP helpers and the value curves recovered from Synth1 V1.13.
// Every curve below cites the function it was recovered from (x64 build).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

template <typename T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float fastTanh(float x)
{
    // Pade approximation, accurate to ~1e-3 in [-3, 3], hard limited outside.
    if (x < -3.f) return -1.f;
    if (x >  3.f) return  1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline double noteToHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

// ---------------------------------------------------------------------------
// Recovered value curves (raw patch value 0..127 -> engine units)
// ---------------------------------------------------------------------------
namespace curves {

inline double norm(int v) { return v * (1.0 / 127.0); }

// setParameter cases 0x0c/0x0f/0x19: 56000^x * 0.0005 s  (0.5 ms .. 28 s)
inline double attackSeconds(int v) { return std::pow(56000.0, norm(v)) * 0.0005; }
// cases 0x0d/0x10/0x12/0x1a/0x1c: 6000^x * 0.01 s  (10 ms .. 60 s)
inline double decaySeconds(int v)  { return std::pow(6000.0, norm(v)) * 0.01; }
// cases 0x1b/0x1d (amp sustain / gain): 2048^(x^0.3) / 2048
inline double ampLevel(int v)
{
    if (v <= 0) return 0.0;
    return std::pow(2048.0, std::pow(norm(v), 0.3)) / 2048.0;
}
// case 0x02: semitones = (v*120)/127 - 60
inline int osc2Semitones(int v) { return (v * 120) / 127 - 60; }
// cases 0x03/0x48: dead zone 62..66 -> 0, otherwise +/- cents
inline double fineCents(int v)
{
    if (v < 62) return v - 62;
    if (v < 67) return 0;
    return v - 66;
}
inline double pulseDuty(int v)   { return 0.02 + 0.96 * norm(v); }
// case 0x5f: sub gain = v*4/127 (mixed as g/(g+1), auto level compensated)
inline double subGain(int v)     { return v * 4.0 / 127.0; }

// CVcf: cutoff is an index 0..1024 into a 1024-entry exponential table that
// starts at 15 Hz (FUN_1800381f0 builds table[i] = 15 * b^(i/1024)).
inline double cutoffHz(double index) { return 15.0 * std::pow(1400.0, clampv(index, 0.0, 1024.0) / 1024.0); }
inline double cutoffIndexPerOctave() { return 1024.0 / std::log2(1400.0); }

// case 0x3d: 50 * 320^x Hz ; case 0x3e: (v-64)/127*50 dB ; case 0x3f: 10^x
inline double eqFreqHz(int v)  { return 50.0 * std::pow(320.0, norm(v)); }
inline double eqGainDb(int v)  { return (v - 64) * (1.0 / 127.0) * 50.0; }
inline double eqQ(int v)       { return std::pow(10.0, norm(v)); }
// FUN_18002a5c0 / FUN_180028b10: below centre high-cut, above centre low-cut.
inline double toneHz(int v, bool& isHighPass)
{
    if (v <= 64) { isHighPass = false; return 200.0 * std::pow(110.0, v / 64.0); }
    isHighPass = true;
    return 10.0 * std::pow(800.0, (v - 64) / 64.0);
}
// FUN_180025ae0: L/R spread grows as 1000^|x|.
inline double delaySpreadSeconds(int v)
{
    if (v == 64) return 0.0;
    const double x = clampv(std::fabs((v - 64) / 63.0), 0.0, 1.0);
    const double s = (std::pow(1000.0, x) - 1.0) * 0.0001;
    return v < 64 ? -s : s;
}
// case 0x34: x<0.5 -> 0.05 * 300^((2x)^0.6) ms, else 30x ms
inline double chorusTimeSeconds(int v)
{
    const double x = norm(v);
    const double ms = x < 0.5 ? 0.05 * std::pow(300.0, std::pow(2.0 * x, 0.6)) : 30.0 * x;
    return ms * 0.001;
}
// case 0x35: (100^x - 1) / 99
inline double chorusDepth(int v)  { return (std::pow(100.0, norm(v)) - 1.0) / 99.0; }
// case 0x36: v==0 -> 0.01 Hz, else 20000^x * 0.02 Hz
inline double chorusRateHz(int v) { return v == 0 ? 0.01 : std::pow(20000.0, norm(v)) * 0.02; }
// case 0x37: (v-64)/64 * 0.99
inline double chorusFeedback(int v) { return (v - 64) / 64.0 * 0.99; }
// CLFO FUN_180015210: free speed = 1500^x / 12 Hz
inline double lfoFreeHz(int v)    { return std::pow(1500.0, norm(v)) / 12.0; }
// CLFO FUN_180015080: pitch depth curve (10^x - 1)/9, others linear
inline double lfoPitchDepth(int v) { return (std::pow(10.0, norm(v)) - 1.0) / 9.0; }

// Tempo tables in ticks, 24 ticks per quarter note.
// LFO sync (.rdata 0x180073cf0), index = (v*30)>>7, 16 bars .. 1/32 triplet
inline constexpr int kLfoSyncTicks[30] = {
    1536, 1152, 864, 768, 576, 432, 384, 288, 216, 192, 144, 96, 84, 72, 48,
    42, 36, 32, 24, 21, 18, 16, 12, 9, 8, 6, 4, 3, 2, 1 };
// Delay time (.data 0x180093a90), index = param 35 (0..19). 0 = 0.1 ms.
inline constexpr int kDelayTicks[20] = {
    0, 1, 2, 3, 4, 6, 8, 9, 12, 16, 18, 21, 24, 32, 36, 42, 48, 72, 84, 96 };
// Arpeggiator beat, index = param 33 (0..18): whole note .. 1/32 triplet.
inline constexpr int kArpTicks[19] = {
    96, 84, 72, 48, 42, 36, 32, 24, 21, 18, 16, 12, 9, 8, 6, 4, 3, 2, 1 };

inline double ticksToSeconds(double ticks, double bpm) { return ticks * 60.0 / (std::max(bpm, 1.0) * 24.0); }

} // namespace curves

// ---------------------------------------------------------------------------
// Small building blocks
// ---------------------------------------------------------------------------

struct Rng {
    uint32_t s = 0x12345678u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float bipolar() { return (int32_t(next()) >> 8) * (1.0f / 8388608.0f); }
    inline float unipolar() { return (next() >> 8) * (1.0f / 16777216.0f); }
};

struct OnePole {
    float a = 0.f, z = 0.f;
    void setCutoff(double hz, double fs) { a = float(1.0 - std::exp(-kTwoPi * hz / fs)); }
    inline float lp(float x) { z += a * (x - z); return z; }
    inline float hp(float x) { return x - lp(x); }
    void reset() { z = 0.f; }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    inline float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void setLowPass(double f, double q, double fs)  { rbj(0, f, q, 0, fs); }
    void setHighPass(double f, double q, double fs) { rbj(1, f, q, 0, fs); }
    void setPeak(double f, double q, double db, double fs) { rbj(2, f, q, db, fs); }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
private:
    void rbj(int type, double f, double q, double db, double fs)
    {
        f = clampv(f, 10.0, fs * 0.49);
        const double w = kTwoPi * f / fs, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / (2.0 * std::max(q, 0.05));
        double B0, B1, B2, A0, A1, A2;
        if (type == 0) { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; }
        else if (type == 1) { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; }
        else {
            const double A = std::pow(10.0, db / 40.0);
            B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
            A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
        }
        b0 = float(B0 / A0); b1 = float(B1 / A0); b2 = float(B2 / A0);
        a1 = float(A1 / A0); a2 = float(A2 / A0);
    }
};

// First-order bilinear low/high-pass exactly as CEq types 1/2 (FUN_18000d4a0):
//   wc = 2*pi*f, k = 2*fs:  LP b0 = b1 = wc/(wc+k);  HP b0 = k/(wc+k), b1 = -b0;  a1 = (k-wc)/(wc+k)
struct FirstOrder {
    float b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
    void setLowPass(double f, double fs)
    {
        const double wc = kTwoPi * f, k = 2.0 * fs, n = 1.0 / (wc + k);
        b0 = b1 = float(wc * n); a1 = float((k - wc) * n);
    }
    void setHighPass(double f, double fs)
    {
        const double wc = kTwoPi * f, k = 2.0 * fs, n = 1.0 / (wc + k);
        b0 = float(k * n); b1 = -b0; a1 = float((k - wc) * n);
    }
    inline float process(float x) { const float y = b0 * x + b1 * x1 + a1 * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

struct DcBlocker {
    float x1 = 0, y1 = 0, r = 0.995f;
    void setup(double fs) { r = float(1.0 - (kTwoPi * 10.0 / fs)); }
    inline float process(float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

// Linear parameter smoother (per-block target, per-sample ramp).
struct Smoothed {
    float cur = 0, target = 0, step = 0;
    int remaining = 0;
    void snap(float v) { cur = target = v; remaining = 0; step = 0; }
    void setTarget(float v, int samples)
    {
        target = v;
        if (samples <= 0) { snap(v); return; }
        remaining = samples;
        step = (target - cur) / samples;
    }
    inline float next()
    {
        if (remaining > 0) { cur += step; if (--remaining == 0) cur = target; }
        return cur;
    }
};

} // namespace cs
