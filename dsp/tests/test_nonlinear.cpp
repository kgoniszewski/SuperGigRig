// test_nonlinear.cpp - waveshapers, half-band oversampling and aliasing.
#include <cstdio>
#include <memory>

#include "blocks/oversampler.hpp"
#include "blocks/waveshaper.hpp"
#include "dsp_test_utils.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

// Magnitude of one half-band stage's H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2 at a
// frequency normalised to the high rate.
double halfbandMagnitude(const HalfbandStage& s, double f) {
    const std::complex<double> z = std::polar(1.0, 2.0 * kPi * f);
    const std::complex<double> z2 = z * z;
    std::complex<double> a0 = 1.0, a1 = 1.0;
    for (int i = 0; i < s.numCoefficients(); ++i) {
        const double c = s.coefficients()[static_cast<std::size_t>(i)];
        const auto ap = (c + 1.0 / z2) / (1.0 + c / z2);
        (i % 2 == 0 ? a0 : a1) *= ap;
    }
    return std::abs(0.5 * (a0 + a1 / z));
}

// A driven nonlinear stage as the amp models will use it: oversample, shape, decimate.
template <class Shaper>
struct DrivenStage {
    Oversampler os;
    Shaper shaper;
    float drive = 1.0f;

    void prepare(int factor, int maxBlock) {
        os.prepare(factor, maxBlock);
        shaper.reset();
    }

    void process(const float* in, float* out, int n) noexcept {
        float* hi = os.upsample(in, n);
        for (int i = 0; i < n * os.factor(); ++i) {
            hi[i] = shaper.process(drive * hi[i]);
        }
        os.downsample(out, n);
    }
};

}  // namespace

TEST_CASE("shapes: F is the antiderivative of f") {
    WaveshaperTable table;
    // An asymmetric triode-like curve: soft on one side, harder on the other.
    table.build([](double x) { return x >= 0.0 ? std::tanh(x) : std::tanh(1.6 * x) / 1.6; }, -4.0, 4.0, 2049);
    const shapes::Table tableShape{&table};
    const double h = 1.0e-5;
    for (double x = -6.0; x <= 6.0; x += 0.173) {
        CHECK_NEAR((shapes::Tanh{}.F(x + h) - shapes::Tanh{}.F(x - h)) / (2 * h), std::tanh(x), 1e-6);
        CHECK_NEAR((shapes::HardClip{}.F(x + h) - shapes::HardClip{}.F(x - h)) / (2 * h),
                   shapes::HardClip{}.f(x), 1e-6);
        CHECK_NEAR((shapes::SoftSqrt{}.F(x + h) - shapes::SoftSqrt{}.F(x - h)) / (2 * h),
                   shapes::SoftSqrt{}.f(x), 1e-6);
        CHECK_NEAR((tableShape.F(x + h) - tableShape.F(x - h)) / (2 * h), tableShape.f(x), 1e-4);
    }
    CHECK(shapes::Tanh{}.F(0.0) == 0.0);
    CHECK(tableShape.F(0.0) == 0.0);
    CHECK(std::isfinite(shapes::Tanh{}.F(1.0e6)));
}

TEST_CASE("table shape matches its closed-form source between grid points") {
    WaveshaperTable table;
    table.build([](double x) { return std::tanh(x); }, -5.0, 5.0, 4097);
    double err = 0.0;
    for (double x = -5.0; x <= 5.0; x += 0.00731) {
        err = std::max(err, std::fabs(table.f(x) - std::tanh(x)));
    }
    CHECK(err < 1.0e-5);
    CHECK(table.f(100.0) == table.f(5.0));  // held at the edge, as a stage saturates
}

