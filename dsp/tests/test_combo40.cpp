// test_combo40.cpp - work package 4: the RC network solver, the triode stage
// and the Combo 40 amp (plan section 8 tests for a nonlinear block).
#include <gigrig/gigrig.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "amps/combo40.hpp"
#include "amps/triode.hpp"
#include "blocks/rc_network.hpp"
#include "amp_test_utils.hpp"
#include "dsp_test_utils.hpp"

using namespace gigrig;
using namespace gigrig::amp;
using namespace gigrig::test;

namespace {

// Classic closed-form transfer function of the passive treble-bass-middle stack
// (as published by D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender
// Bassman tone stack", DAFx 2006), with an ideal source and no load.
std::complex<double> publishedToneStack(double f, double t, double l, double m, double R1, double R2,
                                        double R3, double R4, double C1, double C2, double C3) {
    const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) -
                      m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) +
                      m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) +
                      l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4) +
                      l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) +
                      (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const double b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) -
                      m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) +
                      m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) +
                      t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4 +
                      t * l * C1 * C2 * C3 * R1 * R2 * R4;
    const double a1 =
        (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) +
                      l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) -
                      m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) +
                      l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4) +
                      (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 +
                       C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    const double a3 =
        l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) -
        m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) +
        m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4) +
        l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
    const std::complex<double> s(0.0, 2.0 * kPi * f);
    return (b1 * s + b2 * s * s + b3 * s * s * s) / (1.0 + a1 * s + a2 * s * s + a3 * s * s * s);
}

}  // namespace

// ------------------------------------------------------------------ RC network

TEST_CASE("RC network: one-pole RC low-pass matches its analog answer") {
    RcNetwork<1, 1, 1> rc;  // source 10k -> node 0, 10 nF to ground, and a 1M load
    rc.setSampleRate(96000.0);
    rc.setSource(0, 10.0e3);
    rc.setResistor(0, 0, RcNetwork<1, 1, 1>::kGround, 1.0e6);
    rc.setCapacitor(0, 0, RcNetwork<1, 1, 1>::kGround, 10.0e-9);
    rc.setOutput(0);
    rc.update();
    const double rp = 10.0e3 * 1.0e6 / (10.0e3 + 1.0e6);
    for (double f : {10.0, 1591.5, 10000.0}) {
        const std::complex<double> expected =
            (1.0e6 / (10.0e3 + 1.0e6)) / (1.0 + std::complex<double>(0.0, 2.0 * kPi * f * rp * 10.0e-9));
        CHECK(std::abs(rc.analogResponse(f) - expected) < 1.0e-12);
    }
}

TEST_CASE("RC network: the digital response is the analog response at the prewarped frequency") {
    // The Combo 40 tone network at several knob settings, at each oversampled rate.
    for (double fs : {176400.0, 192000.0}) {
        for (const auto& knobs :
             {std::array<float, 3>{4.0f, 6.0f, 4.0f}, std::array<float, 3>{10.0f, 0.0f, 10.0f},
              std::array<float, 3>{1.0f, 10.0f, 0.0f}}) {
            for (bool bright : {false, true}) {
                Combo40::Settings s;
                s.volume = knobs[0];
                s.treble = knobs[1];
                s.bass = knobs[2];
                s.bright = bright;
                Combo40::ToneNetwork net;
                Combo40::configureToneNetwork(net, fs, s);
                for (double f : {40.0, 200.0, 1000.0, 5000.0, 15000.0}) {
                    const auto digital = measuredResponse(net, f, fs);
                    const auto analog = net.analogResponse(prewarped(f, fs));
                    CHECK(std::abs(digital - analog) < 2.0e-3 * std::abs(analog) + 1.0e-6);
                }
            }
        }
    }
}

TEST_CASE("tone stack: the circuit solution matches the published closed-form stack") {
    // The same topology as the amp, without source impedance, volume pot or load,
    // compared with the published third-order transfer function. Checks both the
    // node wiring of the stack and the network solver over the treble and bass
    // range. The mid leg is a fixed resistor (m = 1), as on the Combo 40, which has
    // no mid pot.
    enum Node { Plate, TrebleTop, Wiper, BassTop, Slope, MidTop };
    const double R1 = 250e3, R2 = 1e6, R3 = 25e3, R4 = 56e3, C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
    double worst = 0.0;
    for (double t : {0.05, 0.5, 0.95}) {
        for (double l : {0.05, 0.5, 0.95}) {
            for (double m : {1.0}) {
                RcNetwork<6, 5, 3> n;
                using N = RcNetwork<6, 5, 3>;
                n.setSampleRate(192000.0);
                n.setSource(Plate, 1.0e-3);
                n.setOutput(Wiper);
                n.setResistor(0, TrebleTop, Wiper, (1.0 - t) * R1);
                n.setResistor(1, Wiper, BassTop, t * R1);
                n.setResistor(2, Plate, Slope, R4);
                n.setResistor(3, BassTop, MidTop, l * R2);
                n.setResistor(4, MidTop, N::kGround, m * R3);
                n.setCapacitor(0, Plate, TrebleTop, C1);
                n.setCapacitor(1, Slope, BassTop, C2);
                n.setCapacitor(2, Slope, MidTop, C3);
                for (double f = 20.0; f < 20000.0; f *= 1.25) {
                    const auto ours = n.analogResponse(f);
                    const auto published = publishedToneStack(f, t, l, m, R1, R2, R3, R4, C1, C2, C3);
                    worst =
                        std::max(worst, std::fabs(gainToDb(std::abs(ours)) - gainToDb(std::abs(published))));
                }
            }
        }
    }
    std::printf("    largest difference: %.2g dB\n", worst);
    CHECK(worst < 0.01);
}

