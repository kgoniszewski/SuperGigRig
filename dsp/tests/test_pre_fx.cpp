// test_pre_fx.cpp - work package 9: the Pre FX pedals (plan section 8 tests for
// each block: known answers, block-size independence under the allocation
// guard, sample-rate sweep, aliasing, zero latency, bypass).
#include <gigrig/gigrig.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "dsp_test_utils.hpp"
#include "effects/pre_fx.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

// Wraps a pedal with fixed settings in the stage interface the shared helpers use.
template <class Pedal>
struct PedalStage {
    Pedal pedal;
    typename Pedal::Settings settings;

    PedalStage(double sampleRate, int maxBlock, const typename Pedal::Settings& s) : settings(s) {
        pedal.setSettings(s);
        pedal.prepare(sampleRate, maxBlock);
    }

    void process(const float* in, float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            out[i] = in[i];
        }
        pedal.process(out, n, settings);
    }
};

template <class Pedal>
std::unique_ptr<PedalStage<Pedal>> makeStage(double fs, int maxBlock, const typename Pedal::Settings& s) {
    return std::make_unique<PedalStage<Pedal>>(fs, maxBlock, s);
}

template <class Pedal>
void checkPedalBlockSizes(double fs, const typename Pedal::Settings& s, const std::vector<float>& in) {
    auto make = [&] { return makeStage<Pedal>(fs, 512, s); };
    auto process = [](auto& st, const float* x, float* y, int n) { st->process(x, y, n); };
    checkBlockSizeIndependence(make, process, in);
}

template <class Pedal>
std::vector<float> renderPedal(double fs, const typename Pedal::Settings& s, const std::vector<float>& in,
                               int block = 64) {
    auto st = makeStage<Pedal>(fs, block, s);
    std::vector<float> out(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t>(block)) {
        const int n =
            static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), in.size() - pos));
        st->process(in.data() + pos, out.data() + pos, n);
    }
    return out;
}

// Small-signal gain in dB of a pedal at freq (an exact FFT bin of the window).
template <class Pedal>
double smallSignalGainDb(double fs, const typename Pedal::Settings& s, double freq,
                         double amplitude = 1.0e-3) {
    constexpr std::size_t n = 16384;
    const double f = std::round(freq * static_cast<double>(n) / fs) * fs / static_cast<double>(n);
    const auto x = sine(2 * n, f, fs, amplitude);
    const auto y = renderPedal<Pedal>(fs, s, x);
    return gainToDb(toneAmplitude(y.data() + n, n, f, fs) / amplitude);
}

template <class Pedal>
double pedalAliasingDb(double fs, const typename Pedal::Settings& s, double toneHz, float amplitude) {
    auto st = makeStage<Pedal>(fs, 64, s);
    const auto bin = static_cast<std::size_t>(std::lround(toneHz * 8192.0 / fs)) | 1u;
    return aliasingDb(*st, fs, bin, amplitude);
}

std::vector<float> impulse(std::size_t n, float amplitude = 0.5f) {
    std::vector<float> x(n, 0.0f);
    x[0] = amplitude;
    return x;
}

}  // namespace

// Aliasing limits, for a 2.4 kHz tone at 0.5 of full scale with every gain
// knob at maximum (the worst case these pedals meet): -60 dB for the two
// pedals that clip hardest (Boost in overdrive mode at +20 dB, Gold OD at gain
// 10, which drives its diodes about 55 dB past clipping), -70 dB for Twin OD.
// Measured on Linux: about -63 to -66 dB and -77 to -82 dB. 2x at 88.2 and 96
// kHz measures the same as 4x at 44.1 and 48 kHz.

// ---------------------------------------------------------------- whole section

