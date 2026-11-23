// test_cabinet.cpp - work package 3: FFT, zero-latency convolver, IR preparation,
// room, and the Cab section in the engine.
#include <gigrig/gigrig.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "blocks/fft.hpp"
#include "cab/cabinet.hpp"
#include "cab/convolver.hpp"
#include "cab/impulse_response.hpp"
#include "cab/room.hpp"
#include "cab_model.hpp"
#include "dsp_test_utils.hpp"
#include "engine_handle.hpp"
#include "wav.hpp"

using namespace gigrig;
using namespace gigrig::test;

namespace {

struct EngineDeleter {
    void operator()(GigRigEngine* e) const { gigrig_destroy(e); }
};
using EnginePtr = std::unique_ptr<GigRigEngine, EngineDeleter>;

EnginePtr makeEngine(double sampleRate, int maxBlock) {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    // The cabinet is checked on its own, so the amp in front of it is bypassed.
    gigrig_set_param(e.get(), GIGRIG_PARAM_AMP_ON, 0.0f);
    REQUIRE(gigrig_prepare(e.get(), sampleRate, maxBlock) == GIGRIG_OK);
    return e;
}

// A cabinet-like test IR: a few decaying resonances with an onset after `delay` samples.
std::vector<float> testIr(std::size_t n, double sampleRate, std::size_t delay = 0) {
    std::vector<float> h(n, 0.0f);
    const double modes[][3] = {{110.0, 0.9, 30.0}, {1800.0, 0.6, 120.0}, {3500.0, 0.4, 300.0}};
    for (std::size_t i = delay; i < n; ++i) {
        const double t = static_cast<double>(i - delay) / sampleRate;
        double v = 0.0;
        for (const auto& m : modes) {
            v += m[1] * std::sin(kTwoPi * m[0] * t) * std::exp(-m[2] * t);
        }
        h[i] = static_cast<float>(v + (i == delay ? 0.5 : 0.0));
    }
    return h;
}

std::vector<double> directConvolution(const std::vector<float>& x, const std::vector<float>& h) {
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
        double acc = 0.0;
        for (std::size_t k = 0; k < h.size() && k <= n; ++k) {
            acc += static_cast<double>(h[k]) * static_cast<double>(x[n - k]);
        }
        y[n] = acc;
    }
    return y;
}

// Largest error relative to the largest output sample.
double relativeError(const std::vector<float>& got, const std::vector<double>& want) {
    double err = 0.0, peak = 1.0e-30;
    for (std::size_t i = 0; i < want.size(); ++i) {
        err = std::max(err, std::fabs(static_cast<double>(got[i]) - want[i]));
        peak = std::max(peak, std::fabs(want[i]));
    }
    return err / peak;
}

// Runs silence until every cabinet crossfade and gain ramp has finished.
void settle(GigRigEngine* e, double sampleRate) {
    std::vector<float> buf(256, 0.0f);
    const int blocks = static_cast<int>(0.1 * sampleRate / 256.0) + 2;
    for (int i = 0; i < blocks; ++i) {
        gigrig_process(e, buf.data(), buf.data(), 256);
    }
}

std::vector<float> impulseResponseOf(GigRigEngine* e, std::size_t n) {
    std::vector<float> x(n, 0.0f);
    if (!x.empty()) {  // also keeps GCC 13 from a false -Wnull-dereference
        x.front() = 1.0f;
    }
    std::vector<float> y(n, 0.0f);
    gigrig_process(e, x.data(), y.data(), static_cast<int32_t>(n));
    return y;
}

}  // namespace

// ---- FFT ----