TEST_CASE("tone stack: knobs and the bright switch move the response the way the amp's do") {
    const double fs = 192000.0;
    auto gainDb = [fs](const Combo40::Settings& s, double f) {
        Combo40::ToneNetwork n;
        Combo40::configureToneNetwork(n, fs, s);
        return gainToDb(std::abs(n.analogResponse(f)));
    };
    Combo40::Settings s;
    s.volume = 10.0f;
    s.treble = 5.0f;
    s.bass = 5.0f;
    // The mid scoop: around 400 to 700 Hz is lower than both 80 Hz and 4 kHz.
    CHECK(gainDb(s, 500.0) < gainDb(s, 80.0) - 3.0);
    CHECK(gainDb(s, 500.0) < gainDb(s, 4000.0) - 3.0);
    // Treble lifts the top, bass lifts the bottom.
    Combo40::Settings t = s;
    t.treble = 10.0f;
    CHECK(gainDb(t, 4000.0) > gainDb(s, 4000.0) + 3.0);
    Combo40::Settings b = s;
    b.bass = 10.0f;
    CHECK(gainDb(b, 80.0) > gainDb(s, 80.0) + 3.0);
    // Bright: a large lift at low volume, almost none at full volume (the cap is shorted out).
    Combo40::Settings low = s;
    low.volume = 3.0f;
    Combo40::Settings lowBright = low;
    lowBright.bright = true;
    Combo40::Settings fullBright = s;
    fullBright.bright = true;
    const double liftLow = gainDb(lowBright, 5000.0) - gainDb(low, 5000.0);
    const double liftFull = gainDb(fullBright, 5000.0) - gainDb(s, 5000.0);
    std::printf("    bright lift at 5 kHz: %.1f dB at volume 3, %.2f dB at volume 10\n", liftLow, liftFull);
    CHECK(liftLow > 6.0);
    CHECK(std::fabs(liftFull) < 0.5);
    // Volume at 0 mutes.
    Combo40::Settings off = s;
    off.volume = 0.0f;
    CHECK(gainDb(off, 1000.0) < -60.0);
}

// ------------------------------------------------------------------ triode

TEST_CASE("triode: 12AX7 stage operating point and gain are those of the real stage") {
    const TriodeOperatingPoint op = solveOperatingPoint(TriodeStageDesign{});
    std::printf("    plate %.1f V, cathode %.2f V, %.2f mA, gain %.1f, output %.0f ohm\n", op.plateVolts,
                op.cathodeVolts, op.plateAmps * 1000.0, op.gain, op.outputOhms);
    // A 100k / 1.5k bypassed 12AX7 stage idles around 1 mA with the cathode near 1.5 V
    // and a gain of 55 to 65.
    CHECK(op.plateAmps > 0.6e-3 && op.plateAmps < 1.4e-3);
    CHECK(op.cathodeVolts > 0.9 && op.cathodeVolts < 2.1);
    CHECK(op.gain < -50.0 && op.gain > -70.0);
    CHECK(op.outputOhms > 25.0e3 && op.outputOhms < 45.0e3);
}