TEST_CASE("pre fx: everything on is block size independent and never allocates, at every rate") {
    for (double fs : kTestRates) {
        auto make = [fs] {
            auto params = std::make_unique<ParameterValues>();
            for (int id : {GIGRIG_PARAM_BOOST_ON, GIGRIG_PARAM_ENVFILTER_ON, GIGRIG_PARAM_GOLDOD_ON,
                           GIGRIG_PARAM_TWINOD_ON, GIGRIG_PARAM_BUCKETDELAY_ON}) {
                params->set(id, 1.0f);
            }
            params->set(GIGRIG_PARAM_BUCKETDELAY_TIME, 60.0f);
            auto fx = std::make_unique<PreFx>();
            fx->prepare(fs, 512, *params);
            return std::make_pair(std::move(params), std::move(fx));
        };
        auto process = [](auto& st, const float* in, float* out, int n) {
            for (int i = 0; i < n; ++i) out[i] = in[i];
            st.second->process(out, n, *st.first);
        };
        const auto in = musicalSignal(static_cast<std::size_t>(fs * 0.3), fs);
        checkBlockSizeIndependence(make, process, in);
        const auto out = renderInBlocks(make, process, in, 32);
        CHECK(allFinite(out));
        CHECK(maxAbs(out) < 4.0);
    }
}

TEST_CASE("pre fx: all pedals off (and the amp bypassed) leaves the engine bit exact") {
    GigRigEngine* e = gigrig_create();
    REQUIRE(e != nullptr);
    REQUIRE(gigrig_prepare(e, 96000.0, 32) == GIGRIG_OK);
    gigrig_set_param(e, GIGRIG_PARAM_AMP_ON, 0.0f);  // the amp is on by default
    const auto in = musicalSignal(9600, 96000.0);
    std::vector<float> out(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += 32) {
        gigrig_process(e, in.data() + pos, out.data() + pos, 32);
    }
    CHECK(out == in);
    gigrig_destroy(e);
}

TEST_CASE("pre fx: switching a pedal on and off fades, then returns to bit exact") {
    const double fs = 48000.0;
    auto params = std::make_unique<ParameterValues>();
    PreFx fx;
    fx.prepare(fs, 64, *params);
    const auto in = musicalSignal(48000, fs);
    std::vector<float> out = in;
    params->set(GIGRIG_PARAM_GOLDOD_ON, 1.0f);
    params->set(GIGRIG_PARAM_GOLDOD_GAIN, 8.0f);
    double maxStep = 0.0;
    for (std::size_t pos = 0; pos < in.size(); pos += 64) {
        if (pos == 24000) {
            params->set(GIGRIG_PARAM_GOLDOD_ON, 0.0f);
        }
        fx.process(out.data() + pos, 64, *params);
    }
    // The first fade sample is a small step from the dry signal, never a jump.
    maxStep = std::fabs(static_cast<double>(out[0] - in[0]));
    CHECK(maxStep < 0.05);
    // 10 ms after switching off, the output is the input again, exactly.
    const std::size_t settled = 24000 + static_cast<std::size_t>(fs * BypassFade::kFadeSeconds) + 64;
    CHECK(std::equal(out.begin() + static_cast<long>(settled), out.end(),
                     in.begin() + static_cast<long>(settled)));
    // While on, it really is a different signal.
    CHECK(maxDifference(std::vector<float>(out.begin() + 4800, out.begin() + 9600),
                        std::vector<float>(in.begin() + 4800, in.begin() + 9600)) > 0.05);
}

TEST_CASE("pre fx: drive pedals add no lookahead (an impulse answers at sample 0)") {
    for (double fs : kTestRates) {
        Boost::Settings b;
        b.on = true;
        GoldOd::Settings g;
        g.on = true;
        TwinOd::Settings t;
        t.on = true;
        EnvFilter::Settings e;
        e.on = true;
        const auto x = impulse(64, 0.01f);
        CHECK(renderPedal<Boost>(fs, b, x)[0] != 0.0f);
        CHECK(renderPedal<GoldOd>(fs, g, x)[0] != 0.0f);
        CHECK(renderPedal<TwinOd>(fs, t, x)[0] != 0.0f);
        CHECK(renderPedal<EnvFilter>(fs, e, x)[0] != 0.0f);
    }
}

// ---------------------------------------------------------------- Boost

TEST_CASE("boost: clean mode is a flat gain of Volume dB at every rate") {
    for (double fs : kTestRates) {
        Boost::Settings s;
        s.on = true;
        s.volumeDb = 12.0f;
        for (double f : {200.0, 1000.0, 5000.0}) {
            CHECK_NEAR(smallSignalGainDb<Boost>(fs, s, f), 12.0, 0.1);
        }
    }
}