TEST_CASE("cab fft: forward matches a direct DFT and inverse returns n times the input") {
    for (int n : {4, 8, 128, 1024}) {
        RealFft fft;
        fft.prepare(n);
        const auto x = noise(static_cast<std::size_t>(n), 1.0, static_cast<std::uint32_t>(n));
        std::vector<float> re(static_cast<std::size_t>(fft.bins())), im(re.size());
        fft.forward(x.data(), re.data(), im.data());
        double err = 0.0;
        for (int k = 0; k < fft.bins(); ++k) {
            double dr = 0.0, di = 0.0;
            for (int i = 0; i < n; ++i) {
                const double w = -kTwoPi * k * i / n;
                dr += static_cast<double>(x[static_cast<std::size_t>(i)]) * std::cos(w);
                di += static_cast<double>(x[static_cast<std::size_t>(i)]) * std::sin(w);
            }
            const auto kk = static_cast<std::size_t>(k);
            err =
                std::max(err, std::hypot(dr - static_cast<double>(re[kk]), di - static_cast<double>(im[kk])));
        }
        CHECK(err < 1.0e-4 * std::sqrt(static_cast<double>(n)));
        std::vector<float> back(static_cast<std::size_t>(n));
        fft.inverse(re.data(), im.data(), back.data());
        double rt = 0.0;
        for (std::size_t i = 0; i < back.size(); ++i) {
            rt = std::max(rt, std::fabs(static_cast<double>(back[i]) / n - static_cast<double>(x[i])));
        }
        CHECK(rt < 1.0e-5);
    }
}

// ---- Convolver ----

TEST_CASE("cab convolver matches direct convolution to within float rounding, for every IR length") {
    const auto x = musicalSignal(24000, 48000.0);
    for (int len : {1, 37, 64, 65, 128, 1000, 1024, 1025, 1600, 5000, 19200}) {
        const auto h = testIr(static_cast<std::size_t>(len), 48000.0);
        Convolver c;
        c.build(h.data(), len);
        std::vector<float> y(x.size());
        c.process(x.data(), y.data(), static_cast<int>(x.size()));
        const double err = relativeError(y, directConvolution(x, h));
        if (!(err < 2.0e-6)) {
            std::fprintf(stderr, "    IR length %d: relative error %.3g\n", len, err);
        }
        CHECK(err < 2.0e-6);
    }
}

TEST_CASE("cab convolver adds zero latency: an impulse returns the IR from the first sample") {
    const auto h = testIr(4000, 48000.0);
    Convolver c;
    c.build(h.data(), static_cast<int>(h.size()));
    std::vector<float> x(5000, 0.0f);
    x[0] = 1.0f;
    std::vector<float> y(x.size());
    c.process(x.data(), y.data(), static_cast<int>(x.size()));
    CHECK(y[0] == h[0]);  // the direct FIR part is exact
    double err = 0.0;
    for (std::size_t i = 0; i < h.size(); ++i) {
        err = std::max(err, std::fabs(static_cast<double>(y[i] - h[i])));
    }
    CHECK(err < 1.0e-6);
    CHECK(std::fabs(y[4500]) < 1.0e-6f);  // and nothing after the IR ends
}

TEST_CASE("cab convolver output is block size independent and never allocates") {
    const auto h = testIr(9000, 96000.0);
    const auto x = musicalSignal(20000, 96000.0);
    checkBlockSizeIndependence(
        [&h] {
            auto c = std::make_unique<Convolver>();
            c->build(h.data(), static_cast<int>(h.size()));
            return c;
        },
        [](std::unique_ptr<Convolver>& c, const float* in, float* out, int n) { c->process(in, out, n); }, x);
}

TEST_CASE("cab convolver reset clears history, empty convolver is silent") {
    const auto h = testIr(3000, 48000.0);
    Convolver c;
    c.build(h.data(), static_cast<int>(h.size()));
    const auto x = noise(2000);
    std::vector<float> y(x.size());
    c.process(x.data(), y.data(), static_cast<int>(x.size()));
    c.reset();
    std::vector<float> silence(4000, 0.0f), out(4000, 1.0f);
    c.process(silence.data(), out.data(), 4000);
    CHECK(maxAbs(out) == 0.0);

    Convolver empty;
    empty.build(nullptr, 0);
    empty.process(x.data(), y.data(), static_cast<int>(x.size()));
    CHECK(maxAbs(y) == 0.0);
}

