// test_steel100.cpp - work package 6: the Steel 100 amp and the amp section's model
// choice (plan section 8 tests for a nonlinear block).
#include <gigrig/gigrig.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "amp_test_utils.hpp"
#include "amps/amp_section.hpp"
#include "amps/combo40.hpp"
#include "amps/steel100.hpp"
#include "dsp_test_utils.hpp"

using namespace gigrig;
using namespace gigrig::amp;
using namespace gigrig::test;

namespace {

struct EngineDeleter {
    void operator()(GigRigEngine* e) const noexcept { gigrig_destroy(e); }
};
using EnginePtr = std::unique_ptr<GigRigEngine, EngineDeleter>;

// Largest sample-to-sample step of x over [from, to).
double largestStep(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double step = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        step = std::max(step, std::fabs(static_cast<double>(x[i + 1]) - static_cast<double>(x[i])));
    }
    return step;
}

double stackDb(const Steel100::Settings& s, double f) {
    Steel100::ToneNetwork n;
    Steel100::configureToneNetwork(n, 192000.0, s);
    return gainToDb(std::abs(n.analogResponse(f)));
}

}  // namespace

// ------------------------------------------------------------------ tone network

TEST_CASE("Steel 100: tone network digital response is the analog one at the prewarped frequency") {
    for (double fs : {176400.0, 192000.0}) {
        for (const auto& knobs :
             {std::array<float, 4>{5.0f, 5.0f, 5.0f, 6.0f}, std::array<float, 4>{10.0f, 0.0f, 10.0f, 0.0f},
              std::array<float, 4>{1.0f, 10.0f, 0.0f, 10.0f}}) {
            for (bool flag : {false, true}) {
                Steel100::Settings s;
                s.gain = knobs[0];
                s.bass = knobs[1];
                s.middle = knobs[2];
                s.treble = knobs[3];
                s.bright = flag;
                s.midBoost = !flag;
                Steel100::ToneNetwork net;
                Steel100::configureToneNetwork(net, fs, s);
                for (double f : {40.0, 200.0, 1000.0, 5000.0, 15000.0}) {
                    const auto digital = measuredResponse(net, f, fs);
                    const auto analog = net.analogResponse(prewarped(f, fs));
                    CHECK(std::abs(digital - analog) < 2.0e-3 * std::abs(analog) + 1.0e-6);
                }
            }
        }
    }
}

TEST_CASE("Steel 100: Middle, Mid boost, Bass, Treble and Bright move the response like the amp's") {
    Steel100::Settings s;
    s.gain = 10.0f;
    // Middle: from 0 to 10 lifts the mid scoop by several dB and leaves the extremes alone.
    Steel100::Settings m0 = s, m10 = s;
    m0.middle = 0.0f;
    m10.middle = 10.0f;
    const double midLift = stackDb(m10, 600.0) - stackDb(m0, 600.0);
    std::printf("    Middle 0 to 10 at 600 Hz: %+.1f dB\n", midLift);
    CHECK(midLift > 6.0);
    // Mid boost: more mids again on top of the Middle knob at 10.
    Steel100::Settings boost = m10;
    boost.midBoost = true;
    const double boostLift = stackDb(boost, 600.0) - stackDb(m10, 600.0);
    std::printf("    Mid boost at 600 Hz (Middle at 10): %+.1f dB\n", boostLift);
    CHECK(boostLift > 2.0);
    // Treble lifts the top, Bass lifts the bottom.
    Steel100::Settings t = s;
    t.treble = 10.0f;
    CHECK(stackDb(t, 4000.0) > stackDb(s, 4000.0) + 3.0);
    Steel100::Settings b = s;
    b.bass = 10.0f;
    CHECK(stackDb(b, 80.0) > stackDb(s, 80.0) + 3.0);
    // Bright: a large lift at low Gain, almost none at full Gain.
    Steel100::Settings low = s;
    low.gain = 3.0f;
    Steel100::Settings lowBright = low;
    lowBright.bright = true;
    Steel100::Settings fullBright = s;
    fullBright.bright = true;
    CHECK(stackDb(lowBright, 5000.0) - stackDb(low, 5000.0) > 6.0);
    CHECK(std::fabs(stackDb(fullBright, 5000.0) - stackDb(s, 5000.0)) < 0.5);
    // Gain at 0 mutes.
    Steel100::Settings off = s;
    off.gain = 0.0f;
    CHECK(stackDb(off, 1000.0) < -60.0);
}