TEST_CASE("boost: clean mode keeps guitar levels clean, overdrive mode clips them") {
    const double fs = 48000.0;
    Boost::Settings s;
    s.on = true;
    s.volumeDb = 12.0f;
    const auto x = sine(48000, 220.0, fs, 0.1);
    auto thd = [&](const std::vector<float>& y) {
        const double h1 = toneAmplitude(y.data() + 24000, 24000, 220.0, fs);
        double h = 0.0;
        for (int k = 2; k <= 6; ++k) {
            const double a = toneAmplitude(y.data() + 24000, 24000, 220.0 * k, fs);
            h += a * a;
        }
        return std::sqrt(h) / h1;
    };
    const double cleanThd = thd(renderPedal<Boost>(fs, s, x));
    s.mode = Boost::Overdrive;
    const auto od = renderPedal<Boost>(fs, s, x);
    const double odThd = thd(od);
    std::printf("    clean THD %.4f%%, overdrive THD %.1f%%\n", cleanThd * 100.0, odThd * 100.0);
    CHECK(cleanThd < 0.01);
    CHECK(odThd > 0.05);
    // The asymmetric curve makes a strong second harmonic.
    CHECK(toneAmplitude(od.data() + 24000, 24000, 440.0, fs) >
          0.01 * toneAmplitude(od.data() + 24000, 24000, 220.0, fs));
}

TEST_CASE("boost: block size independence, including a mode switch") {
    Boost::Settings s;
    s.on = true;
    s.mode = Boost::Overdrive;
    s.volumeDb = 15.0f;
    checkPedalBlockSizes<Boost>(48000.0, s, musicalSignal(24000, 48000.0));
}

TEST_CASE("boost: overdrive aliasing at full Volume stays below -60 dB") {
    for (double fs : kTestRates) {
        Boost::Settings s;
        s.on = true;
        s.mode = Boost::Overdrive;
        s.volumeDb = 20.0f;
        const double db = pedalAliasingDb<Boost>(fs, s, 2400.0, 0.5f);
        std::printf("    %6.0f Hz: %6.1f dB\n", fs, db);
        CHECK(db < -60.0);
    }
}

// ---------------------------------------------------------------- Env Filter

TEST_CASE("env filter: rests at the bottom of its range and opens with level (Up)") {
    for (double fs : kTestRates) {
        EnvFilter::Settings s;
        s.on = true;
        auto st = makeStage<EnvFilter>(fs, 64, s);
        std::vector<float> silence(64, 0.0f), out(64);
        st->process(silence.data(), out.data(), 64);
        CHECK_NEAR(st->pedal.currentCutoff(), EnvFilter::kLoRange[0], 1e-9);
        const auto loud = sine(static_cast<std::size_t>(fs * 0.05), 330.0, fs, 0.3);
        std::vector<float> y(loud.size());
        st->process(loud.data(), y.data(), static_cast<int>(loud.size()) > 64 ? 64 : 64);
        for (std::size_t pos = 64; pos + 64 <= loud.size(); pos += 64) {
            st->process(loud.data() + pos, y.data() + pos, 64);
        }
        CHECK(st->pedal.currentCutoff() > 800.0);
        CHECK(st->pedal.currentCutoff() <= EnvFilter::kLoRange[1]);
    }
}

TEST_CASE("env filter: Down sweeps the other way, Hi range sits higher") {
    const double fs = 48000.0;
    EnvFilter::Settings s;
    s.on = true;
    s.direction = EnvFilter::Down;
    auto st = makeStage<EnvFilter>(fs, 64, s);
    std::vector<float> silence(64, 0.0f), out(64);
    st->process(silence.data(), out.data(), 64);
    CHECK_NEAR(st->pedal.currentCutoff(), EnvFilter::kLoRange[1], 1e-6);
    s.direction = EnvFilter::Up;
    s.range = EnvFilter::Hi;
    auto hi = makeStage<EnvFilter>(fs, 64, s);
    hi->process(silence.data(), out.data(), 64);
    CHECK_NEAR(hi->pedal.currentCutoff(), EnvFilter::kHiRange[0], 1e-6);
}

TEST_CASE("env filter: sensitivity and resonance maps are monotonic and bounded") {
    double last = -1.0;
    for (float g = 0.0f; g <= 10.0f; g += 1.0f) {
        const double p = EnvFilter::sweepPosition(0.1, g);
        CHECK(p > last);
        CHECK(p >= 0.0 && p < 1.0);
        last = p;
    }
    CHECK(EnvFilter::sweepPosition(0.0, 10.0f) == 0.0);
    CHECK_NEAR(EnvFilter::resonance(0.0f), 0.7, 1e-12);
    CHECK_NEAR(EnvFilter::resonance(10.0f), 6.0, 1e-12);
}