// ---- IR preparation ----

TEST_CASE("cab IR: a unit impulse at the session rate is left unchanged") {
    for (double fs : kTestRates) {
        const float one = 1.0f;
        const auto ir = prepareIr(makeSourceIr(&one, 1, 1, fs), fs);
        REQUIRE(ir.size() == 1);
        CHECK_NEAR(ir[0], 1.0, 1.0e-6);
    }
}

TEST_CASE("cab IR: channels are averaged, invalid input is refused") {
    const float stereo[] = {1.0f, 0.0f, 0.5f, 0.5f};
    const SourceIr s = makeSourceIr(stereo, 2, 2, 48000.0);
    REQUIRE(s.samples.size() == 2);
    CHECK(s.samples[0] == 0.5f);
    CHECK(s.samples[1] == 0.5f);
    CHECK(makeSourceIr(nullptr, 2, 2, 48000.0).samples.empty());
    CHECK(makeSourceIr(stereo, 0, 2, 48000.0).samples.empty());
    CHECK(makeSourceIr(stereo, 2, 0, 48000.0).samples.empty());
    CHECK(makeSourceIr(stereo, 2, 2, 1000.0).samples.empty());
    const float bad[] = {1.0f, std::nanf("")};
    CHECK(makeSourceIr(bad, 2, 1, 48000.0).samples.empty());
    const float silent[] = {0.0f, 0.0f, 0.0f};
    CHECK(prepareIr(makeSourceIr(silent, 3, 1, 48000.0), 48000.0).empty());
}

TEST_CASE("cab IR: resampling keeps the frequency response at every rate") {
    const auto h = testIr(2400, 48000.0, 3);
    const SourceIr src = makeSourceIr(h.data(), static_cast<int>(h.size()), 1, 48000.0);
    const auto ref = prepareIr(src, 48000.0);
    for (double fs : kTestRates) {
        const auto ir = prepareIr(src, fs);
        CHECK(std::fabs(static_cast<double>(ir.size()) / fs - static_cast<double>(ref.size()) / 48000.0) <
              0.002);
        // Magnitude at a few guitar-band frequencies, in dB, against the 48 kHz original.
        for (double f : {100.0, 400.0, 1800.0, 3500.0}) {
            auto mag = [f](const std::vector<float>& v, double rate) {
                double re = 0.0, im = 0.0;
                for (std::size_t i = 0; i < v.size(); ++i) {
                    re += static_cast<double>(v[i]) * std::cos(kTwoPi * f * static_cast<double>(i) / rate);
                    im -= static_cast<double>(v[i]) * std::sin(kTwoPi * f * static_cast<double>(i) / rate);
                }
                return 20.0 * std::log10(std::hypot(re, im));
            };
            CHECK_NEAR(mag(ir, fs), mag(ref, 48000.0), 0.1);
        }
        // Leading silence is kept in time, so two mics stay aligned: the onset stays at 3/48000 s.
        const double peak = maxAbs(ir);
        std::size_t onset = 0;
        while (onset < ir.size() && std::fabs(static_cast<double>(ir[onset])) < 0.1 * peak) {
            ++onset;
        }
        CHECK(std::fabs(static_cast<double>(onset) / fs - 3.0 / 48000.0) < 2.0 / fs);
    }
}