// ------------------------------------------------------------------ the amp

TEST_CASE("Steel 100: silence in gives silence out") {
    // Below -200 dBFS. Unlike Combo 40 it is not exactly zero: the 330 V triode tables keep
    // a rounding-level offset at rest (about 1e-12 V), which the coupling high-pass removes.
    for (double fs : kTestRates) {
        Steel100 amp;
        Steel100::Settings s;
        s.fetInput = true;
        amp.setSettings(s);
        amp.prepare(fs, 64);
        std::vector<float> x(static_cast<std::size_t>(fs * 0.2), 0.0f);
        for (std::size_t pos = 0; pos < x.size(); pos += 64) {
            amp.process(x.data() + pos, static_cast<int>(std::min<std::size_t>(64, x.size() - pos)));
        }
        CHECK(maxAbs(x) < 1.0e-10);
    }
}

TEST_CASE("Steel 100: block size independent with knob and switch changes, never allocates") {
    for (double fs : {48000.0, 96000.0}) {
        std::size_t done = 0;
        auto make = [fs, &done] {
            auto a = std::make_unique<Steel100>();
            a->prepare(fs, 512);
            done = 0;
            return a;
        };
        auto process = [&done](std::unique_ptr<Steel100>& a, const float* in, float* out, int n) {
            if (done % 4096 == 0) {
                Steel100::Settings s;
                const auto k = done / 4096;
                const auto step = static_cast<float>(k);
                s.gain = 3.0f + step;
                s.treble = 8.0f - step;
                s.bass = 2.0f + 0.5f * step;
                s.middle = 7.0f - step;
                s.master = 4.0f + step;
                s.bright = k % 2 == 1;
                s.midBoost = k % 3 == 1;
                s.fetInput = k % 2 == 0;
                s.outputDb = -step;
                a->setSettings(s);
            }
            std::copy(in, in + n, out);
            a->process(out, n);
            done += static_cast<std::size_t>(n);
        };
        checkBlockSizeIndependence(make, process, musicalSignal(24576, fs));
    }
}

TEST_CASE("Steel 100: same small-signal tone at every sample rate") {
    Steel100::Settings s;
    std::array<double, 5> reference{};
    const double freqs[] = {80.0, 300.0, 1000.0, 3000.0, 8000.0};
    for (double fs : {48000.0, 44100.0, 88200.0, 96000.0}) {
        for (std::size_t k = 0; k < 5; ++k) {
            const double db = gainToDb(ampTone<Steel100>(s, fs, freqs[k], 0.001).amplitude / 0.001);
            if (fs == 48000.0) {
                reference[k] = db;
            } else {
                CHECK_NEAR(db, reference[k], 0.3);
            }
        }
    }
}

TEST_CASE("Steel 100: stable and bounded at every rate, setting and input level") {
    for (double fs : kTestRates) {
        for (float level : {0.0f, 5.0f, 10.0f}) {
            Steel100::Settings s;
            s.gain = level;
            s.master = 10.0f;
            s.treble = 10.0f;
            s.bass = 10.0f;
            s.middle = 10.0f;
            s.bright = true;
            s.midBoost = true;
            s.fetInput = true;
            Steel100 amp;
            amp.setSettings(s);
            amp.prepare(fs, 64);
            const auto n = static_cast<std::size_t>(fs * 0.3);
            std::vector<float> x(3 * n, 0.0f);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = (i / static_cast<std::size_t>(fs / 110.0)) % 2 == 0 ? 1.0f : -1.0f;
            }
            const auto nz = noise(n, 1.0);
            std::copy(nz.begin(), nz.end(), x.begin() + static_cast<std::ptrdiff_t>(n));
            for (std::size_t pos = 0; pos < x.size(); pos += 64) {
                amp.process(x.data() + pos, static_cast<int>(std::min<std::size_t>(64, x.size() - pos)));
            }
            CHECK(allFinite(x));
            std::printf("    %6.0f Hz, gain %4.1f: peak %.3f, tail rms %.2g\n", fs,
                        static_cast<double>(level), maxAbs(x), rms(x.data() + x.size() - 4096, 4096));
            CHECK(maxAbs(x) < 0.8);
            CHECK(rms(x.data() + x.size() - 4096, 4096) < 1.0e-3);
        }
    }
}

