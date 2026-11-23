// test_eq.cpp - work package 11: the EQ (the compressor is not in version 1).
#include <memory>

#include "dsp_test_utils.hpp"
#include "effects/eq.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

struct EqStage {
    Eq eq;
    Eq::Settings settings;

    EqStage(double fs, int maxBlock, const Eq::Settings& s) : settings(s) {
        eq.setSettings(s);
        eq.prepare(fs, maxBlock);
    }

    void process(const float* in, float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = in[i];
        eq.process(out, n, settings);
    }
};

Eq::Settings shaped() {
    Eq::Settings s;
    s.on = true;
    s.highPassHz = 80.0f;
    s.lowPassHz = 9000.0f;
    s.frequencyHz = {150.0f, 700.0f, 2500.0f, 6000.0f};
    s.gainDb = {4.0f, -6.0f, 9.0f, -3.0f};
    return s;
}

// Measured gain in dB at freq, at an exact FFT bin.
double measuredDb(double fs, const Eq::Settings& s, double freq) {
    constexpr std::size_t n = 16384;
    const double f = std::round(freq * static_cast<double>(n) / fs) * fs / static_cast<double>(n);
    const auto x = sine(3 * n, f, fs, 0.25);
    std::vector<float> y(x.size());
    EqStage st(fs, 64, s);
    for (std::size_t pos = 0; pos < x.size(); pos += 64) {
        st.process(x.data() + pos, y.data() + pos, 64);
    }
    return gainToDb(toneAmplitude(y.data() + 2 * n, n, f, fs) / 0.25);
}

double designDb(double fs, const Eq::Settings& s, double freq) {
    constexpr double n = 16384.0;
    const double f = std::round(freq * n / fs) * fs / n;
    return gainToDb(std::abs(Eq::designResponse(s, f, fs)));
}

}  // namespace

TEST_CASE("eq: measured response matches the design curve at every rate") {
    for (double fs : kTestRates) {
        const auto s = shaped();
        for (double f : {40.0, 80.0, 150.0, 400.0, 700.0, 1500.0, 2500.0, 6000.0, 9000.0, 15000.0}) {
            CHECK_NEAR(measuredDb(fs, s, f), designDb(fs, s, f), 0.02);
        }
    }
}

TEST_CASE("eq: each band does what its knobs say (known answers)") {
    for (double fs : kTestRates) {
        Eq::Settings s;
        s.on = true;
        s.frequencyHz = {100.0f, 500.0f, 2000.0f, 5000.0f};
        // A lone peaking band reaches its gain exactly at its frequency.
        s.gainDb = {0.0f, 9.0f, 0.0f, 0.0f};
        CHECK_NEAR(designDb(fs, s, 500.0), 9.0, 0.05);
        CHECK_NEAR(measuredDb(fs, s, 500.0), 9.0, 0.05);
        s.gainDb = {0.0f, 0.0f, -12.0f, 0.0f};
        CHECK_NEAR(measuredDb(fs, s, 2000.0), -12.0, 0.05);
        // Shelves reach their full gain well past the corner and leave the other end flat.
        s.gainDb = {6.0f, 0.0f, 0.0f, 0.0f};
        CHECK_NEAR(measuredDb(fs, s, 25.0), 6.0, 0.15);
        CHECK_NEAR(measuredDb(fs, s, 3000.0), 0.0, 0.1);
        s.gainDb = {0.0f, 0.0f, 0.0f, -6.0f};
        CHECK_NEAR(measuredDb(fs, s, 18000.0), -6.0, 0.4);
        CHECK_NEAR(measuredDb(fs, s, 200.0), 0.0, 0.1);
        // Butterworth high and low pass are 3 dB down at their corners (the measured
        // response matches the design curve, checked above).
        Eq::Settings f;
        f.on = true;
        f.highPassHz = 120.0f;
        CHECK_NEAR(gainToDb(std::abs(Eq::designResponse(f, 120.0, fs))), -3.01, 0.01);
        f.highPassHz = Eq::kHighPassOffHz;
        f.lowPassHz = 4000.0f;
        CHECK_NEAR(gainToDb(std::abs(Eq::designResponse(f, 4000.0, fs))), -3.01, 0.01);
    }
}

TEST_CASE("eq: on but flat is transparent, and high/low pass at their ends are off") {
    Eq::Settings s;
    s.on = true;
    const auto x = musicalSignal(24000, 48000.0);
    std::vector<float> y(x.size());
    EqStage st(48000.0, 64, s);
    for (std::size_t pos = 0; pos < x.size(); pos += 64) st.process(x.data() + pos, y.data() + pos, 64);
    CHECK(maxDifference(x, y) < 1.0e-5);
}

TEST_CASE("eq: zero latency, block size independence while knobs move, no allocation") {
    for (double fs : kTestRates) {
        std::vector<float> imp(64, 0.0f), out(64);
        imp[0] = 1.0f;
        EqStage st(fs, 64, shaped());
        st.process(imp.data(), out.data(), 64);
        CHECK(out[0] != 0.0f);
    }
    auto make = [] {
        auto st = std::make_unique<EqStage>(48000.0, 512, shaped());
        // Every knob glides to a new value inside the test signal.
        st->settings.highPassHz = 200.0f;
        st->settings.lowPassHz = 5000.0f;
        st->settings.frequencyHz = {300.0f, 1200.0f, 4000.0f, 9000.0f};
        st->settings.gainDb = {-8.0f, 6.0f, -4.0f, 10.0f};
        return st;
    };
    auto process = [](auto& st, const float* in, float* out, int n) { st->process(in, out, n); };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, 48000.0));
}

TEST_CASE("eq: a fast frequency sweep makes no clicks") {
    // A steady tone while Mid 2's frequency jumps end to end: the smoothed
    // coefficients keep the output's sample-to-sample step close to the tone's own.
    const double fs = 96000.0;
    auto s = shaped();
    EqStage st(fs, 32, s);
    const auto x = sine(static_cast<std::size_t>(fs), 1000.0, fs, 0.25);
    std::vector<float> y(x.size());
    double maxStep = 0.0;
    for (std::size_t pos = 0; pos < x.size(); pos += 32) {
        if (pos == 9600) {
            st.settings.frequencyHz[2] = 500.0f;
            st.settings.gainDb[2] = 12.0f;
        }
        st.process(x.data() + pos, y.data() + pos, 32);
    }
    for (std::size_t i = 4800; i < y.size(); ++i) {
        maxStep = std::max(maxStep, std::fabs(static_cast<double>(y[i] - y[i - 1])));
    }
    // A 1 kHz tone at 0.25 boosted by at most about 12 dB steps by under 0.07 per sample at 96 kHz.
    CHECK(maxStep < 0.08);
}
