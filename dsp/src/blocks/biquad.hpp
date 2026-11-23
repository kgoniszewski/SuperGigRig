// biquad.hpp - second- and first-order IIR filters.
//
// Coefficients follow the widely used bilinear-transform "cookbook" designs
// with frequency prewarping, so the cutoff lands exactly where it is asked
// for at every sample rate. The filter runs in transposed direct form II with
// double precision state: low cutoffs at 4x oversampled rates put the poles
// very close to the unit circle, where float state adds audible noise.
#pragma once

#include <complex>

#include "dsp_math.hpp"

namespace gigrig {

// Normalised so that a0 == 1.
struct BiquadCoefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    // Complex response at frequencyHz. Used by tests and later by tone-stack checks.
    std::complex<double> response(double frequencyHz, double sampleRate) const noexcept {
        const double w = kTwoPi * frequencyHz / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    double magnitude(double frequencyHz, double sampleRate) const noexcept {
        return std::abs(response(frequencyHz, sampleRate));
    }

    double magnitudeDb(double frequencyHz, double sampleRate) const noexcept {
        return gainToDb(magnitude(frequencyHz, sampleRate));
    }

    // --- Second-order designs. q is the quality factor (0.7071 for Butterworth).

    static BiquadCoefficients lowPass(double sampleRate, double freq, double q) noexcept;
    static BiquadCoefficients highPass(double sampleRate, double freq, double q) noexcept;
    // Band pass with 0 dB gain at the centre frequency.
    static BiquadCoefficients bandPass(double sampleRate, double freq, double q) noexcept;
    static BiquadCoefficients notch(double sampleRate, double freq, double q) noexcept;
    static BiquadCoefficients allPass(double sampleRate, double freq, double q) noexcept;
    static BiquadCoefficients peak(double sampleRate, double freq, double q, double gainDb) noexcept;
    // Shelves with slope parameter q (0.7071 gives the steepest shelf with no overshoot).
    static BiquadCoefficients lowShelf(double sampleRate, double freq, double q, double gainDb) noexcept;
    static BiquadCoefficients highShelf(double sampleRate, double freq, double q, double gainDb) noexcept;

    // --- First-order designs (b2 = a2 = 0).

    static BiquadCoefficients lowPass1(double sampleRate, double freq) noexcept;
    static BiquadCoefficients highPass1(double sampleRate, double freq) noexcept;
    static BiquadCoefficients allPass1(double sampleRate, double freq) noexcept;
    // First-order shelves: freq is the corner where half the gain (in dB) is reached.
    static BiquadCoefficients lowShelf1(double sampleRate, double freq, double gainDb) noexcept;
    static BiquadCoefficients highShelf1(double sampleRate, double freq, double gainDb) noexcept;
};

class Biquad {
public:
    void setCoefficients(const BiquadCoefficients& c) noexcept { c_ = c; }
    const BiquadCoefficients& coefficients() const noexcept { return c_; }

    void reset() noexcept {
        s1_ = 0.0;
        s2_ = 0.0;
    }

    float process(float input) noexcept {
        const auto x = static_cast<double>(input);
        const double y = c_.b0 * x + s1_;
        s1_ = c_.b1 * x - c_.a1 * y + s2_;
        s2_ = c_.b2 * x - c_.a2 * y;
        return static_cast<float>(y);
    }

    void process(const float* input, float* output, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

private:
    BiquadCoefficients c_;
    double s1_ = 0.0;
    double s2_ = 0.0;
};

}  // namespace gigrig
