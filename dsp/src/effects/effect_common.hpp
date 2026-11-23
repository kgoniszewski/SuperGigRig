// effect_common.hpp - small pieces every effect block shares: the bypass fade,
// the control-rate clock for coefficient updates, the oversampling choice and
// knob mappings.
#pragma once

#include <algorithm>
#include <cstdint>

#include "blocks/dsp_math.hpp"
#include "blocks/smoother.hpp"

namespace gigrig {

// Nonlinear pedals run at about 192 kHz whatever the session rate: 4x at 44.1
// and 48 kHz, 2x at 88.2 and 96 kHz. At 96 kHz the base rate already gives the
// headroom that 4x gives at 48 kHz, so 2x keeps the same aliasing margin for
// half the CPU on the iPad, which runs at 96 kHz (docs/latency.md).
inline int effectOversamplingFactor(double sampleRate) noexcept {
    return sampleRate < 70000.0 ? 4 : 2;
}

// Filter coefficients that follow a smoothed knob are recomputed every
// kControlInterval samples on a grid counted from prepare/reset, not per host
// block, so the output does not depend on how audio is split into blocks.
class ControlClock {
public:
    static constexpr int kInterval = 16;

    void reset() noexcept { phase_ = 0; }

    // Call once per sample; true on the first sample of each control period.
    bool tick() noexcept {
        const bool due = phase_ == 0;
        phase_ = phase_ + 1 == kInterval ? 0 : phase_ + 1;
        return due;
    }

private:
    int phase_ = 0;
};

// Click-free on and off: a 10 ms linear crossfade between the dry signal and
// the effect. When it reaches 0 the effect stops running (no CPU), and the
// output is the input exactly, so a chain of bypassed pedals stays bit exact.
class BypassFade {
public:
    static constexpr double kFadeSeconds = 0.01;

    void prepare(double sampleRate) noexcept { mix_.prepare(sampleRate, kFadeSeconds); }

    // Jumps to the switch position without a fade (prepare, reset).
    void snap(bool on) noexcept { mix_.snap(on ? 1.0f : 0.0f); }

    // Per block. Returns true when the effect was fully off and is now turning
    // on: the caller clears its state so it starts the same way every time.
    bool setOn(bool on) noexcept {
        const bool wasIdle = isIdle();
        mix_.setTarget(on ? 1.0f : 0.0f);
        return wasIdle && on;
    }

    // Fully off and staying off: the block can be skipped.
    bool isIdle() const noexcept { return mix_.current() == 0.0f && mix_.target() == 0.0f; }

    // Fully on: no crossfade needed this sample.
    float next() noexcept { return mix_.next(); }

    static float apply(float dry, float wet, float mix) noexcept {
        // At mix 0 the result is dry exactly; at mix 1 it is wet exactly.
        return mix == 1.0f ? wet : dry + mix * (wet - dry);
    }

private:
    LinearSmoother mix_;
};

// Pedal knobs that read 0 to 10, as printed on the pedals.
inline double knobFraction(float knob) noexcept {
    return std::clamp(static_cast<double>(knob) / 10.0, 0.0, 1.0);
}

// Exponential sweep from lo at 0 to hi at 1, for cutoffs and resonance.
inline double expSweep(double lo, double hi, double t) noexcept {
    return lo * std::pow(hi / lo, std::clamp(t, 0.0, 1.0));
}

// Runs an effect in place behind a BypassFade. computeWet(in, wet, n) writes
// the fully-on effect output; resetState() clears the effect when it turns on
// from fully off. Lambdas only: no std::function on the audio path.
template <class Reset, class Wet>
inline void processWithBypass(BypassFade& fade, bool on, float* io, float* wet, int n, Reset&& resetState,
                              Wet&& computeWet) noexcept {
    if (fade.setOn(on)) {
        resetState();
    }
    if (fade.isIdle()) {
        return;
    }
    computeWet(io, wet, n);
    for (int i = 0; i < n; ++i) {
        io[i] = BypassFade::apply(io[i], wet[i], fade.next());
    }
}

}  // namespace gigrig