TEST_CASE("cab IR: long IRs are cut to 200 ms with a fade, silent tails are dropped") {
    for (double fs : kTestRates) {
        const auto longIr = noise(static_cast<std::size_t>(0.5 * fs), 0.5);
        const auto ir = prepareIr(makeSourceIr(longIr.data(), static_cast<int>(longIr.size()), 1, fs), fs);
        const auto cut = static_cast<std::size_t>(std::lround(0.2 * fs));
        CHECK(ir.size() <= cut);
        CHECK(ir.size() > cut - static_cast<std::size_t>(0.005 * fs));  // only the faded end may be trimmed
        CHECK(std::fabs(ir.back()) < 1.0e-3f);
    }
    std::vector<float> padded = testIr(3000, 48000.0);
    padded.resize(9000, 0.0f);
    const auto ir =
        prepareIr(makeSourceIr(padded.data(), static_cast<int>(padded.size()), 1, 48000.0), 48000.0);
    CHECK(ir.size() <= 3000);
}

TEST_CASE("cab IR: normalisation brings quiet and loud IRs to the same band level") {
    auto quiet = testIr(3000, 48000.0);
    auto loud = quiet;
    for (float& v : quiet) {
        v *= 0.01f;
    }
    for (float& v : loud) {
        v *= 20.0f;
    }
    const auto a = prepareIr(makeSourceIr(quiet.data(), 3000, 1, 48000.0), 48000.0);
    const auto b = prepareIr(makeSourceIr(loud.data(), 3000, 1, 48000.0), 48000.0);
    CHECK_NEAR(irBandGain(a, 48000.0), 1.0, 1.0e-4);
    CHECK(maxDifference(a, b) < 1.0e-5);
}

// ---- Room ----

TEST_CASE("cab room decays by its RT60 at every size and rate, and is block size independent") {
    const double rt60[] = {0.30, 0.55, 0.90};
    for (double fs : kTestRates) {
        for (int size = 0; size < Room::kSizes; ++size) {
            Room room;
            room.prepare(fs);
            room.setSize(size);
            for (int i = 0; i < static_cast<int>(0.02 * fs); ++i) {
                room.processSample(0.0f);  // let the size change fade complete
            }
            const auto burst = noise(static_cast<std::size_t>(0.05 * fs), 0.5);
            for (float v : burst) {
                room.processSample(v);
            }
            // Energy over 50 ms windows, one RT60 apart: about 60 dB lower (lowpass makes it faster).
            auto windowEnergy = [&room, fs]() {
                double e = 0.0;
                for (int i = 0; i < static_cast<int>(0.05 * fs); ++i) {
                    const auto v = static_cast<double>(room.processSample(0.0f));
                    e += v * v;
                }
                return e;
            };
            const double e0 = windowEnergy();
            for (int i = 0; i < static_cast<int>((rt60[size] - 0.05) * fs); ++i) {
                room.processSample(0.0f);
            }
            const double e1 = windowEnergy();
            const double dropDb = 10.0 * std::log10(e0 / e1);
            CHECK(dropDb > 50.0);
            CHECK(dropDb < 90.0);
        }
    }
    const auto x = musicalSignal(20000, 48000.0);
    checkBlockSizeIndependence(
        [] {
            auto r = std::make_unique<Room>();
            r->prepare(48000.0);
            return r;
        },
        [](std::unique_ptr<Room>& r, const float* in, float* out, int n) {
            for (int i = 0; i < n; ++i) {
                out[i] = r->processSample(in[i]);
            }
        },
        x);
}

// ---- Cab section in the engine ----

TEST_CASE("cab engine: with no IR loaded the chain stays a bit-exact passthrough") {
    auto e = makeEngine(96000.0, 32);
    const auto x = musicalSignal(4096, 96000.0);
    std::vector<float> y(x.size());
    gigrig_process(e.get(), x.data(), y.data(), static_cast<int32_t>(x.size()));
    CHECK(y == x);
    CHECK(gigrig_cab_ir_length(e.get(), 0) == 0);
}

