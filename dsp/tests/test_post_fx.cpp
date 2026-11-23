// test_post_fx.cpp - work package 12: Digital Delay and Studio Reverb.
#include <gigrig/gigrig.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "blocks/biquad.hpp"
#include "dsp_test_utils.hpp"
#include "effects/post_fx.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

template <class Fx>
struct FxStage {
    Fx fx;
    typename Fx::Settings settings;

    FxStage(double fs, int maxBlock, const typename Fx::Settings& s) : settings(s) {
        fx.setSettings(s);
        fx.prepare(fs, maxBlock);
    }

    void process(const float* in, float* out, int n) noexcept {
        for (int i = 0; i < n; ++i) out[i] = in[i];
        fx.process(out, n, settings);
    }
};

template <class Fx>
std::vector<float> render(double fs, const typename Fx::Settings& s, const std::vector<float>& in,
                          int block = 64) {
    FxStage<Fx> st(fs, block, s);
    std::vector<float> out(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t>(block)) {
        const int n =
            static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), in.size() - pos));
        st.process(in.data() + pos, out.data() + pos, n);
    }
    return out;
}

std::vector<float> impulse(std::size_t n, float amplitude = 0.5f) {
    std::vector<float> x(n, 0.0f);
    x[0] = amplitude;
    return x;
}

std::size_t peakIndex(const std::vector<float>& y, std::size_t from, std::size_t to) {
    std::size_t best = from;
    for (std::size_t i = from; i < std::min(to, y.size()); ++i) {
        if (std::fabs(y[i]) > std::fabs(y[best])) best = i;
    }
    return best;
}

double peakIn(const std::vector<float>& y, double fs, double fromS, double toS) {
    double m = 0.0;
    for (auto i = static_cast<std::size_t>(fromS * fs);
         i < std::min(y.size(), static_cast<std::size_t>(toS * fs)); ++i) {
        m = std::max(m, static_cast<double>(std::fabs(y[i])));
    }
    return m;
}

// RT60 from the Schroeder backward-integrated energy of an impulse response,
// fitted between -5 and -25 dB and extrapolated to 60 dB.
double rt60(const std::vector<float>& h, double fs) {
    std::vector<double> e(h.size() + 1, 0.0);
    for (std::size_t i = h.size(); i-- > 0;) {
        e[i] = e[i + 1] + static_cast<double>(h[i]) * static_cast<double>(h[i]);
    }
    const double total = e[0];
    auto timeAt = [&](double db) {
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (10.0 * std::log10(e[i] / total) <= db) return static_cast<double>(i) / fs;
        }
        return static_cast<double>(h.size()) / fs;
    };
    return 3.0 * (timeAt(-25.0) - timeAt(-5.0));
}

}  // namespace

// ---------------------------------------------------------------- Digital Delay

TEST_CASE("digital delay: dry untouched, echo exactly at Time, at every rate") {
    for (double fs : kTestRates) {
        DigitalDelay::Settings s;
        s.on = true;
        s.timeMs = 250.0f;
        s.feedbackPercent = 0.0f;
        s.mixPercent = 100.0f;
        s.hardness = 10.0f;
        const auto x = impulse(static_cast<std::size_t>(fs * 0.6));
        const auto y = render<DigitalDelay>(fs, s, x);
        CHECK(y[0] == x[0]);
        const auto expected = static_cast<std::size_t>(std::lround(0.25 * fs));
        CHECK(peakIndex(y, 1, y.size()) == expected);
        CHECK(peakIn(y, fs, 0.3, 0.6) < 1.0e-6);  // no second echo without feedback
    }
}

TEST_CASE("digital delay: tempo sync follows the tempo and note value") {
    DigitalDelay::Settings s;
    s.sync = DigitalDelay::Tempo;
    s.tempoBpm = 120.0f;
    s.division = 3;  // dotted 1/8
    CHECK_NEAR(DigitalDelay::effectiveTimeMs(s), 375.0, 1e-9);
    s.division = 2;
    s.tempoBpm = 90.0f;
    CHECK_NEAR(DigitalDelay::effectiveTimeMs(s), 666.6666667, 1e-6);
    s.division = 0;
    s.tempoBpm = 40.0f;  // 3 s would exceed the line: clamped to 2 s
    CHECK_NEAR(DigitalDelay::effectiveTimeMs(s), DigitalDelay::kMaxTimeMs, 1e-9);
    const double fs = 96000.0;
    s.on = true;
    s.tempoBpm = 120.0f;
    s.division = 3;
    s.feedbackPercent = 0.0f;
    s.mixPercent = 100.0f;
    const auto y = render<DigitalDelay>(fs, s, impulse(static_cast<std::size_t>(fs * 0.5)));
    CHECK(peakIndex(y, 1, y.size()) == static_cast<std::size_t>(0.375 * fs));
}

