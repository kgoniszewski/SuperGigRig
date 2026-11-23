// test_realtime_io.cpp - tests for the platform-independent render path of the app bridge.
#include <gigrig/gigrig.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "RealtimeIO.hpp"
#include "test_framework.hpp"

namespace {

using gigrig::app::LoopbackProbe;
using gigrig::app::RealtimeIO;

struct EngineDeleter {
    void operator()(GigRigEngine* e) const { gigrig_destroy(e); }
};
using EnginePtr = std::unique_ptr<GigRigEngine, EngineDeleter>;

struct Rig {
    EnginePtr dsp{gigrig_create()};
    std::unique_ptr<RealtimeIO> io = std::make_unique<RealtimeIO>();
    Rig() {
        REQUIRE(dsp != nullptr);
        // These tests check channel routing and the loopback probe, so the amp (on by
        // default since work package 4) is bypassed to keep the DSP a bit-exact passthrough.
        gigrig_set_param(dsp.get(), GIGRIG_PARAM_AMP_ON, 0.0f);
        REQUIRE(io->prepare(dsp.get(), 48000.0));
    }
};

float ramp(std::size_t i, int channel) {
    return static_cast<float>(channel + 1) * 0.01f + static_cast<float>(i % 97) * 0.001f;
}

// Simulates a device whose output is cabled back to input channel `loopChannel` with a
// round trip of `latency` frames, driven in cycles of `block` frames (latency >= block,
// as on real hardware). Returns what the probe measured.
std::vector<int32_t> measureLoopback(int block, int latency, int loopChannel, int pings, float hum = 0.0f) {
    Rig rig;
    rig.io->setInputChannel(loopChannel);
    rig.io->probe().arm(pings, 480, 9600, 0.05f);

    const int numIn = 2;
    const int numOut = 2;
    std::vector<float> sent;  // everything written to output 1, by frame
    std::vector<float> inBuf[numIn];
    std::vector<float> outBuf[numOut];
    for (auto& b : inBuf) b.assign(static_cast<std::size_t>(block), 0.0f);
    for (auto& b : outBuf) b.assign(static_cast<std::size_t>(block), 0.0f);

    std::size_t frame = 0;
    for (int cycle = 0; cycle < 100000; ++cycle) {
        for (int i = 0; i < block; ++i) {
            const std::size_t f = frame + static_cast<std::size_t>(i);
            const float back =
                f >= static_cast<std::size_t>(latency) ? sent[f - static_cast<std::size_t>(latency)] : 0.0f;
            // Optional 50 Hz hum on top of the returning signal, like a noisy cable or pickup.
            const float noise = hum * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 50.0 *
                                                                  static_cast<double>(f) / 48000.0));
            inBuf[loopChannel][static_cast<std::size_t>(i)] = back + noise;
            inBuf[1 - loopChannel][static_cast<std::size_t>(i)] = 0.0f;
        }
        const float* in[numIn] = {inBuf[0].data(), inBuf[1].data()};
        float* out[numOut] = {outBuf[0].data(), outBuf[1].data()};
        rig.io->renderMono(in, numIn, out, numOut, block);
        sent.insert(sent.end(), outBuf[0].begin(), outBuf[0].end());
        frame += static_cast<std::size_t>(block);
        if (rig.io->probe().phase() == LoopbackProbe::Phase::Done) break;
    }
    std::vector<int32_t> results;
    for (int i = 0; i < rig.io->probe().resultCount(); ++i) results.push_back(rig.io->probe().result(i));
    return results;
}

}  // namespace

TEST_CASE("bridge: selected input channel goes to output 1 only") {
    Rig rig;
    const int n = 64;
    std::vector<float> in0(n), in1(n), out0(n, 9.0f), out1(n, 9.0f), out2(n, 9.0f);
    for (int i = 0; i < n; ++i) {
        in0[static_cast<std::size_t>(i)] = ramp(static_cast<std::size_t>(i), 0);
        in1[static_cast<std::size_t>(i)] = ramp(static_cast<std::size_t>(i), 1);
    }
    const float* in[2] = {in0.data(), in1.data()};
    float* out[3] = {out0.data(), out1.data(), out2.data()};

    rig.io->setInputChannel(1);
    rig.io->renderMono(in, 2, out, 3, n);
    for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
        CHECK(out0[i] == in1[i]);
        CHECK(out1[i] == 0.0f);
        CHECK(out2[i] == 0.0f);
    }

    rig.io->setOutputToBoth(true);
    rig.io->setInputChannel(0);
    rig.io->renderMono(in, 2, out, 3, n);
    for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
        CHECK(out0[i] == in0[i]);
        CHECK(out1[i] == in0[i]);
        CHECK(out2[i] == 0.0f);
    }
}

