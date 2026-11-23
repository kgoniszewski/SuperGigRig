// test_filters.cpp - biquads, the state-variable filter and parameter smoothers.
#include <memory>

#include "blocks/biquad.hpp"
#include "blocks/smoother.hpp"
#include "blocks/svf.hpp"
#include "dsp_test_utils.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

// Runs a steady sine through a filter and returns the measured gain in dB.
template <class Filter>
double measuredGainDb(Filter& filter, double freq, double sampleRate) {
    const auto n = static_cast<std::size_t>(sampleRate);  // 1 s: settle, then measure the second half
    const auto x = sine(n, freq, sampleRate, 0.5);
    std::vector<float> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = filter.process(x[i]);
    }
    const std::size_t half = n / 2;
    return gainToDb(toneAmplitude(y.data() + half, n - half, freq, sampleRate) / 0.5);
}

}  // namespace

TEST_CASE("biquad lowpass and highpass are 3 dB down at the cutoff at every rate") {
    for (double fs : kTestRates) {
        for (double fc : {80.0, 1000.0, 8000.0}) {
            const auto lp = BiquadCoefficients::lowPass(fs, fc, std::sqrt(0.5));
            const auto hp = BiquadCoefficients::highPass(fs, fc, std::sqrt(0.5));
            CHECK_NEAR(lp.magnitudeDb(fc, fs), -3.0103, 0.001);
            CHECK_NEAR(hp.magnitudeDb(fc, fs), -3.0103, 0.001);
            CHECK_NEAR(lp.magnitudeDb(1.0, fs), 0.0, 0.001);
            CHECK(hp.magnitude(1.0, fs) < 1.0e-3);
            // Second order: about 12 dB per octave well past the cutoff (and well below Nyquist).
            if (fc <= 1000.0)
                CHECK_NEAR(lp.magnitudeDb(fc * 2.0, fs) - lp.magnitudeDb(fc * 4.0, fs), 12.0, 1.5);
        }
    }
}

TEST_CASE("biquad response measured with a sine matches the analytic curve") {
    for (double fs : kTestRates) {
        const auto c = BiquadCoefficients::peak(fs, 700.0, 1.2, 9.0);
        for (double f : {100.0, 700.0, 3000.0}) {
            Biquad b;
            b.setCoefficients(c);
            CHECK_NEAR(measuredGainDb(b, f, fs), c.magnitudeDb(f, fs), 0.02);
        }
    }
}

TEST_CASE("peak, shelf, bandpass, notch and allpass designs hit their known answers") {
    const double fs = 48000.0;
    CHECK_NEAR(BiquadCoefficients::peak(fs, 1000.0, 1.0, 6.0).magnitudeDb(1000.0, fs), 6.0, 1e-6);
    CHECK_NEAR(BiquadCoefficients::peak(fs, 1000.0, 1.0, -12.0).magnitudeDb(1000.0, fs), -12.0, 1e-6);
    CHECK_NEAR(BiquadCoefficients::peak(fs, 1000.0, 1.0, 6.0).magnitudeDb(20.0, fs), 0.0, 0.05);

    const auto ls = BiquadCoefficients::lowShelf(fs, 200.0, std::sqrt(0.5), 8.0);
    CHECK_NEAR(ls.magnitudeDb(1.0, fs), 8.0, 0.01);
    CHECK_NEAR(ls.magnitudeDb(200.0, fs), 4.0, 0.01);  // half the gain at the corner
    CHECK_NEAR(ls.magnitudeDb(20000.0, fs), 0.0, 0.01);

    const auto hs = BiquadCoefficients::highShelf(fs, 3000.0, std::sqrt(0.5), -6.0);
    CHECK_NEAR(hs.magnitudeDb(23999.0, fs), -6.0, 0.02);
    CHECK_NEAR(hs.magnitudeDb(3000.0, fs), -3.0, 0.01);
    CHECK_NEAR(hs.magnitudeDb(20.0, fs), 0.0, 0.01);

    const auto bp = BiquadCoefficients::bandPass(fs, 500.0, 2.0);
    CHECK_NEAR(bp.magnitudeDb(500.0, fs), 0.0, 1e-6);
    CHECK(bp.magnitudeDb(50.0, fs) < -18.0);

    CHECK(BiquadCoefficients::notch(fs, 60.0, 5.0).magnitudeDb(60.0, fs) < -100.0);

    const auto ap = BiquadCoefficients::allPass(fs, 1000.0, 0.7);
    for (double f : {10.0, 500.0, 1000.0, 5000.0, 20000.0}) {
        CHECK_NEAR(ap.magnitude(f, fs), 1.0, 1e-9);
    }
}

TEST_CASE("first-order designs: -3 dB corner, half-gain shelves, flat allpass") {
    for (double fs : kTestRates) {
        CHECK_NEAR(BiquadCoefficients::lowPass1(fs, 720.0).magnitudeDb(720.0, fs), -3.0103, 1e-4);
        CHECK_NEAR(BiquadCoefficients::highPass1(fs, 30.0).magnitudeDb(30.0, fs), -3.0103, 1e-4);
        const auto ls = BiquadCoefficients::lowShelf1(fs, 300.0, 10.0);
        CHECK_NEAR(ls.magnitudeDb(0.01, fs), 10.0, 1e-3);
        CHECK_NEAR(ls.magnitudeDb(300.0, fs), 5.0, 1e-3);
        const auto hs = BiquadCoefficients::highShelf1(fs, 2000.0, -8.0);
        CHECK_NEAR(hs.magnitudeDb(0.01, fs), 0.0, 1e-3);
        CHECK_NEAR(hs.magnitudeDb(2000.0, fs), -4.0, 1e-3);
        CHECK_NEAR(hs.magnitudeDb(fs * 0.4999, fs), -8.0, 0.05);
        CHECK_NEAR(BiquadCoefficients::allPass1(fs, 1000.0).magnitude(5000.0, fs), 1.0, 1e-9);
    }
}