TEST_CASE("ADAA waveshaper follows the static curve for slow signals and keeps silence silent") {
    AdaaWaveshaper<shapes::Tanh> s;
    s.reset();
    CHECK(s.process(0.0f) == 0.0f);
    // A slow ramp: the average over a tiny step is the curve at the midpoint.
    double err = 0.0;
    float prev = 0.0f;
    for (int i = 1; i <= 4000; ++i) {
        const float x = static_cast<float>(i) * 0.001f;
        const float y = s.process(x);
        err =
            std::max(err, std::fabs(static_cast<double>(y) - std::tanh(0.5 * static_cast<double>(x + prev))));
        prev = x;
    }
    CHECK(err < 1.0e-6);
    // A held value converges to f(x) exactly through the ill-conditioned branch.
    s.process(0.7f);
    CHECK_NEAR(s.process(0.7f), std::tanh(0.7), 1e-7);
}

TEST_CASE("half-band stages: passband flat, stopband rejects at least 95 dB") {
    HalfbandStage s1, s2;
    s1.design(8, 0.04);
    s2.design(6, 0.1);
    double pass1 = 0.0, stop1 = 0.0, pass2 = 0.0, stop2 = 0.0;
    for (double f = 0.0; f <= 0.5; f += 1.0e-4) {
        if (f <= 0.2) pass1 = std::max(pass1, std::fabs(gainToDb(halfbandMagnitude(s1, f))));
        if (f >= 0.29) stop1 = std::max(stop1, halfbandMagnitude(s1, f));
        if (f <= 0.15) pass2 = std::max(pass2, std::fabs(gainToDb(halfbandMagnitude(s2, f))));
        if (f >= 0.35) stop2 = std::max(stop2, halfbandMagnitude(s2, f));
    }
    std::printf("    stage 1: passband ripple %.2g dB, stopband %.1f dB\n", pass1, gainToDb(stop1));
    std::printf("    stage 2: passband ripple %.2g dB, stopband %.1f dB\n", pass2, gainToDb(stop2));
    CHECK(pass1 < 1.0e-6);
    CHECK(pass2 < 1.0e-6);
    CHECK(gainToDb(stop1) < -95.0);
    CHECK(gainToDb(stop2) < -95.0);
}

TEST_CASE("oversampler round trip is flat across the guitar band at every rate") {
    for (double fs : kTestRates) {
        for (int factor : {2, 4}) {
            Oversampler os;
            os.prepare(factor, 64);
            for (double target : {50.0, 1000.0, 5000.0, 0.4 * 44100.0}) {
                // On an exact bin of the 8192-sample window, so the amplitude estimate is exact.
                const double f = std::round(target * 8192.0 / fs) * fs / 8192.0;
                const auto x = sine(16384, f, fs, 0.5);
                std::vector<float> y(x.size());
                for (std::size_t pos = 0; pos < x.size(); pos += 64) {
                    os.upsample(x.data() + pos, 64);
                    os.downsample(y.data() + pos, 64);
                }
                const double gain = toneAmplitude(y.data() + 8192, 8192, f, fs) / 0.5;
                CHECK_NEAR(gainToDb(gain), 0.0, 0.05);
            }
        }
    }
}

TEST_CASE("oversampler adds no lookahead: an impulse comes out from the first sample") {
    for (int factor : {1, 2, 4}) {
        Oversampler os;
        os.prepare(factor, 32);
        std::vector<float> in(32, 0.0f), out(32, 0.0f);
        in[0] = 1.0f;
        os.upsample(in.data(), 32);
        os.downsample(out.data(), 32);
        CHECK(out[0] != 0.0f);
        if (factor == 1) {
            CHECK(out[0] == 1.0f);
        }
    }
}

