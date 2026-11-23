// envelope.hpp - peak and RMS envelope followers with separate attack and release.
//
// Used by the envelope filter, gate, compressor-style detectors and supply sag.
// Attack and release are one-pole time constants: the time to reach 1 - 1/e
// (about 63%) of a step.
#pragma once

#include "dsp_math.hpp"

namespace gigrig {

class EnvelopeFollower {
public:
    enum class Mode { Peak, Rms };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept { state_ = 0.0; }

    void setMode(Mode mode) noexcept { mode_ = mode; }

    void setTimes(double attackSeconds, double releaseSeconds) noexcept {
        attack_ = attackSeconds;
        release_ = releaseSeconds;
        updateCoefficients();
    }

    // Returns the envelope in linear amplitude.
    float process(float input) noexcept {
        // RMS follows the squared signal so that its output is a true RMS level.
        const double x = mode_ == Mode::Peak ? std::fabs(static_cast<double>(input))
                                             : static_cast<double>(input) * static_cast<double>(input);
        const double c = x > state_ ? attackCoeff_ : releaseCoeff_;
        state_ = x + c * (state_ - x);
        return static_cast<float>(mode_ == Mode::Peak ? state_ : std::sqrt(state_));
    }

    float value() const noexcept {
        return static_cast<float>(mode_ == Mode::Peak ? state_ : std::sqrt(state_));
    }

private:
    void updateCoefficients() noexcept {
        attackCoeff_ = onePoleCoefficient(attack_, sampleRate_);
        releaseCoeff_ = onePoleCoefficient(release_, sampleRate_);
    }

    Mode mode_ = Mode::Peak;
    double sampleRate_ = 48000.0;
    double attack_ = 0.001;
    double release_ = 0.1;
    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double state_ = 0.0;
};

}  // namespace gigrig