TEST_CASE("env filter: Slow release keeps the filter open longer") {
    const double fs = 48000.0;
    auto cutoffAfterRelease = [&](int speed) {
        EnvFilter::Settings s;
        s.on = true;
        s.speed = speed;
        auto st = makeStage<EnvFilter>(fs, 64, s);
        auto x = sine(9600, 330.0, fs, 0.3);
        x.resize(9600 + 4800, 0.0f);  // 100 ms of silence after the note
        std::vector<float> y(x.size());
        for (std::size_t pos = 0; pos < x.size(); pos += 64) {
            st->process(x.data() + pos, y.data() + pos, 64);
        }
        return st->pedal.currentCutoff();
    };
    CHECK(cutoffAfterRelease(EnvFilter::Slow) > 1.5 * cutoffAfterRelease(EnvFilter::Fast));
}

TEST_CASE("env filter: block size independence") {
    EnvFilter::Settings s;
    s.on = true;
    s.peak = 9.0f;
    s.gain = 7.0f;
    checkPedalBlockSizes<EnvFilter>(48000.0, s, musicalSignal(24000, 48000.0));
}

// ---------------------------------------------------------------- Gold OD

TEST_CASE("gold od: Level is an exact output gain and Treble tilts the top end") {
    for (double fs : kTestRates) {
        GoldOd::Settings s;
        s.on = true;
        s.gain = 0.0f;
        const double flat1k = smallSignalGainDb<GoldOd>(fs, s, 1000.0);
        s.levelDb = -6.0f;
        CHECK_NEAR(smallSignalGainDb<GoldOd>(fs, s, 1000.0), flat1k - 6.0, 0.02);
        s.levelDb = 0.0f;
        s.treble = 10.0f;
        const double bright = smallSignalGainDb<GoldOd>(fs, s, 6000.0);
        s.treble = 0.0f;
        const double dark = smallSignalGainDb<GoldOd>(fs, s, 6000.0);
        CHECK(bright - dark > 14.0);
        CHECK(bright - dark < 2.0 * GoldOd::kTrebleRangeDb + 0.1);
    }
}

TEST_CASE("gold od: clean path keeps low gain settings near unity, gain adds drive") {
    const double fs = 48000.0;
    GoldOd::Settings s;
    s.on = true;
    s.gain = 0.0f;
    // Low gain: a fattened clean boost, within a few dB of unity in the mids.
    const double low = smallSignalGainDb<GoldOd>(fs, s, 700.0);
    CHECK(low > 0.0 && low < 8.0);
    // High gain: much louder small-signal mids (the clipped path dominates).
    s.gain = 10.0f;
    CHECK(smallSignalGainDb<GoldOd>(fs, s, 700.0) > low + 15.0);
    // Bass stays mostly in the clean path, so it gains far less.
    CHECK(smallSignalGainDb<GoldOd>(fs, s, 80.0) < smallSignalGainDb<GoldOd>(fs, s, 700.0) - 6.0);
}

TEST_CASE("gold od: block size independence and aliasing") {
    GoldOd::Settings s;
    s.on = true;
    s.gain = 8.0f;
    s.treble = 7.0f;
    checkPedalBlockSizes<GoldOd>(48000.0, s, musicalSignal(24000, 48000.0));
    for (double fs : kTestRates) {
        s.gain = 10.0f;
        const double db = pedalAliasingDb<GoldOd>(fs, s, 2400.0, 0.5f);
        std::printf("    %6.0f Hz: %6.1f dB\n", fs, db);
        CHECK(db < -60.0);
    }
}

// ---------------------------------------------------------------- Twin OD

TEST_CASE("twin od: mid mode has the mid hump and tight bass, amp mode is flatter") {
    for (double fs : kTestRates) {
        TwinOd::Settings s;
        s.on = true;
        s.gain = 5.0f;
        s.tone = 10.0f;
        const double midHump =
            smallSignalGainDb<TwinOd>(fs, s, 1500.0) - smallSignalGainDb<TwinOd>(fs, s, 100.0);
        s.mode = TwinOd::Amp;
        const double ampHump =
            smallSignalGainDb<TwinOd>(fs, s, 1500.0) - smallSignalGainDb<TwinOd>(fs, s, 100.0);
        CHECK(midHump > 12.0);
        CHECK(ampHump < midHump - 6.0);
    }
}

