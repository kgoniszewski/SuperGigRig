// amp_common.hpp - helpers shared by the amp models.
#pragma once

#include <algorithm>
#include <cmath>

namespace gigrig::amp {

// Oversampling factor for an amp at a session rate: the internal rate stays at or above
// 176.4 kHz (plan section 5): 4x at 44.1 and 48 kHz, 2x at 88.2 and 96 kHz, none above.
inline int ampOversamplingFactor(double sampleRate) noexcept {
    constexpr double minimumInternalRate = 176400.0;
    if (sampleRate >= minimumInternalRate) {
        return 1;
    }
    return 2.0 * sampleRate >= minimumInternalRate ? 2 : 4;
}

// Knob rotation (0 to 10) to the fraction of an audio-taper pot: an exponential law
// through 0, 10% at mid travel and 100% at full, (81^x - 1) / 80.
inline double audioTaperPot(double knob) noexcept {
    const double x = std::clamp(knob / 10.0, 0.0, 1.0);
    return (std::pow(81.0, x) - 1.0) / 80.0;
}

// Knob rotation (0 to 10) to the fraction of a linear pot.
inline double linearPot(double knob) noexcept {
    return std::clamp(knob / 10.0, 0.0, 1.0);
}

// Wiper contact resistance; keeps tone networks solvable with a pot at either end.
inline constexpr double kMinPotOhms = 50.0;

// Splits a pot of total resistance at a wiper fraction (0 = wiper at the ground end).
inline void splitPot(double total, double fraction, double& upper, double& lower) noexcept {
    lower = std::clamp(fraction * total, kMinPotOhms, total - kMinPotOhms);
    upper = total - lower;
}

}  // namespace gigrig::amp