TEST_CASE("digital delay: Feedback sets the repeat level, Hardness darkens repeats") {
    const double fs = 48000.0;
    auto repeatRatio = [&](double toneHz, float hardness) {
        DigitalDelay::Settings s;
        s.on = true;
        s.timeMs = 200.0f;
        s.feedbackPercent = 50.0f;
        s.mixPercent = 100.0f;
        s.hardness = hardness;
        auto x = sine(static_cast<std::size_t>(fs), toneHz, fs, 0.2);
        std::fill(x.begin() + 4800, x.end(), 0.0f);  // 100 ms burst
        const auto y = render<DigitalDelay>(fs, s, x);
        return peakIn(y, fs, 0.42, 0.5) / peakIn(y, fs, 0.22, 0.3);
    };
    CHECK_NEAR(repeatRatio(300.0, 10.0f), 0.5, 0.02);
    CHECK(repeatRatio(5000.0, 0.0f) < 0.5 * repeatRatio(5000.0, 10.0f));
}

TEST_CASE("digital delay: moving Time glides without zipper noise or clicks") {
    const double fs = 96000.0;
    DigitalDelay::Settings s;
    s.on = true;
    s.timeMs = 300.0f;
    s.feedbackPercent = 40.0f;
    s.mixPercent = 100.0f;
    FxStage<DigitalDelay> st(fs, 32, s);
    const auto x = sine(static_cast<std::size_t>(fs * 2.0), 440.0, fs, 0.2);
    std::vector<float> y(x.size());
    for (std::size_t pos = 0; pos < x.size(); pos += 32) {
        if (pos % 4800 == 0 && pos >= 48000) {
            // The knob is turned in steps every 50 ms, as a UI drag delivers it.
            st.settings.timeMs = st.settings.timeMs > 200.0f ? st.settings.timeMs - 40.0f : 600.0f;
        }
        st.process(x.data() + pos, y.data() + pos, 32);
    }
    // The wet part is the output minus the dry input. A click or zipper step
    // shows as a sample-to-sample jump far above a 440 Hz tone's own.
    double maxStep = 0.0;
    for (std::size_t i = 48000; i < y.size(); ++i) {
        const double w1 = static_cast<double>(y[i] - x[i]);
        const double w0 = static_cast<double>(y[i - 1] - x[i - 1]);
        maxStep = std::max(maxStep, std::fabs(w1 - w0));
    }
    // Repeats up to about 0.33 (0.2 / (1 - 0.4)), a 440 Hz step of 2 pi f / fs per unit,
    // with room for the glide's pitch change.
    const double toneStep = 0.34 * 2.0 * kPi * 440.0 / fs;
    std::printf("    max wet step %.5f (tone alone %.5f)\n", maxStep, toneStep);
    CHECK(maxStep < 2.0 * toneStep);
}

TEST_CASE("digital delay: trails ring out after switching off, then idle and bit exact") {
    const double fs = 48000.0;
    DigitalDelay::Settings s;
    s.on = true;
    s.timeMs = 100.0f;
    s.feedbackPercent = 50.0f;
    s.mixPercent = 50.0f;
    FxStage<DigitalDelay> st(fs, 64, s);
    auto x = sine(static_cast<std::size_t>(fs * 6.0), 400.0, fs, 0.3);
    std::fill(x.begin() + 2400, x.end(), 0.0f);
    std::vector<float> y(x.size());
    for (std::size_t pos = 0; pos < x.size(); pos += 64) {
        if (pos == 3200) st.settings.on = false;
        st.process(x.data() + pos, y.data() + pos, 64);
    }
    CHECK(peakIn(y, fs, 0.1, 0.15) > 0.05);
    CHECK(st.fx.isIdle());
    const auto more = musicalSignal(64, fs);
    std::vector<float> out(64);
    st.process(more.data(), out.data(), 64);
    CHECK(out == more);
}

TEST_CASE("digital delay: block size independence while Time, Sync and Hardness move") {
    DigitalDelay::Settings s;
    s.on = true;
    s.timeMs = 40.0f;
    s.feedbackPercent = 70.0f;
    s.mixPercent = 50.0f;
    s.hardness = 2.0f;
    auto make = [s] {
        auto st = std::make_unique<FxStage<DigitalDelay>>(48000.0, 512, s);
        st->settings.sync = DigitalDelay::Tempo;
        st->settings.tempoBpm = 240.0f;
        st->settings.division = 6;  // 62.5 ms
        st->settings.hardness = 9.0f;
        return st;
    };
    auto process = [](auto& st, const float* in, float* out, int n) { st->process(in, out, n); };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, 48000.0));
}