TEST_CASE("twin od: Tone sweeps the high cut and Level is an exact gain") {
    const double fs = 96000.0;
    TwinOd::Settings s;
    s.on = true;
    s.gain = 0.0f;
    s.tone = 0.0f;
    const double dark = smallSignalGainDb<TwinOd>(fs, s, 4000.0);
    s.tone = 10.0f;
    const double bright = smallSignalGainDb<TwinOd>(fs, s, 4000.0);
    CHECK(bright - dark > 10.0);
    const double ref = smallSignalGainDb<TwinOd>(fs, s, 1000.0);
    s.levelDb = 6.0f;
    CHECK_NEAR(smallSignalGainDb<TwinOd>(fs, s, 1000.0), ref + 6.0, 0.02);
    CHECK_NEAR(TwinOd::toneCutoff(0.0f), 600.0, 1e-9);
    CHECK_NEAR(TwinOd::toneCutoff(10.0f), 8000.0, 1e-6);
}

TEST_CASE("twin od: block size independence (both modes) and aliasing") {
    for (int mode : {TwinOd::Mid, TwinOd::Amp}) {
        TwinOd::Settings s;
        s.on = true;
        s.mode = mode;
        s.gain = 9.0f;
        checkPedalBlockSizes<TwinOd>(48000.0, s, musicalSignal(24000, 48000.0));
        for (double fs : kTestRates) {
            s.gain = 10.0f;
            const double db = pedalAliasingDb<TwinOd>(fs, s, 2400.0, 0.5f);
            std::printf("    mode %d, %6.0f Hz: %6.1f dB\n", mode, fs, db);
            CHECK(db < -70.0);
        }
    }
}

// ---------------------------------------------------------------- Bucket Delay

TEST_CASE("bucket delay: dry is untouched and the echo lands at Time, at every rate") {
    for (double fs : kTestRates) {
        BucketDelay::Settings s;
        s.on = true;
        s.timeMs = 120.0f;
        s.feedbackPercent = 0.0f;
        s.blendPercent = 100.0f;
        const auto x = impulse(static_cast<std::size_t>(fs * 0.3));
        const auto y = renderPedal<BucketDelay>(fs, s, x);
        CHECK(y[0] == x[0]);
        std::size_t peak = 1;
        for (std::size_t i = 1; i < y.size(); ++i) {
            if (std::fabs(y[i]) > std::fabs(y[peak])) peak = i;
        }
        const double expected = 0.12 * fs;
        // The chip's two low-pass filters add a little group delay, under 0.2 ms.
        CHECK(static_cast<double>(peak) >= expected);
        CHECK(static_cast<double>(peak) < expected + 0.0002 * fs + 2.0);
        // No second echo without feedback.
        double late = 0.0;
        for (std::size_t i = static_cast<std::size_t>(expected * 1.6); i < y.size(); ++i) {
            late = std::max(late, static_cast<double>(std::fabs(y[i])));
        }
        CHECK(late < 1.0e-3 * static_cast<double>(std::fabs(y[peak])));
    }
}

TEST_CASE("bucket delay: longer times are darker") {
    CHECK(BucketDelay::bandwidthHz(40.0) > BucketDelay::bandwidthHz(300.0));
    CHECK(BucketDelay::bandwidthHz(300.0) > BucketDelay::bandwidthHz(800.0));
    CHECK(BucketDelay::bandwidthHz(800.0) >= 2000.0);
}

