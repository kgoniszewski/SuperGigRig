// lfo.hpp - low-frequency oscillator, free running or locked to a tempo.
#pragma once

#include "dsp_math.hpp"

namespace gigrig {

class Lfo {
public:
    enum class Shape { Sine, Triangle };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        updateIncrement();
    }

    void reset(double phase = 0.0) noexcept { setPhase(phase); }

    void setShape(Shape shape) noexcept { shape_ = shape; }

    void setRate(double hz) noexcept {
        rateHz_ = std::max(0.0, hz);
        updateIncrement();
    }

    // Tempo lock: one cycle every beatsPerCycle beats (0.25 is a sixteenth note).
    void setTempo(double bpm, double beatsPerCycle) noexcept {
        setRate(beatsPerCycle > 0.0 ? bpm / 60.0 / beatsPerCycle : 0.0);
    }

    // Phase in cycles, 0 to 1. Used to re-align with the beat on tap or transport start.
    void setPhase(double phase) noexcept { phase_ = phase - std::floor(phase); }

    // Output in [-1, 1]; both shapes start at 0 and rise at phase 0.
    float next() noexcept {
        const double p = phase_;
        phase_ += increment_;
        if (phase_ >= 1.0) {
            phase_ -= std::floor(phase_);
        }
        if (shape_ == Shape::Sine) {
            return static_cast<float>(std::sin(kTwoPi * p));
        }
        // Triangle: 0 -> 1 -> -1 -> 0 over one cycle.
        const double t = p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        return static_cast<float>(t);
    }

    double phase() const noexcept { return phase_; }
    double rate() const noexcept { return rateHz_; }

private:
    void updateIncrement() noexcept { increment_ = sampleRate_ > 0.0 ? rateHz_ / sampleRate_ : 0.0; }

    Shape shape_ = Shape::Sine;
    double sampleRate_ = 48000.0;
    double rateHz_ = 1.0;
    double increment_ = 0.0;
    double phase_ = 0.0;
};

}  // namespace gigrig
