// env_filter.hpp - Env Filter, the envelope-controlled filter of the Pre FX
// section (plan section 4, row 2).
//
// A resonant low-pass state-variable filter whose cutoff follows the pick
// attack, as in the classic optical envelope filters of the 1970s: Gain sets
// how strongly the playing level moves the filter, Peak sets the resonance,
// and three switches choose the sweep direction (Up or Down), the release of
// the envelope (Fast or Slow) and the sweep range (Lo or Hi). The filter is
// linear, so it needs no oversampling.
#pragma once

#include <vector>

#include "blocks/envelope.hpp"
#include "blocks/svf.hpp"
#include "effect_common.hpp"

namespace gigrig {

class EnvFilter {
public:
    enum Direction { Up = 0, Down = 1 };
    enum Speed { Fast = 0, Slow = 1 };
    enum Range { Lo = 0, Hi = 1 };

    struct Settings {
        bool on = false;
        float gain = 5.0f;  // sensitivity, 0 to 10
        float peak = 5.0f;  // resonance, 0 to 10
        int direction = Up;
        int speed = Fast;
        int range = Lo;
    };

    // Sweep ends in Hz for the two ranges.
    static constexpr double kLoRange[2] = {200.0, 1500.0};
    static constexpr double kHiRange[2] = {400.0, 3200.0};
    static constexpr double kAttackSeconds = 0.003;
    static constexpr double kFastReleaseSeconds = 0.06;
    static constexpr double kSlowReleaseSeconds = 0.3;

    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Sweep position (0 = bottom of the range, 1 = top) for an envelope level,
    // before direction. Exposed for tests.
    static double sweepPosition(double envelope, float gainKnob) noexcept;
    // Resonance for a Peak knob position.
    static double resonance(float peakKnob) noexcept;

    double currentCutoff() const noexcept { return svf_.cutoff(); }

private:
    void resetState() noexcept;
    void computeWet(const float* in, float* wet, int n) noexcept;

    Settings settings_;
    BypassFade fade_;
    EnvelopeFollower env_;
    StateVariableFilter svf_;
    LinearSmoother sensitivity_;  // knob position, smoothed
    OnePoleSmoother q_;
    std::vector<float> wet_;
};

}  // namespace gigrig