TEST_CASE("triode: transfer curve is inverting, monotonic, asymmetric and saturates at the rails") {
    TriodeStageDesign d;
    const TriodeOperatingPoint op = solveOperatingPoint(d);
    WaveshaperTable table;
    buildTriodeTable(table, d, 40.0, 4097);
    CHECK(std::fabs(table.f(0.0)) < 1.0e-3);
    CHECK(table.F(0.0) == 0.0);
    double prev = table.f(-40.0);
    bool monotonic = true;
    for (double x = -40.0; x <= 40.0; x += 0.01) {
        const double y = table.f(x);
        monotonic = monotonic && y <= prev + 1.0e-9;
        prev = y;
    }
    CHECK(monotonic);
    // Cutoff: the plate rises to B+ and stops.
    CHECK_NEAR(table.f(-30.0), d.supplyVolts - op.plateVolts, 0.5);
    // Small signal slope is the stage gain.
    CHECK_NEAR((table.f(0.01) - table.f(-0.01)) / 0.02, op.gain, 0.05 * std::fabs(op.gain));
    // Asymmetry: grid conduction limits the positive grid swing well before cutoff
    // limits the negative one, so 3 V of grid drive gives unequal plate swings.
    const double down = -table.f(3.0);
    const double up = table.f(-3.0);
    std::printf("    plate swing for +-3 V grid: %.1f V down, %.1f V up\n", down, up);
    CHECK(std::fabs(down - up) > 10.0);
    // Grid conduction: well above 0 V the plate moves far less per grid volt than at idle.
    const double slopeHigh = (table.f(5.01) - table.f(4.99)) / 0.02;
    std::printf("    slope at +5 V grid: %.2f (idle %.1f)\n", slopeHigh, op.gain);
    CHECK(std::fabs(slopeHigh) < 0.1 * std::fabs(op.gain));
}

// ------------------------------------------------------------------ the amp

TEST_CASE("Combo 40: oversampling keeps the internal rate at 176.4 kHz or more") {
    CHECK(Combo40::factorForRate(44100.0) == 4);
    CHECK(Combo40::factorForRate(48000.0) == 4);
    CHECK(Combo40::factorForRate(88200.0) == 2);
    CHECK(Combo40::factorForRate(96000.0) == 2);
    CHECK(Combo40::factorForRate(192000.0) == 1);
}

TEST_CASE("Combo 40: silence in gives silence out") {
    for (double fs : kTestRates) {
        Combo40 amp;
        amp.prepare(fs, 64);
        std::vector<float> x(static_cast<std::size_t>(fs * 0.2), 0.0f);
        for (std::size_t pos = 0; pos < x.size(); pos += 64) {
            amp.process(x.data() + pos, static_cast<int>(std::min<std::size_t>(64, x.size() - pos)));
        }
        CHECK(maxAbs(x) == 0.0);
    }
}