TEST_CASE("Steel 100: far more clean headroom than Combo 40, and the FET input adds clean gain") {
    // Voicing targets (plan: "very high headroom clean"). Same -10 dBFS strum as the
    // Combo 40 test.
    const double fs = 96000.0;
    const double in = dbToGain(-10.0);
    std::printf("    220 Hz at -10 dBFS: gain, master, output dBFS, THD %% (normal / FET)\n");
    for (float gain : {3.0f, 5.0f, 7.0f, 10.0f}) {
        for (float master : {5.0f, 10.0f}) {
            Steel100::Settings s;
            s.gain = gain;
            s.master = master;
            const ToneResult r = ampTone<Steel100>(s, fs, 220.0, in);
            s.fetInput = true;
            const ToneResult f = ampTone<Steel100>(s, fs, 220.0, in);
            std::printf("      %4.1f %4.1f   %6.1f %6.2f   /  %6.1f %6.2f\n", static_cast<double>(gain),
                        static_cast<double>(master), gainToDb(r.amplitude), 100.0 * r.thd,
                        gainToDb(f.amplitude), 100.0 * f.thd);
            // The default and lower Gain settings stay clean on the normal input, at either Master.
            if (gain <= 5.0f) CHECK(r.thd < 0.025);
        }
    }
    // At the same output level as Combo 40 on the edge of breakup (volume 6), Steel 100
    // is much cleaner.
    Combo40::Settings c;
    c.volume = 6.0f;
    const ToneResult combo = ampTone<Combo40>(c, fs, 220.0, in);
    Steel100::Settings s;
    s.gain = 5.0f;
    s.master = 10.0f;
    const ToneResult steel = ampTone<Steel100>(s, fs, 220.0, in);
    std::printf(
        "    Combo 40 vol 6: %.1f dBFS, %.1f %% THD; Steel 100 gain 5 master 10: %.1f dBFS, %.2f %%\n",
        gainToDb(combo.amplitude), 100.0 * combo.thd, gainToDb(steel.amplitude), 100.0 * steel.thd);
    CHECK(steel.amplitude > 0.7 * combo.amplitude);
    CHECK(steel.thd < 0.5 * combo.thd);
    // FET input: more level for a small signal, still clean.
    Steel100::Settings quiet;
    quiet.master = 3.0f;
    const ToneResult normal = ampTone<Steel100>(quiet, fs, 220.0, 0.01);
    quiet.fetInput = true;
    const ToneResult fet = ampTone<Steel100>(quiet, fs, 220.0, 0.01);
    const double fetDb = gainToDb(fet.amplitude / normal.amplitude);
    std::printf("    FET input lift at 220 Hz: %+.1f dB\n", fetDb);
    CHECK(fetDb > 6.0 && fetDb < 12.0);
    CHECK(fet.thd < 0.01);
    // Flat out it still breaks up: Gain and Master at 10 with the FET input.
    Steel100::Settings full;
    full.gain = 10.0f;
    full.master = 10.0f;
    full.fetInput = true;
    CHECK(ampTone<Steel100>(full, fs, 220.0, in).thd > 0.05);
}

TEST_CASE("Steel 100: an impulse comes out from the first sample (no added latency)") {
    for (double fs : kTestRates) {
        Steel100::Settings s;
        s.gain = 10.0f;
        s.treble = 10.0f;
        Steel100 amp;
        amp.setSettings(s);
        amp.prepare(fs, 32);
        std::vector<float> x(32, 0.0f);
        x[0] = 0.01f;
        amp.process(x.data(), 32);
        CHECK(x[0] != 0.0f);
    }
}

TEST_CASE("aliasing: Steel 100 driven hard keeps audible aliasing low") {
    // Gain, Master, Treble at 10 and Bright on, as hard as the Combo 40 test drives its amp.
    for (double fs : kTestRates) {
        const auto lowBin = static_cast<std::size_t>(std::lround(1200.0 * 8192.0 / fs)) | 1u;
        const auto highBin = static_cast<std::size_t>(std::lround(4800.0 * 8192.0 / fs)) | 1u;
        for (std::size_t bin : {lowBin, highBin}) {
            AmpStage<Steel100> stage;
            Steel100::Settings s;
            s.gain = 10.0f;
            s.master = 10.0f;
            s.treble = 10.0f;
            s.bright = true;
            stage.amp.setSettings(s);
            stage.amp.prepare(fs, 64);
            const double db = ampAliasingDb(stage, fs, bin, 0.5f);
            // The FET input adds 9 dB, so the same drive into the tubes comes from a quieter
            // guitar: -15 dBFS on the FET input is about -6 dBFS on the normal input.
            AmpStage<Steel100> fetStage;
            s.fetInput = true;
            fetStage.amp.setSettings(s);
            fetStage.amp.prepare(fs, 64);
            const double fetDb = ampAliasingDb(fetStage, fs, bin, 0.18f);
            std::printf("    %6.0f Hz, tone %7.1f Hz (%dx): %6.1f dB, FET input %6.1f dB\n", fs,
                        static_cast<double>(bin) * fs / 8192.0, ampOversamplingFactor(fs), db, fetDb);
            CHECK(db < (bin == lowBin ? -70.0 : -45.0));
            CHECK(fetDb < (bin == lowBin ? -70.0 : -45.0));
        }
    }
}