TEST_CASE("designs stay valid at extreme settings") {
    const double fs = 44100.0;
    for (double f : {0.0, -5.0, 1.0e9}) {
        const auto c = BiquadCoefficients::lowPass(fs, f, 0.0);
        CHECK(std::isfinite(c.b0) && std::isfinite(c.a1) && std::isfinite(c.a2));
        CHECK(std::fabs(c.a2) < 1.0);  // poles inside the unit circle
    }
}

TEST_CASE("biquad output is independent of block size") {
    const double fs = 48000.0;
    auto make = [fs] {
        auto b = std::make_unique<Biquad>();
        b->setCoefficients(BiquadCoefficients::lowShelf(fs, 150.0, 0.7, 6.0));
        return b;
    };
    auto process = [](std::unique_ptr<Biquad>& b, const float* in, float* out, int n) {
        b->process(in, out, n);
    };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, fs));
}

TEST_CASE("state-variable filter is tuned at every rate") {
    for (double fs : kTestRates) {
        StateVariableFilter svf;
        svf.prepare(fs);
        svf.setParameters(1000.0, std::sqrt(0.5));
        struct Lp {
            StateVariableFilter* f;
            float process(float x) { return f->process(x).lowPass; }
        } lp{&svf};
        CHECK_NEAR(measuredGainDb(lp, 1000.0, fs), -3.0103, 0.02);
        svf.reset();
        struct Hp {
            StateVariableFilter* f;
            float process(float x) { return f->process(x).highPass; }
        } hp{&svf};
        CHECK_NEAR(measuredGainDb(hp, 1000.0, fs), -3.0103, 0.02);
        // Band pass peaks at a gain of q at the cutoff.
        svf.reset();
        svf.setParameters(2000.0, 4.0);
        struct Bp {
            StateVariableFilter* f;
            float process(float x) { return f->process(x).bandPass; }
        } bp{&svf};
        CHECK_NEAR(measuredGainDb(bp, 2000.0, fs), gainToDb(4.0), 0.05);
    }
}

TEST_CASE("state-variable filter stays stable when swept every sample") {
    for (double fs : kTestRates) {
        StateVariableFilter svf;
        svf.prepare(fs);
        const auto x = noise(static_cast<std::size_t>(fs), 0.5);
        double peak = 0.0;
        bool finite = true;
        for (std::size_t i = 0; i < x.size(); ++i) {
            // Fast sweep 50 Hz to 15 kHz and back, 20 times a second, at high resonance.
            const double ph = std::fmod(static_cast<double>(i) * 20.0 / fs, 1.0);
            const double cutoff = 50.0 * std::pow(300.0, ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph);
            svf.setParameters(cutoff, 8.0);
            const auto y = svf.process(x[i]);
            finite = finite && std::isfinite(y.lowPass) && std::isfinite(y.bandPass);
            peak = std::max(peak, static_cast<double>(std::fabs(y.lowPass)));
        }
        CHECK(finite);
        CHECK(peak < 50.0);
    }
}

TEST_CASE("linear smoother reaches the exact target after the ramp time") {
    for (double fs : kTestRates) {
        LinearSmoother s;
        s.prepare(fs, 0.01);
        s.snap(1.0f);
        s.setTarget(0.25f);
        const int ramp = static_cast<int>(std::lround(0.01 * fs));
        float v = 0.0f;
        for (int i = 0; i < ramp - 1; ++i) {
            v = s.next();
        }
        CHECK(v > 0.25f);
        CHECK(s.isSmoothing());
        CHECK(s.next() == 0.25f);
        CHECK(!s.isSmoothing());
        CHECK(s.next() == 0.25f);
    }
}

TEST_CASE("linear smoother ramps linearly and can be retargeted mid-ramp") {
    LinearSmoother s;
    s.prepare(1000.0, 0.004);  // 4 samples
    s.snap(0.0f);
    s.setTarget(1.0f);
    CHECK_NEAR(s.next(), 0.25, 1e-6);
    CHECK_NEAR(s.next(), 0.5, 1e-6);
    s.setTarget(0.0f);  // from 0.5 back to 0 over a full ramp
    CHECK_NEAR(s.next(), 0.375, 1e-6);
    for (int i = 0; i < 3; ++i) {
        s.next();
    }
    CHECK(s.current() == 0.0f);
}

TEST_CASE("one-pole smoother has the stated time constant and settles exactly") {
    for (double fs : kTestRates) {
        OnePoleSmoother s;
        s.prepare(fs, 0.005);
        s.snap(0.0f);
        s.setTarget(1.0f);
        const int tau = static_cast<int>(std::lround(0.005 * fs));
        float v = 0.0f;
        for (int i = 0; i < tau; ++i) {
            v = s.next();
        }
        CHECK_NEAR(v, 1.0 - std::exp(-1.0), 0.01);
        for (int i = 0; i < tau * 40; ++i) {
            v = s.next();
        }
        CHECK(v == 1.0f);
        CHECK(!s.isSmoothing());
    }
}
