// test_time_blocks.cpp - delay line, envelope follower and LFO.
#include <memory>

#include "blocks/delay_line.hpp"
#include "blocks/envelope.hpp"
#include "blocks/lfo.hpp"
#include "blocks/svf.hpp"
#include "dsp_test_utils.hpp"

using namespace gigrig;
using namespace gigrig::test;

TEST_CASE("delay line: integer delay returns the input from d samples ago") {
    DelayLine d;
    d.prepare(100);
    std::vector<float> out;
    for (int i = 0; i < 200; ++i) {
        out.push_back(d.read(37));
        d.write(static_cast<float>(i + 1));
    }
    CHECK(out[36] == 0.0f);
    CHECK(out[37] == 1.0f);
    CHECK(out[150] == 114.0f);
    CHECK(d.read(1) == 200.0f);
    CHECK(d.read(0) == 200.0f);          // clamped to 1
    CHECK(d.read(1000) == d.read(100));  // clamped to the maximum
}

TEST_CASE("delay line: delay in milliseconds is exact at every rate") {
    for (double fs : kTestRates) {
        DelayLine d;
        const int delay = static_cast<int>(std::lround(0.250 * fs));
        d.prepare(static_cast<int>(fs));
        int arrival = -1;
        for (int i = 0; i < static_cast<int>(fs); ++i) {
            const float y = d.read(delay);
            if (y != 0.0f && arrival < 0) {
                arrival = i;
            }
            d.write(i == 0 ? 1.0f : 0.0f);
        }
        CHECK(arrival == delay);
    }
}

TEST_CASE("delay line: fractional reads interpolate a sine accurately") {
    const double fs = 48000.0;
    const double f = 1000.0;
    DelayLine d;
    d.prepare(1000);
    const auto x = sine(4000, f, fs);
    double cubicErr = 0.0, linearErr = 0.0;
    const double delay = 123.37;
    for (std::size_t i = 0; i < x.size(); ++i) {
        d.write(x[i]);
        if (i > 1000) {
            // After the write, the newest sample is at delay 1 relative to the next write.
            const double expected = std::sin(2.0 * kPi * f * (static_cast<double>(i) + 1.0 - delay) / fs);
            cubicErr = std::max(cubicErr, std::fabs(static_cast<double>(d.readCubic(delay)) - expected));
            linearErr = std::max(linearErr, std::fabs(static_cast<double>(d.readLinear(delay)) - expected));
        }
    }
    CHECK(cubicErr < 2.0e-4);
    CHECK(linearErr < 3.0e-3);
    CHECK(cubicErr < linearErr / 5.0);
}

TEST_CASE("delay line: integer positions of fractional reads are exact, and clamps hold") {
    DelayLine d;
    d.prepare(64);
    for (int i = 0; i < 128; ++i) {
        d.write(static_cast<float>(i));
    }
    CHECK(d.readCubic(10.0) == d.read(10));
    CHECK(d.readLinear(10.0) == d.read(10));
    CHECK(d.readCubic(0.5) == d.readCubic(2.0));
    CHECK(d.readLinear(-3.0) == d.read(1));
    CHECK(d.readCubic(1.0e9) == d.readCubic(64.0));
    CHECK(std::isfinite(d.readCubic(64.0)));
    d.reset();
    CHECK(d.read(5) == 0.0f);
}

TEST_CASE("delay line with modulated cubic read is block size independent") {
    const double fs = 48000.0;
    struct Chorus {
        DelayLine d;
        double phase = 0.0;
    };
    auto make = [] {
        auto c = std::make_unique<Chorus>();
        c->d.prepare(4800);
        return c;
    };
    auto process = [fs](std::unique_ptr<Chorus>& c, const float* in, float* out, int n) {
        for (int i = 0; i < n; ++i) {
            const double delay = 480.0 + 200.0 * std::sin(c->phase);
            c->phase += 2.0 * kPi * 0.7 / fs;
            out[i] = 0.5f * (in[i] + c->d.readCubic(delay));
            c->d.write(in[i]);
        }
    };
    checkBlockSizeIndependence(make, process, musicalSignal(24000, fs));
}

