// dsp_math.hpp - small numeric helpers shared by every DSP block.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gigrig {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

// pow(10, 0) is exactly 1, so a 0 dB setting gives a bit-exact unity gain.
inline double dbToGain(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

inline double gainToDb(double gain) noexcept {
    return 20.0 * std::log10(std::max(gain, 1.0e-30));
}

// Coefficient of a one-pole lowpass that reaches 1 - 1/e of a step after
// timeSeconds. Returns 0 (no smoothing) for a non-positive time.
inline double onePoleCoefficient(double timeSeconds, double sampleRate) noexcept {
    if (!(timeSeconds > 0.0) || !(sampleRate > 0.0)) {
        return 0.0;
    }
    return std::exp(-1.0 / (timeSeconds * sampleRate));
}

}  // namespace gigrig