TEST_CASE("cab engine: a loaded IR plays with zero latency and matches the prepared IR") {
    for (double fs : kTestRates) {
        auto e = makeEngine(fs, 64);
        const auto h = testIr(6000, 48000.0);
        REQUIRE(gigrig_cab_load_ir(e.get(), 0, h.data(), static_cast<int32_t>(h.size()), 1, 48000.0) ==
                GIGRIG_OK);
        settle(e.get(), fs);
        const auto prepared = prepareIr(makeSourceIr(h.data(), static_cast<int>(h.size()), 1, 48000.0), fs);
        CHECK(gigrig_cab_ir_length(e.get(), 0) == static_cast<int32_t>(prepared.size()));
        gigrig_reset(e.get());
        const auto y = impulseResponseOf(e.get(), prepared.size() + 100);
        CHECK(y[0] == prepared[0]);
        double err = 0.0;
        for (std::size_t i = 0; i < prepared.size(); ++i) {
            err = std::max(err, std::fabs(static_cast<double>(y[i] - prepared[i])));
        }
        CHECK(err < 1.0e-5);
        CHECK(gigrig_latency_samples(e.get()) == 0);
    }
}

TEST_CASE("cab engine: level, phase, the second mic and the bypass switch") {
    const double fs = 48000.0;
    auto e = makeEngine(fs, 128);
    const auto h1 = testIr(2000, fs);
    const auto h2 = testIr(1500, fs, 20);
    REQUIRE(gigrig_cab_load_ir(e.get(), 0, h1.data(), 2000, 1, fs) == GIGRIG_OK);
    REQUIRE(gigrig_cab_load_ir(e.get(), 1, h2.data(), 1500, 1, fs) == GIGRIG_OK);
    settle(e.get(), fs);
    gigrig_reset(e.get());
    const auto mic1 = impulseResponseOf(e.get(), 2500);

    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_PHASE, 1.0f);
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_LEVEL, -6.0f);
    settle(e.get(), fs);
    gigrig_reset(e.get());
    const auto inverted = impulseResponseOf(e.get(), 2500);
    const float g = static_cast<float>(-dbToGain(-6.0));
    for (std::size_t i = 0; i < mic1.size(); ++i) {
        CHECK_NEAR(inverted[i], g * mic1[i], 1.0e-6);
    }

    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_ON, 0.0f);
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC2_ON, 1.0f);
    settle(e.get(), fs);
    gigrig_reset(e.get());
    const auto mic2 = impulseResponseOf(e.get(), 2500);
    const auto p2 = prepareIr(makeSourceIr(h2.data(), 1500, 1, fs), fs);
    CHECK(mic2[0] == 0.0f);  // its leading silence is kept
    CHECK_NEAR(mic2[20], p2[20], 1.0e-6);

    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_ON, 0.0f);
    settle(e.get(), fs);
    const auto x = musicalSignal(2048, fs);
    std::vector<float> y(x.size());
    gigrig_process(e.get(), x.data(), y.data(), 2048);
    CHECK(y == x);  // bypassed: bit exact again
}

TEST_CASE("cab engine: room send adds a tail after the dry IR") {
    const double fs = 48000.0;
    auto e = makeEngine(fs, 256);
    const auto h = testIr(500, fs);
    REQUIRE(gigrig_cab_load_ir(e.get(), 0, h.data(), 500, 1, fs) == GIGRIG_OK);
    settle(e.get(), fs);
    gigrig_reset(e.get());
    const auto dry = impulseResponseOf(e.get(), 24000);
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_ROOM, 50.0f);
    settle(e.get(), fs);
    gigrig_reset(e.get());
    const auto wet = impulseResponseOf(e.get(), 24000);
    CHECK(rms(dry, 2000) == 0.0);
    CHECK(rms(wet, 2000) > 1.0e-4);
    CHECK(allFinite(wet));
}

