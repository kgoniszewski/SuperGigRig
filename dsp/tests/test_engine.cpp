// test_engine.cpp - parameter table, command queues, and the engine's use of them.
#include <gigrig/gigrig.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "commands.hpp"
#include "dsp_test_utils.hpp"
#include "engine_handle.hpp"
#include "parameters.hpp"
#include "spsc_queue.hpp"

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
    REQUIRE(gigrig_prepare(e.get(), sampleRate, maxBlock) == GIGRIG_OK);
    return e;
}

// Counts live instances so tests can see exactly when objects are freed.
struct Tracked {
    static inline std::atomic<int> alive{0};
    Tracked() { ++alive; }
    ~Tracked() { --alive; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
};

}  // namespace

TEST_CASE("parameter table: dense ids, unique keys, valid ranges") {
    CHECK(gigrig_param_count() == GIGRIG_PARAM_COUNT);
    CHECK(gigrig_param_count() >= 2);
    std::set<std::string> keys;
    for (int32_t id = 0; id < gigrig_param_count(); ++id) {
        GigRigParamInfo p{};
        REQUIRE(gigrig_param_info(id, &p) == GIGRIG_OK);
        CHECK(p.id == id);
        CHECK(p.key != nullptr && p.name != nullptr && p.unit != nullptr);
        CHECK(keys.insert(p.key).second);
        CHECK(gigrig_param_find(p.key) == id);
        CHECK(p.min_value < p.max_value);
        CHECK(p.default_value >= p.min_value && p.default_value <= p.max_value);
    }
    GigRigParamInfo p{};
    CHECK(gigrig_param_info(-1, &p) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_param_info(GIGRIG_PARAM_COUNT, &p) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_param_info(0, nullptr) == GIGRIG_ERROR_INVALID_PARAM);
    CHECK(gigrig_param_find("no.such.key") == -1);
    CHECK(gigrig_param_find(nullptr) == -1);
}

TEST_CASE("parameter ids and keys are pinned (presets depend on them)") {
    // If this fails, an id or key was renumbered or renamed: append instead.
    CHECK(GIGRIG_PARAM_INPUT_LEVEL == 0);
    CHECK(GIGRIG_PARAM_OUTPUT_LEVEL == 1);
    CHECK(std::strcmp(parameterInfo(0).key, "input.level") == 0);
    CHECK(std::strcmp(parameterInfo(1).key, "output.level") == 0);
    const char* const cabKeys[] = {"cab.on",         "cab.mic1.on",   "cab.mic1.level", "cab.mic1.phase",
                                   "cab.mic1.room",  "cab.mic2.on",   "cab.mic2.level", "cab.mic2.phase",
                                   "cab.mic2.room",  "cab.room.size"};
    CHECK(GIGRIG_PARAM_CAB_ON == 2);
    CHECK(GIGRIG_PARAM_CAB_ROOM_SIZE == 11);
    for (int i = 0; i < 10; ++i) {
        CHECK(std::strcmp(parameterInfo(2 + i).key, cabKeys[i]) == 0);
    }
}

TEST_CASE("parameter values: defaults, clamping, NaN ignored, unknown ids ignored") {
    auto e = makeEngine(48000.0, 64);
    for (int32_t id = 0; id < gigrig_param_count(); ++id) {
        CHECK(gigrig_get_param(e.get(), id) == parameterInfo(id).default_value);
    }
    gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, 6.5f);
    CHECK(gigrig_get_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL) == 6.5f);
    gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, 1000.0f);
    CHECK(gigrig_get_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL) == 24.0f);
    gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, std::nanf(""));
    CHECK(gigrig_get_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL) == 24.0f);
    gigrig_set_param(e.get(), 9999, 1.0f);
    gigrig_set_param(nullptr, 0, 1.0f);
    CHECK(gigrig_get_param(e.get(), 9999) == 0.0f);
    CHECK(gigrig_get_param(nullptr, 0) == 0.0f);
}

