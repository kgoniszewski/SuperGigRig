// amp_test_utils.hpp - measurement helpers shared by the amp model tests
// (test_combo40.cpp, test_steel100.cpp). Every amp class has a Settings struct,
// setSettings, prepare(rate, maxBlock) and an in-place process.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "dsp_test_utils.hpp"

namespace gigrig::test {

// Response of a network (gain and phase) measured by running a steady sine through it.
template <class Network>
std::complex<double> measuredResponse(Network& n, double freq, double sampleRate) {
    n.reset();
    const auto periods = std::max(40.0, freq * 0.05);
    const auto len = static_cast<std::size_t>(std::ceil(periods * sampleRate / freq));
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < 2 * len; ++i) {
        const double w = 2.0 * kPi * freq * static_cast<double>(i) / sampleRate;
        const double y = n.process(std::sin(w));
        if (i >= len) {  // second half: settled
            re += y * std::sin(w);
            im += y * std::cos(w);
        }
    }
    // Rounded to whole periods only approximately, so allow for the window's leakage in the tolerance.
    return std::complex<double>(re, im) * (2.0 / static_cast<double>(len));
}

// Analog frequency whose bilinear (trapezoidal) image is freq at sampleRate.
inline double prewarped(double freq, double sampleRate) {
    return sampleRate / kPi * std::tan(kPi * freq / sampleRate);
}

// A driven amp as an aliasing-test stage (same shape as DrivenStage in test_nonlinear.cpp).
template <class Amp>
struct AmpStage {
    Amp amp;
    void process(const float* in, float* out, int n) noexcept {
        std::copy(in, in + n, out);
        amp.process(out, n);
    }
};

// Like aliasingDb in dsp_test_utils.hpp, with a longer settling time: an amp's bias
// shift and supply sag take a few hundred milliseconds to reach steady state.
template <class Stage>
double ampAliasingDb(Stage& stage, double sampleRate, std::size_t toneBin, float amplitude) {
    constexpr std::size_t n = 8192;
    const double freq = static_cast<double>(toneBin) * sampleRate / static_cast<double>(n);
    const std::size_t settle = (static_cast<std::size_t>(sampleRate * 1.5) / n + 1) * n;
    const auto x = sine(settle + n, freq, sampleRate, static_cast<double>(amplitude));
    std::vector<float> y(x.size());
    for (std::size_t pos = 0; pos < x.size(); pos += 64) {
        stage.process(x.data() + pos, y.data() + pos, 64);
    }
    const auto p = powerSpectrum(y.data() + settle, n);
    double alias = 0.0;
    const auto audibleBins = std::min(n / 2, static_cast<std::size_t>(20000.0 * n / sampleRate));
    for (std::size_t k = 1; k <= audibleBins; ++k) {
        if (k % toneBin != 0) {
            alias += p[k];
        }
    }
    return 10.0 * std::log10(alias / p[toneBin]);
}

// Steady-state output of an amp for a sine: fundamental amplitude and THD (harmonics 2 to 15).
struct ToneResult {
    double amplitude;
    double thd;
};

template <class Amp>
ToneResult ampTone(const typename Amp::Settings& s, double sampleRate, double freq, double amplitude) {
    Amp amp;
    amp.setSettings(s);
    amp.prepare(sampleRate, 64);
    const auto n = static_cast<std::size_t>(sampleRate);
    auto x = sine(n, freq, sampleRate, amplitude);
    for (std::size_t pos = 0; pos < n; pos += 64) {
        amp.process(x.data() + pos, static_cast<int>(std::min<std::size_t>(64, n - pos)));
    }
    const std::size_t from = n / 2;
    const double a1 = toneAmplitude(x.data() + from, n - from, freq, sampleRate);
    double h = 0.0;
    for (int k = 2; k <= 15 && freq * k < 0.45 * sampleRate; ++k) {
        const double a = toneAmplitude(x.data() + from, n - from, freq * k, sampleRate);
        h += a * a;
    }
    return {a1, std::sqrt(h) / a1};
}

}  // namespace gigrig::test