TEST_CASE("cab engine: replaced and cleared IRs are freed off the audio thread, loads queue during a fade") {
    const double fs = 48000.0;
    auto e = makeEngine(fs, 32);
    const auto a = testIr(1000, fs);
    const auto b = testIr(3000, fs);
    REQUIRE(gigrig_cab_load_ir(e.get(), 0, a.data(), 1000, 1, fs) == GIGRIG_OK);
    REQUIRE(gigrig_cab_load_ir(e.get(), 0, b.data(), 3000, 1, fs) == GIGRIG_OK);  // waits for the first fade
    std::vector<float> buf(32, 0.0f);
    {
        AllocationGuard guard;
        gigrig_process(e.get(), buf.data(), buf.data(), 32);
    }
    CHECK(gigrig_cab_ir_length(e.get(), 0) == 0);  // first still fading in
    settle(e.get(), fs);
    CHECK(gigrig_cab_ir_length(e.get(), 0) ==
          static_cast<int32_t>(prepareIr(makeSourceIr(b.data(), 3000, 1, fs), fs).size()));
    CHECK(gigrig_collect_garbage(e.get()) == 1);  // a, after b replaced it

    REQUIRE(gigrig_cab_clear_ir(e.get(), 0) == GIGRIG_OK);
    settle(e.get(), fs);
    CHECK(gigrig_cab_ir_length(e.get(), 0) == 0);
    CHECK(gigrig_collect_garbage(e.get()) == 1);  // b
    const auto x = musicalSignal(1024, fs);
    std::vector<float> y(x.size());
    gigrig_process(e.get(), x.data(), y.data(), 1024);
    CHECK(y == x);  // empty again: bit-exact passthrough
}

TEST_CASE("cab engine: errors for bad slots, bad data and an unprepared engine") {
    EnginePtr unprepared{gigrig_create()};
    const float one = 1.0f;
    CHECK(gigrig_cab_load_ir(unprepared.get(), 0, &one, 1, 1, 48000.0) == GIGRIG_ERROR_NOT_PREPARED);
    CHECK(gigrig_cab_load_ir(nullptr, 0, &one, 1, 1, 48000.0) == GIGRIG_ERROR_NULL_ENGINE);
    auto e = makeEngine(48000.0, 64);
    CHECK(gigrig_cab_load_ir(e.get(), 2, &one, 1, 1, 48000.0) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_cab_load_ir(e.get(), -1, &one, 1, 1, 48000.0) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_cab_load_ir(e.get(), 0, nullptr, 1, 1, 48000.0) == GIGRIG_ERROR_INVALID_IR);
    const float zero = 0.0f;
    CHECK(gigrig_cab_load_ir(e.get(), 0, &zero, 1, 1, 48000.0) == GIGRIG_ERROR_INVALID_IR);
    CHECK(gigrig_cab_clear_ir(e.get(), 5) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_cab_ir_length(e.get(), 7) == 0);
    CHECK(gigrig_cab_ir_length(nullptr, 0) == 0);
}

TEST_CASE("cab engine: a rate change rebuilds loaded IRs, stale loads are refused") {
    auto e = makeEngine(48000.0, 64);
    const auto h = testIr(4800, 48000.0);
    REQUIRE(gigrig_cab_load_ir(e.get(), 0, h.data(), 4800, 1, 48000.0) == GIGRIG_OK);
    settle(e.get(), 48000.0);
    const int at48 = gigrig_cab_ir_length(e.get(), 0);
    REQUIRE(gigrig_prepare(e.get(), 96000.0, 32) == GIGRIG_OK);
    const int at96 = gigrig_cab_ir_length(e.get(), 0);
    CHECK(std::abs(at96 - 2 * at48) <= 4);

    // A load built at 96 kHz but picked up after a change back to 48 kHz is refused and freed.
    REQUIRE(gigrig_cab_load_ir(e.get(), 1, h.data(), 4800, 1, 48000.0) == GIGRIG_OK);
    REQUIRE(gigrig_prepare(e.get(), 48000.0, 32) == GIGRIG_OK);
    settle(e.get(), 48000.0);
    CHECK(gigrig_cab_ir_length(e.get(), 1) == 0);
    CHECK(e->engine.cabinet().refusedLoads() == 1);
    CHECK(gigrig_collect_garbage(e.get()) == 1);
}