TEST_CASE("envelope follower: attack and release time constants at every rate") {
    for (double fs : kTestRates) {
        EnvelopeFollower e;
        e.prepare(fs);
        e.setTimes(0.002, 0.050);
        const int attack = static_cast<int>(std::lround(0.002 * fs));
        float v = 0.0f;
        for (int i = 0; i < attack; ++i) {
            v = e.process(1.0f);
        }
        CHECK_NEAR(v, 1.0 - std::exp(-1.0), 0.01);
        for (int i = 0; i < attack * 20; ++i) {
            e.process(1.0f);
        }
        const int release = static_cast<int>(std::lround(0.050 * fs));
        for (int i = 0; i < release; ++i) {
            v = e.process(0.0f);
        }
        CHECK_NEAR(v, std::exp(-1.0), 0.01);
    }
}

TEST_CASE("envelope follower: peak follows the crest, RMS reads 0.707 of a sine") {
    const double fs = 48000.0;
    const auto x = sine(static_cast<std::size_t>(fs), 220.0, fs, 0.8);
    EnvelopeFollower peak, rmsDet;
    peak.prepare(fs);
    rmsDet.prepare(fs);
    peak.setTimes(0.0001, 0.5);
    rmsDet.setMode(EnvelopeFollower::Mode::Rms);
    rmsDet.setTimes(0.2, 0.2);
    for (float s : x) {
        peak.process(s);
        rmsDet.process(s);
    }
    CHECK_NEAR(peak.value(), 0.8, 0.01);
    CHECK_NEAR(rmsDet.value(), 0.8 / std::sqrt(2.0), 0.01);
}

TEST_CASE("LFO runs at the set rate at every sample rate") {
    for (double fs : kTestRates) {
        for (auto shape : {Lfo::Shape::Sine, Lfo::Shape::Triangle}) {
            Lfo lfo;
            lfo.prepare(fs);
            lfo.setShape(shape);
            lfo.setRate(5.0);
            int risingZeroCrossings = 0;
            float prev = lfo.next();
            float lo = prev, hi = prev;
            for (int i = 1; i < static_cast<int>(fs * 2.0); ++i) {
                const float v = lfo.next();
                if (prev < 0.0f && v >= 0.0f) {
                    ++risingZeroCrossings;
                }
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                prev = v;
            }
            CHECK(risingZeroCrossings == 10 || risingZeroCrossings == 9);
            CHECK_NEAR(hi, 1.0, 1e-3);
            CHECK_NEAR(lo, -1.0, 1e-3);
        }
    }
}

TEST_CASE("LFO tempo lock, triangle shape and phase reset") {
    Lfo lfo;
    lfo.prepare(48000.0);
    lfo.setTempo(120.0, 1.0);  // one cycle per beat at 120 BPM
    CHECK_NEAR(lfo.rate(), 2.0, 1e-12);
    lfo.setTempo(90.0, 0.25);  // sixteenth notes at 90 BPM
    CHECK_NEAR(lfo.rate(), 6.0, 1e-12);

    lfo.setShape(Lfo::Shape::Triangle);
    lfo.setRate(1.0);
    lfo.setPhase(0.25);
    CHECK_NEAR(lfo.next(), 1.0, 1e-6);
    lfo.setPhase(0.5);
    CHECK_NEAR(lfo.next(), 0.0, 1e-6);
    lfo.setPhase(1.75);  // wraps to 0.75
    CHECK_NEAR(lfo.next(), -1.0, 1e-6);
    lfo.reset();
    CHECK(lfo.next() == 0.0f);
}

TEST_CASE("envelope-swept SVF with LFO is block size independent at every rate") {
    // The modulation blocks together, the way the envelope filter and tremolo will use them.
    struct Chain {
        EnvelopeFollower env;
        Lfo lfo;
        StateVariableFilter svf;
    };
    for (double fs : kTestRates) {
        auto make = [fs] {
            auto c = std::make_unique<Chain>();
            c->env.prepare(fs);
            c->env.setTimes(0.003, 0.120);
            c->lfo.prepare(fs);
            c->lfo.setRate(3.0);
            c->svf.prepare(fs);
            return c;
        };
        auto process = [](std::unique_ptr<Chain>& c, const float* in, float* out, int n) {
            for (int i = 0; i < n; ++i) {
                const double e = static_cast<double>(c->env.process(in[i]));
                const double l = static_cast<double>(c->lfo.next());
                c->svf.setParameters(300.0 * std::pow(10.0, std::min(1.5, 3.0 * e)) * (1.0 + 0.2 * l), 4.0);
                out[i] = c->svf.process(in[i]).bandPass * 0.25f;
            }
        };
        const auto signal = musicalSignal(static_cast<std::size_t>(fs / 2), fs);
        checkBlockSizeIndependence(make, process, signal);
    }
}