// ---------------------------------------------------------------- Studio Reverb

TEST_CASE("studio reverb: decay time matches the Decay setting (hall and plate, 48 and 96 kHz)") {
    for (double fs : {48000.0, 96000.0}) {
        for (int mode : {StudioReverb::Hall, StudioReverb::Plate}) {
            for (float decay : {1.0f, 2.5f, 5.0f}) {
                StudioReverb::Settings s;
                s.on = true;
                s.mode = mode;
                s.decaySeconds = decay;
                s.mixPercent = 100.0f;
                s.preDelayMs = 0.0f;
                s.lowCutHz = 20.0f;
                s.highCutHz = 20000.0f;
                const auto x =
                    impulse(static_cast<std::size_t>(fs * (static_cast<double>(decay) * 1.5 + 0.5)));
                auto y = render<StudioReverb>(fs, s, x);
                y[0] -= x[0];  // keep only the reverb
                // Decay is the low and mid frequency decay time (highs die faster by
                // design), so it is measured below 1 kHz.
                Biquad lp1, lp2;
                lp1.setCoefficients(BiquadCoefficients::lowPass(fs, 1000.0, 0.7071));
                lp2.setCoefficients(BiquadCoefficients::lowPass(fs, 1000.0, 0.7071));
                for (float& v : y) v = lp2.process(lp1.process(v));
                const double t = rt60(y, fs);
                std::printf("    %s %6.0f Hz: Decay %.1f s -> RT60 %.2f s\n", mode == 0 ? "hall " : "plate",
                            fs, static_cast<double>(decay), t);
                CHECK(t > 0.8 * static_cast<double>(decay));
                CHECK(t < 1.25 * static_cast<double>(decay));
            }
        }
    }
}

TEST_CASE("studio reverb: dry untouched, reverb starts after the pre-delay") {
    for (double fs : kTestRates) {
        for (int mode : {StudioReverb::Hall, StudioReverb::Plate}) {
            StudioReverb::Settings s;
            s.on = true;
            s.mode = mode;
            s.mixPercent = 100.0f;
            s.preDelayMs = 50.0f;
            const auto x = impulse(static_cast<std::size_t>(fs * 0.3));
            const auto y = render<StudioReverb>(fs, s, x);
            CHECK(y[0] == x[0]);
            const auto pre = static_cast<std::size_t>(0.05 * fs);
            double before = 0.0;
            for (std::size_t i = 1; i < pre; ++i)
                before = std::max(before, static_cast<double>(std::fabs(y[i])));
            CHECK(before == 0.0);
            CHECK(peakIn(y, fs, 0.05, 0.3) > 1.0e-3);
        }
    }
}

TEST_CASE("studio reverb: Low Cut and High Cut shape what enters the reverb") {
    const double fs = 48000.0;
    auto wetLevel = [&](double toneHz, float lowCut, float highCut) {
        StudioReverb::Settings s;
        s.on = true;
        s.mixPercent = 100.0f;
        s.lowCutHz = lowCut;
        s.highCutHz = highCut;
        const auto x = sine(static_cast<std::size_t>(fs), toneHz, fs, 0.2);
        const auto y = render<StudioReverb>(fs, s, x);
        std::vector<float> wet(y.size());
        for (std::size_t i = 0; i < y.size(); ++i) wet[i] = y[i] - x[i];
        return rms(wet, 24000);
    };
    CHECK(wetLevel(100.0, 800.0f, 20000.0f) < 0.1 * wetLevel(100.0, 20.0f, 20000.0f));
    CHECK(wetLevel(8000.0, 20.0f, 1500.0f) < 0.1 * wetLevel(8000.0, 20.0f, 20000.0f));
}

TEST_CASE("studio reverb: longest decay stays stable and bounded on loud noise") {
    for (int mode : {StudioReverb::Hall, StudioReverb::Plate}) {
        StudioReverb::Settings s;
        s.on = true;
        s.mode = mode;
        s.decaySeconds = 10.0f;
        s.mixPercent = 100.0f;
        s.highCutHz = 20000.0f;
        s.lowCutHz = 20.0f;
        auto x = noise(static_cast<std::size_t>(96000.0 * 4.0), 0.5);
        std::fill(x.begin() + 192000, x.end(), 0.0f);
        const auto y = render<StudioReverb>(96000.0, s, x);
        CHECK(allFinite(y));
        std::printf("    %s: peak %.2f\n", mode == 0 ? "hall " : "plate", maxAbs(y));
        CHECK(maxAbs(y) < 4.0);
        CHECK(peakIn(y, 96000.0, 3.5, 4.0) > 1.0e-3);  // still ringing 1.5 s after the noise stops
    }
}