TEST_CASE("bridge: missing input channel and oversized cycles give silence") {
    Rig rig;
    const int n = 32;
    std::vector<float> in0(n, 0.5f), out0(n, 9.0f);
    const float* in[1] = {in0.data()};
    float* out[1] = {out0.data()};

    rig.io->setInputChannel(3);
    rig.io->renderMono(in, 1, out, 1, n);
    for (float v : out0) CHECK(v == 0.0f);

    rig.io->setInputChannel(0);
    std::vector<float> big(RealtimeIO::kMaxFrames + 1, 0.5f);
    std::vector<float> bigOut(RealtimeIO::kMaxFrames + 1, 9.0f);
    const float* bigIn[1] = {big.data()};
    float* bigOutPtr[1] = {bigOut.data()};
    rig.io->renderMono(bigIn, 1, bigOutPtr, 1, RealtimeIO::kMaxFrames + 1);
    for (float v : bigOut) CHECK(v == 0.0f);
}

TEST_CASE("bridge: loopback probe measures the exact round trip at any buffer size") {
    for (int block : {1, 32, 64, 256}) {
        for (int latency : {block, 2 * block + 37, 3 * block + 150}) {
            const auto r = measureLoopback(block, latency, 0, 5);
            REQUIRE(r.size() == 5);
            for (int32_t v : r) CHECK(v == latency);
        }
    }
}

TEST_CASE("bridge: loopback probe works on input 2 and reports a missing cable as failed") {
    const auto r = measureLoopback(64, 200, 1, 3);
    REQUIRE(r.size() == 3);
    for (int32_t v : r) CHECK(v == 200);

    // Cable on input 2 but the rig listens on input 1: every ping times out.
    Rig rig;
    rig.io->setInputChannel(0);
    rig.io->probe().arm(2, 48, 480, 0.05f);
    std::vector<float> silent(32, 0.0f), o0(32), o1(32);
    const float* in[2] = {silent.data(), silent.data()};
    float* out[2] = {o0.data(), o1.data()};
    for (int c = 0; c < 200 && rig.io->probe().phase() != LoopbackProbe::Phase::Done; ++c) {
        rig.io->renderMono(in, 2, out, 2, 32);
    }
    REQUIRE(rig.io->probe().resultCount() == 2);
    CHECK(rig.io->probe().result(0) == LoopbackProbe::kFailed);
    CHECK(rig.io->probe().result(1) == LoopbackProbe::kFailed);
}

TEST_CASE("bridge: guitar signal is muted while measuring and returns afterwards") {
    Rig rig;
    rig.io->probe().arm(1, 64, 64, 0.05f);
    const int n = 32;
    std::vector<float> in0(n, 0.01f), out0(n);  // below the threshold: never detected
    const float* in[1] = {in0.data()};
    float* out[1] = {out0.data()};

    int cycles = 0;
    while (rig.io->probe().phase() != LoopbackProbe::Phase::Done && cycles < 100) {
        rig.io->renderMono(in, 1, out, 1, n);
        // The cycle in which the last ping resolves already carries the guitar again.
        if (rig.io->probe().phase() != LoopbackProbe::Phase::Done) {
            for (float v : out0) CHECK(v == 0.0f || v == LoopbackProbe::kClickLevel);
        }
        ++cycles;
    }
    REQUIRE(rig.io->probe().phase() == LoopbackProbe::Phase::Done);
    rig.io->renderMono(in, 1, out, 1, n);
    for (float v : out0) CHECK(v == 0.01f);
}

TEST_CASE("bridge: loopback probe ignores hum below the click and reports a too noisy input") {
    // -24 dBFS hum is above a fixed -26 dBFS threshold but well below the click: still exact.
    const auto quiet = measureLoopback(64, 300, 0, 5, 0.06f);
    REQUIRE(quiet.size() == 5);
    for (int32_t v : quiet) CHECK(v == 300);

    // -20 dBFS hum: the click is not distinct enough, so no number is reported.
    const auto loud = measureLoopback(64, 300, 0, 3, 0.1f);
    REQUIRE(loud.size() == 3);
    for (int32_t v : loud) CHECK(v == LoopbackProbe::kTooNoisy);
}