TEST_CASE("oversampler group delay stays near 0.1 ms in the guitar band") {
    // Causal IIR filters have no lookahead but do have group delay. It is the
    // only delay the oversampled stages add, so it is measured and bounded here:
    // phase delay of a low tone (where it is flattest) through up and down.
    for (double fs : kTestRates) {
        for (int factor : {2, 4}) {
            Oversampler os;
            os.prepare(factor, 64);
            const double f = std::round(200.0 * 16384.0 / fs) * fs / 16384.0;
            const auto x = sine(32768, f, fs, 0.5);
            std::vector<float> y(x.size());
            for (std::size_t pos = 0; pos < x.size(); pos += 64) {
                os.upsample(x.data() + pos, 64);
                os.downsample(y.data() + pos, 64);
            }
            double re = 0.0, im = 0.0;
            for (std::size_t i = 16384; i < y.size(); ++i) {
                const double w = 2.0 * kPi * f * static_cast<double>(i) / fs;
                re += static_cast<double>(y[i]) * std::sin(w);
                im += static_cast<double>(y[i]) * std::cos(w);
            }
            const double delaySeconds = -std::atan2(im, re) / (2.0 * kPi * f);
            std::printf("    %6.0f Hz, %dx: %.3f ms (%.2f samples)\n", fs, factor, delaySeconds * 1000.0,
                        delaySeconds * fs);
            CHECK(delaySeconds > 0.0);
            CHECK(delaySeconds * fs < 6.0);
        }
    }
}

TEST_CASE("oversampled ADAA stage is block size independent") {
    auto make = [] {
        auto s = std::make_unique<DrivenStage<AdaaWaveshaper<shapes::Tanh>>>();
        s->prepare(4, 512);
        s->drive = 8.0f;
        return s;
    };
    auto process = [](auto& s, const float* in, float* out, int n) { s->process(in, out, n); };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, 48000.0));
}

TEST_CASE("aliasing: 4x oversampling with ADAA keeps audible aliasing below the stated limits") {
    // A hard-driven tanh (drive 10, close to a square wave) on a high note is the
    // worst case for an amp model. The stated limits:
    //   - 4x + ADAA: aliased energy below 20 kHz at least 100 dB under the
    //     fundamental for a 1.2 kHz tone and 70 dB for a 4.8 kHz tone, at every
    //     sample rate (measured on Linux CI: about -119 and -74 dB at 44.1 kHz);
    //   - at least 30 dB better than the same shaper run naively at the base rate.
    for (double fs : kTestRates) {
        // Tone bins: about 1.2 kHz and 4.8 kHz at the rate, on bins that share no small factors.
        const auto lowBin = static_cast<std::size_t>(std::lround(1200.0 * 8192.0 / fs)) | 1u;
        const auto highBin = static_cast<std::size_t>(std::lround(4800.0 * 8192.0 / fs)) | 1u;
        for (std::size_t bin : {lowBin, highBin}) {
            DrivenStage<NaiveWaveshaper<shapes::Tanh>> naive;
            naive.prepare(1, 64);
            naive.drive = 10.0f;
            DrivenStage<AdaaWaveshaper<shapes::Tanh>> good;
            good.prepare(4, 64);
            good.drive = 10.0f;
            const double naiveDb = aliasingDb(naive, fs, bin, 1.0f);
            const double goodDb = aliasingDb(good, fs, bin, 1.0f);
            std::printf("    %6.0f Hz, tone %7.1f Hz: naive 1x %6.1f dB, 4x+ADAA %6.1f dB\n", fs,
                        static_cast<double>(bin) * fs / 8192.0, naiveDb, goodDb);
            CHECK(goodDb < (bin == lowBin ? -100.0 : -70.0));
            CHECK(goodDb < naiveDb - 30.0);
        }
    }
}

TEST_CASE("aliasing: hard clipper at 4x with ADAA stays below -65 dB") {
    // Hard clipping is the harshest curve the pedals use (diode clippers); 2.4 kHz tone.
    for (double fs : kTestRates) {
        const auto bin = static_cast<std::size_t>(std::lround(2400.0 * 8192.0 / fs)) | 1u;
        DrivenStage<AdaaWaveshaper<shapes::HardClip>> stage;
        stage.prepare(4, 64);
        stage.drive = 10.0f;
        const double db = aliasingDb(stage, fs, bin, 1.0f);
        std::printf("    %6.0f Hz hard clip: %6.1f dB\n", fs, db);
        CHECK(db < -65.0);
    }
}