TEST_CASE("studio reverb: mode change and switching off keep the tail, then idle and bit exact") {
    const double fs = 48000.0;
    StudioReverb::Settings s;
    s.on = true;
    s.decaySeconds = 1.0f;
    s.mixPercent = 50.0f;
    FxStage<StudioReverb> st(fs, 64, s);
    auto x = musicalSignal(static_cast<std::size_t>(fs * 6.0), fs);
    std::fill(x.begin() + 24000, x.end(), 0.0f);
    std::vector<float> y(x.size());
    for (std::size_t pos = 0; pos < x.size(); pos += 64) {
        if (pos == 12032) st.settings.mode = StudioReverb::Plate;
        if (pos == 24000) st.settings.on = false;
        st.process(x.data() + pos, y.data() + pos, 64);
    }
    CHECK(peakIn(y, fs, 0.6, 0.8) > 1.0e-3);  // tail after switching off
    CHECK(st.fx.isIdle());
    const auto more = musicalSignal(64, fs);
    std::vector<float> out(64);
    st.process(more.data(), out.data(), 64);
    CHECK(out == more);
}

TEST_CASE("studio reverb: block size independence while every control moves") {
    for (int mode : {StudioReverb::Hall, StudioReverb::Plate}) {
        StudioReverb::Settings s;
        s.on = true;
        s.mode = mode;
        auto make = [s] {
            auto st = std::make_unique<FxStage<StudioReverb>>(48000.0, 512, s);
            st->settings.preDelayMs = 80.0f;
            st->settings.decaySeconds = 6.0f;
            st->settings.lowCutHz = 300.0f;
            st->settings.highCutHz = 3000.0f;
            st->settings.mixPercent = 60.0f;
            st->settings.mode = 1 - st->settings.mode;  // and the mode switches
            return st;
        };
        auto process = [](auto& st, const float* in, float* out, int n) { st->process(in, out, n); };
        checkBlockSizeIndependence(make, process, musicalSignal(24000, 48000.0));
    }
}

// ---------------------------------------------------------------- whole section

TEST_CASE("post fx: whole engine is block size independent with every block on, at every rate") {
    for (double fs : kTestRates) {
        auto make = [fs] {
            GigRigEngine* e = gigrig_create();
            for (int id : {GIGRIG_PARAM_BOOST_ON, GIGRIG_PARAM_ENVFILTER_ON, GIGRIG_PARAM_GOLDOD_ON,
                           GIGRIG_PARAM_TWINOD_ON, GIGRIG_PARAM_BUCKETDELAY_ON, GIGRIG_PARAM_EQ_ON,
                           GIGRIG_PARAM_DELAY_ON, GIGRIG_PARAM_REVERB_ON}) {
                gigrig_set_param(e, id, 1.0f);
            }
            gigrig_set_param(e, GIGRIG_PARAM_EQ_MID1_GAIN, 6.0f);
            gigrig_prepare(e, fs, 512);
            return std::unique_ptr<GigRigEngine, void (*)(GigRigEngine*)>(e, gigrig_destroy);
        };
        auto process = [](auto& e, const float* in, float* out, int n) {
            gigrig_process(e.get(), in, out, n);
        };
        checkBlockSizeIndependence(make, process, musicalSignal(static_cast<std::size_t>(fs * 0.25), fs));
    }
}

TEST_CASE("post fx benchmark: 96 kHz, 32 samples, delay and reverb on (hall, then plate)") {
    const double fs = 96000.0;
    for (int mode : {0, 1}) {
        auto params = std::make_unique<ParameterValues>();
        params->set(GIGRIG_PARAM_DELAY_ON, 1.0f);
        params->set(GIGRIG_PARAM_REVERB_ON, 1.0f);
        params->set(GIGRIG_PARAM_REVERB_MODE, static_cast<float>(mode));
        PostFx fx;
        fx.prepare(fs, 32, *params);
        auto buf = musicalSignal(static_cast<std::size_t>(fs * 2.0), fs);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos + 32 <= buf.size(); pos += 32) {
            fx.process(buf.data() + pos, 32, *params);
        }
        const double load =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 2.0;
        std::printf("    delay + %s: %.2f%% of one core in real time\n", mode == 0 ? "hall" : "plate",
                    load * 100.0);
#ifdef NDEBUG
        CHECK(load < 0.25);
#endif
    }
}
