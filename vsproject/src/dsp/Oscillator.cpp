#include "dsp/Oscillator.h"
#include "dsp/DspCommon.h"

#include <cmath>
#include <vector>

namespace cs {

const WaveTables& WaveTables::instance()
{
    static const WaveTables* tables = new WaveTables();   // built once, never freed
    return *tables;
}

WaveTables::WaveTables()
{
    std::vector<double> buf(kSize);
    std::vector<double> sinTab(kSize);
    for (int i = 0; i < kSize; ++i)
        sinTab[i] = std::sin(kTwoPi * i / kSize);
    // sin(2*pi*k*i/N) == sinTab[(k*i) mod N] exactly, so partials are cheap.
    auto partial = [&](int k, int i) { return sinTab[(size_t(k) * size_t(i)) & (kSize - 1)]; };

    for (int i = 0; i <= kSize; ++i)
        mSine[i] = float(sinTab[i % kSize]);

    for (int level = 0; level < kLevels; ++level) {
        const int harmonics = 1024 >> level;

        // Descending saw: +1 at phase 0 falling to -1 (Synth1 >= V1.11).
        std::fill(buf.begin(), buf.end(), 0.0);
        for (int k = 1; k <= harmonics; ++k) {
            // Lanczos sigma reduces Gibbs ringing at the discontinuity.
            const double sigma = k == 1 ? 1.0 : std::sin(kPi * k / (harmonics + 1)) / (kPi * k / (harmonics + 1));
            const double a = (2.0 / kPi) * sigma / k;
            for (int i = 0; i < kSize; ++i)
                buf[i] += a * partial(k, i);
        }
        for (int i = 0; i <= kSize; ++i)
            mSaw[level][i] = float(buf[i % kSize]);

        std::fill(buf.begin(), buf.end(), 0.0);
        for (int k = 1; k <= harmonics; k += 2) {
            const double sign = ((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
            const double a = (8.0 / (kPi * kPi)) * sign / (double(k) * k);
            for (int i = 0; i < kSize; ++i)
                buf[i] += a * partial(k, i);
        }
        for (int i = 0; i <= kSize; ++i)
            mTri[level][i] = float(buf[i % kSize]);
    }
}

} // namespace cs
