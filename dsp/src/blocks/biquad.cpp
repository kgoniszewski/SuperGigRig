#include "biquad.hpp"

namespace gigrig {
namespace {

// Keeps every design valid for any input: frequency inside (0, Nyquist), q > 0.
double clampFrequency(double sampleRate, double freq) noexcept {
    return std::clamp(freq, 1.0e-3, 0.49999 * sampleRate);
}

struct Prewarp {
    double cosw, sinw;
};

Prewarp prewarp(double sampleRate, double freq) noexcept {
    const double w = kTwoPi * clampFrequency(sampleRate, freq) / sampleRate;
    return {std::cos(w), std::sin(w)};
}

BiquadCoefficients normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    BiquadCoefficients c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

double safeQ(double q) noexcept {
    return std::max(q, 1.0e-3);
}

// tan(w/2) of the bilinear transform, for the first-order designs.
double bilinearK(double sampleRate, double freq) noexcept {
    return std::tan(kPi * clampFrequency(sampleRate, freq) / sampleRate);
}

}  // namespace

BiquadCoefficients BiquadCoefficients::lowPass(double sampleRate, double freq, double q) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0, 1.0 + alpha, -2.0 * cosw,
                     1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::highPass(double sampleRate, double freq, double q) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0, 1.0 + alpha, -2.0 * cosw,
                     1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::bandPass(double sampleRate, double freq, double q) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::notch(double sampleRate, double freq, double q) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise(1.0, -2.0 * cosw, 1.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::allPass(double sampleRate, double freq, double q) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise(1.0 - alpha, -2.0 * cosw, 1.0 + alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::peak(double sampleRate, double freq, double q,
                                            double gainDb) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = sinw / (2.0 * safeQ(q));
    return normalise(1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cosw,
                     1.0 - alpha / a);
}

BiquadCoefficients BiquadCoefficients::lowShelf(double sampleRate, double freq, double q,
                                                double gainDb) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = sinw / (2.0 * safeQ(q));
    const double k = 2.0 * std::sqrt(a) * alpha;
    return normalise(a * ((a + 1.0) - (a - 1.0) * cosw + k), 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw),
                     a * ((a + 1.0) - (a - 1.0) * cosw - k), (a + 1.0) + (a - 1.0) * cosw + k,
                     -2.0 * ((a - 1.0) + (a + 1.0) * cosw), (a + 1.0) + (a - 1.0) * cosw - k);
}

BiquadCoefficients BiquadCoefficients::highShelf(double sampleRate, double freq, double q,
                                                 double gainDb) noexcept {
    const auto [cosw, sinw] = prewarp(sampleRate, freq);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = sinw / (2.0 * safeQ(q));
    const double k = 2.0 * std::sqrt(a) * alpha;
    return normalise(a * ((a + 1.0) + (a - 1.0) * cosw + k), -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw),
                     a * ((a + 1.0) + (a - 1.0) * cosw - k), (a + 1.0) - (a - 1.0) * cosw + k,
                     2.0 * ((a - 1.0) - (a + 1.0) * cosw), (a + 1.0) - (a - 1.0) * cosw - k);
}

BiquadCoefficients BiquadCoefficients::lowPass1(double sampleRate, double freq) noexcept {
    const double k = bilinearK(sampleRate, freq);
    return normalise(k, k, 0.0, k + 1.0, k - 1.0, 0.0);
}

BiquadCoefficients BiquadCoefficients::highPass1(double sampleRate, double freq) noexcept {
    const double k = bilinearK(sampleRate, freq);
    return normalise(1.0, -1.0, 0.0, k + 1.0, k - 1.0, 0.0);
}

BiquadCoefficients BiquadCoefficients::allPass1(double sampleRate, double freq) noexcept {
    const double k = bilinearK(sampleRate, freq);
    return normalise(k - 1.0, k + 1.0, 0.0, k + 1.0, k - 1.0, 0.0);
}

// First-order shelves place the corner at the geometric mean of the pole and
// zero, so exactly half the shelf gain in dB is reached at freq.
BiquadCoefficients BiquadCoefficients::lowShelf1(double sampleRate, double freq, double gainDb) noexcept {
    const double k = bilinearK(sampleRate, freq);
    const double g = std::pow(10.0, gainDb / 40.0);  // sqrt of the linear gain
    // Analog prototype H(s) = (s + k*g) / (s + k/g), with s the bilinear variable.
    return normalise(1.0 + k * g, k * g - 1.0, 0.0, 1.0 + k / g, k / g - 1.0, 0.0);
}

BiquadCoefficients BiquadCoefficients::highShelf1(double sampleRate, double freq, double gainDb) noexcept {
    const double k = bilinearK(sampleRate, freq);
    const double g = std::pow(10.0, gainDb / 40.0);
    // Analog prototype H(s) = g^2 (s + k/g) / (s + k*g): unity at DC, g^2 at high frequency.
    const double g2 = g * g;
    return normalise(g2 * (1.0 + k / g), g2 * (k / g - 1.0), 0.0, 1.0 + k * g, k * g - 1.0, 0.0);
}

}  // namespace gigrig