TEST_CASE("tapers: linear, log and stepped map to and from knob positions") {
    const GigRigParamInfo lin{
        0, "t.lin", "Lin", "dB", -24.0f, 24.0f, 0.0f, GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR};
    CHECK_NEAR(parameterToNormalized(lin, 0.0f), 0.5, 1e-7);
    CHECK_NEAR(parameterFromNormalized(lin, 0.25f), -12.0, 1e-5);

    const GigRigParamInfo log{
        0, "t.log", "Log", "Hz", 20.0f, 20000.0f, 1000.0f, GIGRIG_TAPER_LOG, GIGRIG_SMOOTHING_ONE_POLE};
    CHECK_NEAR(parameterFromNormalized(log, 0.5f), std::sqrt(20.0 * 20000.0), 0.01);  // geometric middle
    CHECK_NEAR(parameterToNormalized(log, 200.0f), 1.0 / 3.0, 1e-6);
    CHECK(parameterFromNormalized(log, 1.0f) == 20000.0f);
    CHECK(parameterFromNormalized(log, 0.0f) == 20.0f);

    const GigRigParamInfo step{
        0, "t.step", "Step", "", 0.0f, 2.0f, 0.0f, GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE};
    CHECK(parameterFromNormalized(step, 0.3f) == 1.0f);
    CHECK(parameterFromNormalized(step, 0.8f) == 2.0f);
    CHECK(constrainParameter(step, 0.4f) == 0.0f);

    for (int32_t id = 0; id < gigrig_param_count(); ++id) {
        const float d = parameterInfo(id).default_value;
        CHECK_NEAR(gigrig_param_from_normalized(id, gigrig_param_to_normalized(id, d)), d, 1e-4);
    }
    CHECK(gigrig_param_to_normalized(-1, 1.0f) == 0.0f);
    CHECK(gigrig_param_from_normalized(-1, 1.0f) == 0.0f);
}

TEST_CASE("input and output levels apply in dB after a fixed ramp") {
    for (double fs : kTestRates) {
        auto e = makeEngine(fs, 64);
        gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, -6.0f);
        gigrig_set_param(e.get(), GIGRIG_PARAM_OUTPUT_LEVEL, -18.0f);
        const auto n = static_cast<std::size_t>(fs * 0.1);
        std::vector<float> in(n, 0.25f), out(n, 0.0f);
        for (std::size_t pos = 0; pos < n; pos += 64) {
            const auto m = static_cast<int32_t>(std::min<std::size_t>(64, n - pos));
            gigrig_process(e.get(), in.data() + pos, out.data() + pos, m);
        }
        const double expected = 0.25 * dbToGain(-6.0) * dbToGain(-18.0);
        const float* o = out.data();
        REQUIRE(o != nullptr);
        CHECK(std::fabs(o[0] - 0.25f) < 0.01f);  // the ramp starts at the old level, no jump
        const auto ramp = static_cast<std::size_t>(std::lround(Engine::kGainRampSeconds * fs));
        CHECK_NEAR(out[ramp + 1], expected, 1e-6);
        CHECK_NEAR(out.back(), expected, 1e-6);
        for (std::size_t i = 1; i < ramp; ++i) {
            CHECK(out[i] <= out[i - 1] + 1e-7f);  // monotonic, no zipper steps back
        }
    }
}

TEST_CASE("prepare and reset start at the current levels without a ramp") {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    gigrig_set_param(e.get(), GIGRIG_PARAM_OUTPUT_LEVEL, -6.0f);
    REQUIRE(gigrig_prepare(e.get(), 48000.0, 32) == GIGRIG_OK);
    std::vector<float> in(32, 0.5f), out(32);
    gigrig_process(e.get(), in.data(), out.data(), 32);
    CHECK_NEAR(out[0], 0.5 * dbToGain(-6.0), 1e-6);

    gigrig_set_param(e.get(), GIGRIG_PARAM_OUTPUT_LEVEL, 0.0f);
    gigrig_reset(e.get());
    gigrig_process(e.get(), in.data(), out.data(), 32);
    CHECK(out[0] == 0.5f);
}

TEST_CASE("engine output with parameter changes is block size independent") {
    const double fs = 48000.0;
    const auto in = musicalSignal(24576, fs);
    // Parameter changes happen at sample positions that are multiples of 512,
    // so every block size sees them at the same sample.
    auto make = [fs] { return makeEngine(fs, 512); };
    std::size_t done = 0;
    auto process = [&done](EnginePtr& e, const float* x, float* y, int n) {
        if (done % 8192 == 0) {
            const float level = done == 0 ? 0.0f : (done == 8192 ? -9.0f : 4.0f);
            gigrig_set_param(e.get(), GIGRIG_PARAM_OUTPUT_LEVEL, level);
            gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, -level / 2.0f);
        }
        gigrig_process(e.get(), x, y, n);
        done += static_cast<std::size_t>(n);
    };
    const long before = allocationViolations();
    done = 0;
    const auto reference = renderInBlocks(make, process, in, 1);
    for (int b : kTestBlockSizes) {
        done = 0;
        CHECK(renderInBlocks(make, process, in, b) == reference);
    }
    CHECK(allocationViolations() == before);
    CHECK(maxDifference(reference, in) > 0.01);  // the level changes did something
}

TEST_CASE("SPSC queue: order, full and empty") {
    SpscQueue<int, 4> q;
    int v = 0;
    CHECK(!q.pop(v));
    CHECK(q.freeForProducer() == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(q.push(i));
    }
    CHECK(!q.push(99));
    CHECK(q.sizeForConsumer() == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(q.pop(v));
        CHECK(v == i);
    }
    CHECK(!q.pop(v));
    // Wrap-around many times.
    for (int i = 0; i < 1000; ++i) {
        CHECK(q.push(i));
        CHECK(q.pop(v) && v == i);
    }
}

