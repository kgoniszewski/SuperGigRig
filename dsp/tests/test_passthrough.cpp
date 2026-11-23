// test_passthrough.cpp - work package 0 tests for the C interface.
#include "test_framework.hpp"

#include <gigrig/gigrig.h>

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

namespace {

struct EngineDeleter {
    void operator()(GigRigEngine* e) const { gigrig_destroy(e); }
};
using EnginePtr = std::unique_ptr<GigRigEngine, EngineDeleter>;

EnginePtr makeEngine(double sampleRate, int maxBlock) {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    // These tests check the interface and level path, so the amp (on by default
    // since work package 4) is bypassed to keep the chain bit exact.
    gigrig_set_param(e.get(), GIGRIG_PARAM_AMP_ON, 0.0f);
    REQUIRE(gigrig_prepare(e.get(), sampleRate, maxBlock) == GIGRIG_OK);
    return e;
}

// Deterministic test signal: a 440 Hz sine plus a little low-level "noise".
std::vector<float> testSignal(std::size_t n, double sampleRate) {
    std::vector<float> x(n);
    std::uint32_t seed = 12345;
    for (std::size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double noise = (static_cast<double>(seed >> 8) / 16777216.0 - 0.5) * 1e-3;
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / sampleRate) + noise);
    }
    return x;
}

// Renders the whole signal through a fresh engine in blocks of blockSize.
std::vector<float> render(const std::vector<float>& in, double sampleRate, int blockSize) {
    auto e = makeEngine(sampleRate, blockSize);
    std::vector<float> out(in.size(), -1.0f);
    for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t>(blockSize)) {
        const auto n = static_cast<int32_t>(std::min<std::size_t>(static_cast<std::size_t>(blockSize), in.size() - pos));
        gigrig_process(e.get(), in.data() + pos, out.data() + pos, n);
    }
    return out;
}

}  // namespace

TEST_CASE("api version matches header") {
    CHECK(gigrig_api_version() == GIGRIG_API_VERSION);
}

TEST_CASE("passthrough copies input to output exactly") {
    const auto in = testSignal(4096, 48000.0);
    const auto out = render(in, 48000.0, 32);
    CHECK(out == in);
}

TEST_CASE("passthrough is bit exact at every supported sample rate") {
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0}) {
        const auto in = testSignal(2048, rate);
        CHECK(render(in, rate, 64) == in);
    }
}

TEST_CASE("output does not depend on block size") {
    const auto in = testSignal(48000, 48000.0);
    const auto reference = render(in, 48000.0, 1);
    for (int blockSize : {32, 64, 512}) {
        CHECK(render(in, 48000.0, blockSize) == reference);
    }
}

TEST_CASE("in-place processing works") {
    auto e = makeEngine(48000.0, 64);
    const auto original = testSignal(64, 48000.0);
    auto buffer = original;
    gigrig_process(e.get(), buffer.data(), buffer.data(), 64);
    CHECK(buffer == original);
}

TEST_CASE("impulse arrives with zero samples of latency") {
    auto e = makeEngine(48000.0, 32);
    CHECK(gigrig_latency_samples(e.get()) == 0);
    std::vector<float> in(32, 0.0f), out(32, 0.0f);
    in[0] = 1.0f;
    gigrig_process(e.get(), in.data(), out.data(), 32);
    CHECK(out[0] == 1.0f);
    for (std::size_t i = 1; i < out.size(); ++i) {
        CHECK(out[i] == 0.0f);
    }
}

TEST_CASE("blocks larger than the prepared maximum are processed in chunks") {
    auto e = makeEngine(48000.0, 32);
    const auto in = testSignal(1000, 48000.0);
    std::vector<float> out(in.size(), -1.0f);
    gigrig_process(e.get(), in.data(), out.data(), static_cast<int32_t>(in.size()));
    CHECK(out == in);
}

TEST_CASE("unprepared engine outputs silence") {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    const auto in = testSignal(64, 48000.0);
    std::vector<float> out(64, 1.0f);
    gigrig_process(e.get(), in.data(), out.data(), 64);
    for (float s : out) {
        CHECK(s == 0.0f);
    }
}

TEST_CASE("null input outputs silence") {
    auto e = makeEngine(48000.0, 64);
    std::vector<float> out(64, 1.0f);
    gigrig_process(e.get(), nullptr, out.data(), 64);
    for (float s : out) {
        CHECK(s == 0.0f);
    }
}

TEST_CASE("null engine and null output are ignored") {
    std::vector<float> buf(16, 0.25f);
    gigrig_process(nullptr, buf.data(), buf.data(), 16);
    auto e = makeEngine(48000.0, 16);
    gigrig_process(e.get(), buf.data(), nullptr, 16);
    gigrig_process(e.get(), buf.data(), buf.data(), 0);
    gigrig_process(e.get(), buf.data(), buf.data(), -5);
    gigrig_reset(nullptr);
    gigrig_destroy(nullptr);
    CHECK(gigrig_latency_samples(nullptr) == 0);
    CHECK(buf[0] == 0.25f);
}

TEST_CASE("prepare rejects invalid arguments and leaves engine unprepared") {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    CHECK(gigrig_prepare(nullptr, 48000.0, 64) == GIGRIG_ERROR_NULL_ENGINE);
    CHECK(gigrig_prepare(e.get(), 0.0, 64) == GIGRIG_ERROR_INVALID_SAMPLE_RATE);
    CHECK(gigrig_prepare(e.get(), std::nan(""), 64) == GIGRIG_ERROR_INVALID_SAMPLE_RATE);
    CHECK(gigrig_prepare(e.get(), 1.0e6, 64) == GIGRIG_ERROR_INVALID_SAMPLE_RATE);
    CHECK(gigrig_prepare(e.get(), 48000.0, 0) == GIGRIG_ERROR_INVALID_BLOCK_SIZE);
    CHECK(gigrig_prepare(e.get(), 48000.0, GIGRIG_MAX_BLOCK_SIZE + 1) == GIGRIG_ERROR_INVALID_BLOCK_SIZE);

    // A failed prepare after a good one must leave the engine unprepared (silent).
    REQUIRE(gigrig_prepare(e.get(), 48000.0, 64) == GIGRIG_OK);
    CHECK(gigrig_prepare(e.get(), -1.0, 64) == GIGRIG_ERROR_INVALID_SAMPLE_RATE);
    std::vector<float> in(64, 0.5f), out(64, 1.0f);
    gigrig_process(e.get(), in.data(), out.data(), 64);
    CHECK(out[0] == 0.0f);
}

TEST_CASE("prepare can be called again to change rate and block size") {
    auto e = makeEngine(48000.0, 32);
    REQUIRE(gigrig_prepare(e.get(), 96000.0, 256) == GIGRIG_OK);
    const auto in = testSignal(256, 96000.0);
    std::vector<float> out(256, -1.0f);
    gigrig_process(e.get(), in.data(), out.data(), 256);
    CHECK(out == in);
}