TEST_CASE("bucket delay: repeats decay with feedback and self-oscillation stays bounded") {
    const double fs = 48000.0;
    BucketDelay::Settings s;
    s.on = true;
    s.timeMs = 100.0f;
    s.feedbackPercent = 50.0f;
    s.blendPercent = 100.0f;
    const auto burst = sine(static_cast<std::size_t>(fs * 4.0), 500.0, fs, 0.3);
    auto x = burst;
    std::fill(x.begin() + 2400, x.end(), 0.0f);  // 50 ms tone burst
    const auto y = renderPedal<BucketDelay>(fs, s, x);
    auto windowPeak = [&](double fromS, double toS) {
        double m = 0.0;
        for (auto i = static_cast<std::size_t>(fromS * fs); i < static_cast<std::size_t>(toS * fs); ++i) {
            m = std::max(m, static_cast<double>(std::fabs(y[i])));
        }
        return m;
    };
    const double first = windowPeak(0.1, 0.16);
    const double second = windowPeak(0.2, 0.26);
    const double third = windowPeak(0.3, 0.36);
    std::printf("    repeats: %.3f %.3f %.3f\n", first, second, third);
    CHECK(second < first && third < second);
    CHECK(second > 0.2 * first);

    // Above 100% the loop runs away only as far as the saturating feedback path allows.
    s.feedbackPercent = 110.0f;
    const auto osc = renderPedal<BucketDelay>(fs, s, x);
    CHECK(allFinite(osc));
    const double tail = rms(osc.data() + static_cast<std::size_t>(3.0 * fs), static_cast<std::size_t>(fs));
    std::printf("    self-oscillation: rms %.3f, peak %.3f\n", tail, maxAbs(osc));
    CHECK(tail > 0.01);
    CHECK(maxAbs(osc) < 2.0);
}

TEST_CASE("bucket delay: switching off lets the repeats ring out, then it goes idle and bit exact") {
    const double fs = 48000.0;
    BucketDelay::Settings s;
    s.on = true;
    s.timeMs = 100.0f;
    s.feedbackPercent = 40.0f;
    s.blendPercent = 50.0f;
    BucketDelay d;
    d.setSettings(s);
    d.prepare(fs, 64);
    auto x = sine(static_cast<std::size_t>(fs * 5.0), 400.0, fs, 0.3);
    std::fill(x.begin() + 2400, x.end(), 0.0f);
    std::vector<float> y = x;
    for (std::size_t pos = 0; pos < x.size(); pos += 64) {
        if (pos == 4800) {
            s.on = false;  // before the first repeat
        }
        d.process(y.data() + pos, 64, s);
    }
    double firstRepeat = 0.0;
    for (std::size_t i = 4800; i < 7200; ++i)
        firstRepeat = std::max(firstRepeat, static_cast<double>(std::fabs(y[i])));
    CHECK(firstRepeat > 0.05);  // trails
    CHECK(d.isIdle());
    // Once idle, new input passes untouched.
    const auto more = musicalSignal(4800, fs);
    auto copy = more;
    d.process(copy.data(), 64, s);
    CHECK(std::equal(copy.begin(), copy.begin() + 64, more.begin()));
}

TEST_CASE("bucket delay: block size independence while Time moves") {
    BucketDelay::Settings s;
    s.on = true;
    s.timeMs = 30.0f;
    s.feedbackPercent = 70.0f;
    s.blendPercent = 50.0f;
    // Time glides from the prepared value to a new one inside the test signal.
    auto make = [s] {
        auto st = makeStage<BucketDelay>(48000.0, 512, s);
        st->settings.timeMs = 45.0f;
        return st;
    };
    auto process = [](auto& st, const float* x, float* y, int n) { st->process(x, y, n); };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, 48000.0));
}

// ---------------------------------------------------------------- CPU

TEST_CASE("pre fx benchmark: 96 kHz, 32 samples, every pedal on") {
    // Prints the cost so it can be tracked; Debug and sanitiser builds are much slower,
    // so the only assertion is a very loose real-time bound in optimised builds.
    const double fs = 96000.0;
    auto params = std::make_unique<ParameterValues>();
    for (int id : {GIGRIG_PARAM_BOOST_ON, GIGRIG_PARAM_ENVFILTER_ON, GIGRIG_PARAM_GOLDOD_ON,
                   GIGRIG_PARAM_TWINOD_ON, GIGRIG_PARAM_BUCKETDELAY_ON}) {
        params->set(id, 1.0f);
    }
    PreFx fx;
    fx.prepare(fs, 32, *params);
    const auto in = musicalSignal(static_cast<std::size_t>(fs * 2.0), fs);
    std::vector<float> buf = in;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos + 32 <= buf.size(); pos += 32) {
        fx.process(buf.data() + pos, 32, *params);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double load = seconds / 2.0;
    std::printf("    all five pedals: %.2f%% of one core in real time\n", load * 100.0);
#ifdef NDEBUG
    CHECK(load < 0.25);
#endif
}
