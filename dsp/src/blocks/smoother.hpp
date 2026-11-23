// smoother.hpp - parameter smoothers.
//
// Parameters arrive once per block from the atomic parameter array. Smoothing
// them inside the block removes zipper noise. Both smoothers advance per
// sample with a fixed time constant, so the output does not depend on how the
// host splits audio into blocks.
#pragma once

#include "dsp_math.hpp"

namespace gigrig {

// Linear ramp to the target over a fixed time. Used for gains, where a
// straight line in amplitude sounds smooth and finishes at an exact value.
class LinearSmoother {
public:
    // Not real-time safe only in the sense that it should be called from prepare.
    void prepare(double sampleRate, double rampSeconds) noexcept {
        rampSamples_ = std::max(1, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        snap(target_);
    }

    void snap(float value) noexcept {
        current_ = value;
        target_ = value;
        step_ = 0.0f;
        remaining_ = 0;
    }

    void setTarget(float value) noexcept {
        if (value == target_) {
            return;
        }
        target_ = value;
        remaining_ = rampSamples_;
        step_ = (target_ - current_) / static_cast<float>(rampSamples_);
    }

    float next() noexcept {
        if (remaining_ > 0) {
            --remaining_;
            // The last step lands exactly on the target, so no rounding drift remains.
            current_ = remaining_ == 0 ? target_ : current_ + step_;
        }
        return current_;
    }

    bool isSmoothing() const noexcept { return remaining_ > 0; }
    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int remaining_ = 0;
    int rampSamples_ = 1;
};

// Exponential approach to the target. Used for filter cutoffs and other
// values that are heard on a logarithmic scale. The state is double: in float
// the last steps of a slow glide fall below half an ulp and the value stalls
// just short of the target.
class OnePoleSmoother {
public:
    void prepare(double sampleRate, double timeSeconds) noexcept {
        coefficient_ = onePoleCoefficient(timeSeconds, sampleRate);
    }

    void snap(float value) noexcept {
        current_ = static_cast<double>(value);
        target_ = current_;
    }

    void setTarget(float value) noexcept { target_ = static_cast<double>(value); }

    float next() noexcept {
        current_ = target_ + coefficient_ * (current_ - target_);
        // Settle exactly once the difference is inaudible, so steady state is exact.
        if (std::fabs(current_ - target_) <= 1.0e-7 * std::max(1.0, std::fabs(target_))) {
            current_ = target_;
        }
        return static_cast<float>(current_);
    }

    bool isSmoothing() const noexcept { return current_ != target_; }
    float current() const noexcept { return static_cast<float>(current_); }
    float target() const noexcept { return static_cast<float>(target_); }

private:
    double current_ = 0.0;
    double target_ = 0.0;
    double coefficient_ = 0.0;
};

}  // namespace gigrig
