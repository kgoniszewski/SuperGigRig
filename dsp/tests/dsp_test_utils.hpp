// dsp_test_utils.hpp - signals, spectra and the standard block checks from
// plan section 8, shared by the DSP tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "alloc_guard.hpp"
#include "blocks/dsp_math.hpp"
#include "test_framework.hpp"

namespace gigrig::test {

inline constexpr double kTestRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
inline constexpr int kTestBlockSizes[] = {1, 32, 64, 512};

inline std::vector<float> sine(std::size_t n, double freq, double sampleRate, double amplitude = 1.0) {
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * freq * static_cast<double>(i) / sampleRate));
    }
    return x;
}

// Deterministic white noise in [-amplitude, amplitude].
inline std::vector<float> noise(std::size_t n, double amplitude = 0.5, std::uint32_t seed = 12345) {
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        x[i] = static_cast<float>(amplitude * (static_cast<double>(seed >> 8) / 8388608.0 - 1.0));
    }
    return x;
}

// A guitar-like test signal: decaying plucked tones plus noise, with level changes.
inline std::vector<float> musicalSignal(std::size_t n, double sampleRate) {
    std::vector<float> x = noise(n, 0.02);
    const double notes[] = {82.41, 196.0, 329.63, 659.26};
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const std::size_t note = (i / static_cast<std::size_t>(sampleRate / 4.0)) % 4;
        const double tn = std::fmod(t, 0.25);
        double s = 0.0;
        for (int h = 1; h <= 6; ++h) {
            s += std::sin(2.0 * std::numbers::pi * notes[note] * h * t) / h;
        }
        x[i] += static_cast<float>(0.6 * s * std::exp(-tn * 8.0));
    }
    return x;
}

inline double rms(const float* x, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

inline double rms(const std::vector<float>& x, std::size_t from = 0) {
    return from < x.size() ? rms(x.data() + from, x.size() - from) : 0.0;
}

inline double maxAbs(const std::vector<float>& x) {
    double m = 0.0;
    for (float v : x) {
        m = std::max(m, static_cast<double>(std::fabs(v)));
    }
    return m;
}

inline bool allFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

inline double maxDifference(const std::vector<float>& a, const std::vector<float>& b) {
    double m = a.size() == b.size() ? 0.0 : 1.0e9;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    }
    return m;
}

// Amplitude of the component at freq, by correlation over whole periods-ish (long window).
inline double toneAmplitude(const float* x, std::size_t n, double freq, double sampleRate) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 2.0 * std::numbers::pi * freq * static_cast<double>(i) / sampleRate;
        re += static_cast<double>(x[i]) * std::cos(w);
        im += static_cast<double>(x[i]) * std::sin(w);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(n);
}

// In-place radix-2 FFT; size must be a power of two.
inline void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Power per bin (0 .. n/2) of n samples starting at x (rectangular window).
inline std::vector<double> powerSpectrum(const float* x, std::size_t n) {
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = static_cast<double>(x[i]);
    }
    fft(a);
    std::vector<double> p(n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k) {
        p[k] = std::norm(a[k]);
    }
    return p;
}

// Renders a signal through a processor in blocks of blockSize, running every
// process call under the allocation guard. make() builds and prepares a fresh
// processor (allocation allowed); process(proc, in, out, n) runs one block.
template <class Make, class Process>
std::vector<float> renderInBlocks(Make make, Process process, const std::vector<float>& in, int blockSize) {
    auto proc = make();
    std::vector<float> out(in.size(), 0.0f);
    for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t>(blockSize)) {
        const int n =
            static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize), in.size() - pos));
        AllocationGuard guard;
        process(proc, in.data() + pos, out.data() + pos, n);
    }
    return out;
}

// The plan's block-size independence check: identical output for 1, 32, 64 and 512.
template <class Make, class Process>
void checkBlockSizeIndependence(Make make, Process process, const std::vector<float>& in) {
    const long before = allocationViolations();
    const auto reference = renderInBlocks(make, process, in, 1);
    CHECK(allFinite(reference));
    for (int b : kTestBlockSizes) {
        const auto output = renderInBlocks(make, process, in, b);
        CHECK(output == reference);
    }
    CHECK(allocationViolations() == before);
}

}  // namespace gigrig::test