// ------------------------------------------------------------------ amp section and engine

TEST_CASE("engine: amp.model switches to Steel 100 with a short crossfade and no click") {
    EnginePtr e{gigrig_create()};
    REQUIRE(e != nullptr);
    REQUIRE(gigrig_prepare(e.get(), 96000.0, 32) == GIGRIG_OK);
    gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_ON, 0.0f);
    CHECK(gigrig_get_param(e.get(), GIGRIG_PARAM_AMP_MODEL) == 0.0f);
    const auto in = sine(96000, 220.0, 96000.0, 0.3);
    std::vector<float> combo(in.size()), mixed(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += 32) {
        gigrig_process(e.get(), in.data() + pos, combo.data() + pos, 32);
    }
    gigrig_reset(e.get());
    // Switch to Steel 100 half way through.
    for (std::size_t pos = 0; pos < in.size(); pos += 32) {
        if (pos == 48000) gigrig_set_param(e.get(), GIGRIG_PARAM_AMP_MODEL, 1.0f);
        gigrig_process(e.get(), in.data() + pos, mixed.data() + pos, 32);
    }
    // Before the switch: Combo 40, bit for bit.
    CHECK(std::equal(combo.begin(), combo.begin() + 48000, mixed.begin()));
    // After the crossfade: a different amp.
    double diff = 0.0;
    for (std::size_t i = 90000; i < 96000; ++i) {
        diff = std::max(diff, std::fabs(static_cast<double>(combo[i]) - static_cast<double>(mixed[i])));
    }
    CHECK(diff > 0.01);
    // No click: the largest sample-to-sample step around the switch is no bigger than the
    // steady signal's own steps.
    const double steadyStep = std::max(largestStep(mixed, 40000, 47000), largestStep(mixed, 90000, 95000));
    const double switchStep = largestStep(mixed, 47990, 50500);
    std::printf("    largest step: steady %.4f, around the switch %.4f\n", steadyStep, switchStep);
    CHECK(switchStep < 1.2 * steadyStep);
}

TEST_CASE("engine: amp section is block size independent across a model change") {
    for (double fs : {48000.0, 96000.0}) {
        std::size_t done = 0;
        auto make = [fs, &done] {
            EnginePtr e{gigrig_create()};
            gigrig_prepare(e.get(), fs, 512);
            gigrig_set_param(e.get(), GIGRIG_PARAM_CAB_ON, 0.0f);
            done = 0;
            return e;
        };
        auto process = [&done](EnginePtr& e, const float* in, float* out, int n) {
            if (done == 8192) gigrig_set_param(e.get(), GIGRIG_PARAM_AMP_MODEL, 1.0f);
            if (done == 16384) gigrig_set_param(e.get(), GIGRIG_PARAM_AMP_MODEL, 0.0f);
            gigrig_process(e.get(), in, out, n);
            done += static_cast<std::size_t>(n);
        };
        checkBlockSizeIndependence(make, process, musicalSignal(24576, fs));
    }
}

TEST_CASE("benchmark: Steel 100 CPU at 96 kHz with 32-sample blocks") {
    for (double fs : {48000.0, 96000.0}) {
        Steel100 amp;
        Steel100::Settings s;
        s.fetInput = true;
        amp.setSettings(s);
        amp.prepare(fs, 32);
        auto x = musicalSignal(static_cast<std::size_t>(fs * 2.0), fs);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos + 32 <= x.size(); pos += 32) {
            amp.process(x.data() + pos, 32);
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("    %6.0f Hz, %dx: %.2f%% of one core for real time\n", fs, ampOversamplingFactor(fs),
                    100.0 * seconds / 2.0);
    }
}