TEST_CASE("Combo 40: block size independent with knob changes, never allocates") {
    for (double fs : {48000.0, 96000.0}) {
        std::size_t done = 0;
        auto make = [fs, &done] {
            auto a = std::make_unique<Combo40>();
            a->prepare(fs, 512);
            done = 0;
            return a;
        };
        // Knobs move at multiples of 512 samples, so every block size sees them at the same sample.
        auto process = [&done](std::unique_ptr<Combo40>& a, const float* in, float* out, int n) {
            if (done % 4096 == 0) {
                Combo40::Settings s;
                const auto step = static_cast<float>(done / 4096);
                s.volume = 3.0f + step;
                s.treble = 8.0f - step;
                s.bass = 2.0f + 0.5f * step;
                s.bright = (done / 4096) % 2 == 1;
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

TEST_CASE("Combo 40: same small-signal tone at every sample rate") {
    Combo40::Settings s;
    std::array<double, 5> reference{};
    const double freqs[] = {80.0, 300.0, 1000.0, 3000.0, 8000.0};
    for (double fs : {48000.0, 44100.0, 88200.0, 96000.0}) {
        for (std::size_t k = 0; k < 5; ++k) {
            const double db = gainToDb(ampTone<Combo40>(s, fs, freqs[k], 0.001).amplitude / 0.001);
            if (fs == 48000.0) {
                reference[k] = db;
            } else {
                CHECK_NEAR(db, reference[k], 0.3);
            }
        }
    }
}

TEST_CASE("Combo 40: stable and bounded at every rate, setting and input level") {
    for (double fs : kTestRates) {
        for (float volume : {0.0f, 5.0f, 10.0f}) {
            Combo40::Settings s;
            s.volume = volume;
            s.treble = 10.0f;
            s.bass = 10.0f;
            s.bright = true;
            Combo40 amp;
            amp.setSettings(s);
            amp.prepare(fs, 64);
            // Full-scale square wave, then full-scale noise, then silence.
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
            std::printf("    %6.0f Hz, volume %4.1f: peak %.3f, tail rms %.2g\n", fs,
                        static_cast<double>(volume), maxAbs(x), rms(x.data() + x.size() - 4096, 4096));
            // Full power is about -9 dBFS; even a full-scale square wave (whose edges
            // overshoot through the transformer) stays clear of digital clipping.
            CHECK(maxAbs(x) < 0.8);
            // The tail decays: 0.3 s after the input stops it is below -60 dBFS.
            CHECK(rms(x.data() + x.size() - 4096, 4096) < 1.0e-3);
        }
    }
}

TEST_CASE("Combo 40: clean at low volume, breaks up as the volume rises") {
    // Voicing targets (plan: "early soft breakup"). The guitar level is -10 dBFS
    // peak, a firm strum from single coils with the interface's gain set sensibly.
    const double fs = 96000.0;
    const double in = dbToGain(-10.0);
    Combo40::Settings s;
    std::printf("    220 Hz at -10 dBFS: volume, output dBFS, THD %%\n");
    double prevLevel = -200.0;
    for (float v : {2.0f, 4.0f, 6.0f, 8.0f, 10.0f}) {
        s.volume = v;
        const ToneResult r = ampTone<Combo40>(s, fs, 220.0, in);
        std::printf("      %4.1f %6.1f %6.1f\n", static_cast<double>(v), gainToDb(r.amplitude),
                    100.0 * r.thd);
        CHECK(gainToDb(r.amplitude) > prevLevel);  // louder with every step
        prevLevel = gainToDb(r.amplitude);
        if (v <= 4.0f) CHECK(r.thd < 0.03);
        if (v >= 8.0f) CHECK(r.thd > 0.1);
    }
}

TEST_CASE("Combo 40: an impulse comes out from the first sample (no added latency)") {
    for (double fs : kTestRates) {
        Combo40::Settings s;
        s.volume = 10.0f;
        s.treble = 10.0f;
        Combo40 amp;
        amp.setSettings(s);
        amp.prepare(fs, 32);
        std::vector<float> x(32, 0.0f);
        x[0] = 0.01f;
        amp.process(x.data(), 32);
        // The response starts at the first sample: nothing is buffered or looked ahead.
        // (Its shape after that is the circuit's own phase response, as on the amp.)
        CHECK(x[0] != 0.0f);
    }
}

TEST_CASE("aliasing: Combo 40 driven hard keeps audible aliasing low") {
    // Full volume, full treble and bright, -6 dBFS input: about as hard as this amp
    // can be pushed. Limits are set a few dB above the values measured on Linux CI.
    for (double fs : kTestRates) {
        const auto lowBin = static_cast<std::size_t>(std::lround(1200.0 * 8192.0 / fs)) | 1u;
        const auto highBin = static_cast<std::size_t>(std::lround(4800.0 * 8192.0 / fs)) | 1u;
        for (std::size_t bin : {lowBin, highBin}) {
            AmpStage<Combo40> stage;
            Combo40::Settings s;
            s.volume = 10.0f;
            s.treble = 10.0f;
            s.bright = true;
            stage.amp.setSettings(s);
            stage.amp.prepare(fs, 64);
            const double db = ampAliasingDb(stage, fs, bin, 0.5f);
            std::printf("    %6.0f Hz, tone %7.1f Hz (%dx): %6.1f dB\n", fs,
                        static_cast<double>(bin) * fs / 8192.0, Combo40::factorForRate(fs), db);
            CHECK(db < (bin == lowBin ? -70.0 : -45.0));
        }
    }
}

TEST_CASE("engine: Combo 40 is in the chain by default and amp.on bypasses it") {
    GigRigEngine* e = gigrig_create();
    REQUIRE(e != nullptr);
    REQUIRE(gigrig_prepare(e, 96000.0, 32) == GIGRIG_OK);
    CHECK(gigrig_get_param(e, GIGRIG_PARAM_AMP_ON) == 1.0f);
    CHECK(gigrig_latency_samples(e) == 0);
    const auto in = sine(9600, 440.0, 96000.0, 0.3);
    std::vector<float> out(in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += 32) {
        gigrig_process(e, in.data() + pos, out.data() + pos, 32);
    }
    CHECK(maxDifference(out, in) > 0.01);
    CHECK(maxAbs(out) > 0.01);
    gigrig_set_param(e, GIGRIG_PARAM_AMP_ON, 0.0f);
    for (std::size_t pos = 0; pos < in.size(); pos += 32) {
        gigrig_process(e, in.data() + pos, out.data() + pos, 32);
    }
    CHECK(out == in);
    gigrig_destroy(e);
}

TEST_CASE("benchmark: Combo 40 CPU at 96 kHz with 32-sample blocks") {
    // Printed, not checked: debug and sanitiser builds are far slower than release.
    for (double fs : {48000.0, 96000.0}) {
        Combo40 amp;
        Combo40::Settings s;
        s.volume = 6.0f;
        amp.setSettings(s);
        amp.prepare(fs, 32);
        auto x = musicalSignal(static_cast<std::size_t>(fs * 2.0), fs);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos + 32 <= x.size(); pos += 32) {
            amp.process(x.data() + pos, 32);
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("    %6.0f Hz, %dx: %.2f%% of one core for real time\n", fs, Combo40::factorForRate(fs),
                    100.0 * seconds / 2.0);
    }
}