TEST_CASE("cab engine: two slots and room are block size independent and never allocate, at every rate") {
    for (double fs : kTestRates) {
        const auto h1 = testIr(static_cast<std::size_t>(0.2 * fs), fs);
        const auto h2 = testIr(static_cast<std::size_t>(0.05 * fs), fs, 30);
        const auto x = musicalSignal(static_cast<std::size_t>(0.3 * fs), fs);
        checkBlockSizeIndependence(
            [&] {
                auto e = makeEngine(fs, 512);
                gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC2_ON, 1.0f);
                gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_ROOM, 30.0f);
                gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC2_ROOM, 20.0f);
                gigrig_cab_load_ir(e.get(), 0, h1.data(), static_cast<int32_t>(h1.size()), 1, fs);
                gigrig_cab_load_ir(e.get(), 1, h2.data(), static_cast<int32_t>(h2.size()), 1, fs);
                settle(e.get(), fs);
                gigrig_reset(e.get());
                return e;
            },
            [](EnginePtr& e, const float* in, float* out, int n) { gigrig_process(e.get(), in, out, n); }, x);
    }
}

TEST_CASE("cab benchmark: two 200 ms IRs and the room at 96 kHz, 32-sample blocks") {
    const double fs = 96000.0;
    auto e = makeEngine(fs, 32);
    const auto h = noise(static_cast<std::size_t>(0.2 * fs), 0.5, 7);  // noise never trims: worst case
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC2_ON, 1.0f);
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_MIC1_ROOM, 30.0f);
    gigrig_cab_load_ir(e.get(), 0, h.data(), static_cast<int32_t>(h.size()), 1, fs);
    gigrig_cab_load_ir(e.get(), 1, h.data(), static_cast<int32_t>(h.size()), 1, fs);
    settle(e.get(), fs);
    const auto x = musicalSignal(static_cast<std::size_t>(fs), fs);
    std::vector<float> y(x.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos + 32 <= x.size(); pos += 32) {
        gigrig_process(e.get(), x.data() + pos, y.data() + pos, 32);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    cab benchmark: 1 s of audio in %.1f ms (%.1f%% of one core)\n", seconds * 1000.0,
                seconds * 100.0);
    CHECK(allFinite(y));
}

// ---- Factory IRs ----

TEST_CASE("cab factory IRs: committed files match the model, have a licence row, and load") {
    std::string licences;
    {
        std::FILE* f = std::fopen(GIGRIG_IRS_DIR "/LICENSES.md", "rb");
        REQUIRE(f != nullptr);
        char chunk[4096];
        std::size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
            licences.append(chunk, got);
        }
        std::fclose(f);
    }
    int count = 0;
    for (const auto& cab : tools::factoryCabinets()) {
        for (const auto& mic : tools::factoryMics()) {
            const std::string name = cab.name + "-" + mic.name + ".wav";
            CHECK(licences.find("| " + name + " |") != std::string::npos);
            tools::WavData wav;
            std::string error;
            REQUIRE(tools::readWav(std::string(GIGRIG_IRS_DIR) + "/" + name, wav, error));
            CHECK(wav.channels == 1);
            CHECK(wav.sampleRate == tools::kIrSampleRate);
            // A tolerance rather than bit equality: libm on the Mac may round the designs differently.
            CHECK(maxDifference(wav.samples, tools::generateCabIr(cab, mic)) < 1.0e-6);
            for (double fs : kTestRates) {
                const auto ir = prepareIr(
                    makeSourceIr(wav.samples.data(), static_cast<int>(wav.frames()), 1, wav.sampleRate), fs);
                CHECK(!ir.empty());
                CHECK(static_cast<double>(ir.size()) <= 0.2 * fs);
                CHECK(std::fabs(ir[0]) > 0.0f);  // energy from sample 0: the cabinet adds no delay
            }
            ++count;
        }
    }
    CHECK(count == 6);
}