TEST_CASE("SPSC queue: two threads pass a million items in order") {
    auto q = std::make_unique<SpscQueue<std::uint32_t, 64>>();
    constexpr std::uint32_t count = 1000000;
    std::thread producer([&q] {
        for (std::uint32_t i = 0; i < count;) {
            if (q->push(i)) {
                ++i;
            }
        }
    });
    bool inOrder = true;
    std::uint32_t expected = 0, v = 0;
    while (expected < count) {
        if (q->pop(v)) {
            inOrder = inOrder && v == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
}

TEST_CASE("commands: objects cross to the audio thread and are freed off it") {
    auto e = makeEngine(48000.0, 64);
    Engine& engine = e->engine;
    Tracked::alive = 0;
    for (int i = 0; i < 3; ++i) {
        CHECK(engine.pushCommand(makeCommand(CommandType::Noop, i, std::make_unique<Tracked>())));
    }
    CHECK(Tracked::alive == 3);
    CHECK(gigrig_collect_garbage(e.get()) == 0);  // nothing retired before the audio thread runs

    std::vector<float> buf(64, 0.0f);
    {
        AllocationGuard guard;  // applying commands must not allocate or free
        gigrig_process(e.get(), buf.data(), buf.data(), 64);
    }
    CHECK(Tracked::alive == 3);  // retired, not yet freed: the audio thread never frees
    CHECK(gigrig_collect_garbage(e.get()) == 3);
    CHECK(Tracked::alive == 0);
}

TEST_CASE("commands: full queue refuses, pending objects are freed on destroy") {
    Tracked::alive = 0;
    {
        auto e = makeEngine(48000.0, 64);
        Engine& engine = e->engine;
        int accepted = 0;
        for (std::size_t i = 0; i < CommandQueues::kCapacity + 5; ++i) {
            Command c = makeCommand(CommandType::Noop, 0, std::make_unique<Tracked>());
            if (engine.pushCommand(c)) {
                ++accepted;
            } else {
                disposeCommand(c);  // the caller still owns a refused command
            }
        }
        CHECK(accepted == static_cast<int>(CommandQueues::kCapacity));
        CHECK(Tracked::alive == accepted);
    }
    CHECK(Tracked::alive == 0);
}

TEST_CASE("commands: the audio thread waits when the return queue is full") {
    CommandQueues q;
    Tracked::alive = 0;
    int applied = 0;
    auto apply = [&applied](const Command& c) noexcept {
        ++applied;
        return Retired{c.object, c.destroy};
    };
    for (std::size_t round = 0; round < 2; ++round) {
        for (std::size_t i = 0; i < CommandQueues::kCapacity; ++i) {
            REQUIRE(q.push(makeCommand(CommandType::Noop, 0, std::make_unique<Tracked>())));
        }
        q.drain(apply);
    }
    // The second batch could not be retired, so it stays queued instead of leaking.
    CHECK(applied == static_cast<int>(CommandQueues::kCapacity));
    CHECK(q.collectGarbage() == static_cast<int>(CommandQueues::kCapacity));
    q.drain(apply);
    CHECK(applied == 2 * static_cast<int>(CommandQueues::kCapacity));
    CHECK(q.collectGarbage() == static_cast<int>(CommandQueues::kCapacity));
    CHECK(Tracked::alive == 0);
}

TEST_CASE("engine processing never allocates, at every rate and block size") {
    for (double fs : kTestRates) {
        for (int b : kTestBlockSizes) {
            auto e = makeEngine(fs, b);
            const auto in = musicalSignal(4096, fs);
            std::vector<float> out(in.size());
            const long before = allocationViolations();
            for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t>(b)) {
                if (pos == 2048) {
                    gigrig_set_param(e.get(), GIGRIG_PARAM_INPUT_LEVEL, 3.0f);
                }
                AllocationGuard guard;
                gigrig_process(e.get(), in.data() + pos, out.data() + pos,
                               static_cast<int32_t>(
                                   std::min<std::size_t>(static_cast<std::size_t>(b), in.size() - pos)));
            }
            CHECK(allocationViolations() == before);
        }
    }
}

TEST_CASE("allocation guard catches allocation inside a guard and only there") {
    const long before = allocationViolations();
    {
        auto v = std::make_unique<std::vector<int>>(10);  // outside: allowed
        CHECK(allocationViolations() == before);
        AllocationGuard guard;
        v->push_back(1);  // grows the vector: a violation
    }
    CHECK(allocationViolations() == before + 1);
    resetAllocationViolations();
    CHECK(allocationViolations() == 0);
}
