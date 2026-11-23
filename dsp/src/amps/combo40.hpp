// combo40.hpp - "Combo 40", a 40 W clean American combo amp (plan sections 4 and 5).
//
// Circuit basis: the public AB763-family schematic, vibrato channel, without
// its tremolo and reverb circuits. Signal path, all at the oversampled rate:
//
//   input (grid stopper and Miller capacitance of V1)
//   V1, 12AX7 gain stage (100k plate, 1.5k cathode, fully bypassed)
//   tone stack: 250 pF treble cap, 100k slope resistor, 0.1 uF bass and
//     0.047 uF mid caps, 250k audio-taper treble and bass pots, mid fixed at
//     6.8k; then the 1M audio-taper volume pot with the 120 pF bright cap
//     across it and the Miller capacitance of V2 at its wiper. All of this is
//     one RC network solved exactly (blocks/rc_network.hpp), so the source
//     impedance of V1, the stack, the volume pot and the bright cap interact
//     as on the amp: the bright cap matters most at low volume, and the treble
//     softens as the volume pot's own resistance rises toward mid travel.
//   V2, 12AX7 gain stage, its grid driven from the volume wiper
//   coupling cap and the level loss of the dropped reverb mixing network
//   phase inverter (long-tail pair: symmetric soft clip)
//   push-pull 6L6-style pair in class AB: crossover region and plate
//     saturation, with tube-rectifier supply sag
//   output transformer: band limit and low-frequency core saturation
//   negative feedback from the speaker winding into the phase inverter
//
// Controls follow the front panel: Bright (switch), Volume, Treble, Bass
// (0 to 10, like the knobs), plus an Output level in dB that scales the
// final signal and does not change the amp's character.
//
// Oversampling: the internal rate is kept at or above 176.4 kHz: 4x at
// 44.1 and 48 kHz, 2x at 88.2 and 96 kHz, none above. The target setting is
// 96 kHz with 32-sample buffers (2026-11-23), where 2x gives the same
// internal rate and aliasing as 4x at 48 kHz for half the cost of 4x.
#pragma once

#include "amps/triode.hpp"
#include "blocks/biquad.hpp"
#include "blocks/oversampler.hpp"
#include "blocks/rc_network.hpp"
#include "blocks/smoother.hpp"
#include "blocks/waveshaper.hpp"

namespace gigrig::amp {

// Push-pull power section: phase inverter, output pair, rectifier sag,
// output transformer and the global negative feedback loop. Signal in
// phase-inverter grid volts; output normalised so 1.0 is full power.
class Combo40PowerAmp {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    float process(float input) noexcept;

    float supply() const noexcept { return static_cast<float>(supply_); }

private:
    AdaaWaveshaper<shapes::Table> inverter_;
    AdaaWaveshaper<shapes::Table> outputPair_;
    Biquad transformerLowPass_;
    double sampleRate_ = 48000.0;
    double sagAttack_ = 0.0;
    double sagRelease_ = 0.0;
    double coreBase_ = 0.0;  // primary-inductance high-pass coefficient at rest
    double fluxScale_ = 0.0;
    double fluxLeak_ = 0.0;
    double supplyEnvelope_ = 0.0;
    double supply_ = 1.0;
    double coreLow_ = 0.0;
    double flux_ = 0.0;
    double feedback_ = 0.0;
};

class Combo40 {
public:
    // Knob values; the engine passes parameter values straight through.
    struct Settings {
        bool bright = false;
        float volume = 4.0f;
        float treble = 6.0f;
        float bass = 4.0f;
        float outputDb = 0.0f;
    };

    // Not real-time safe.
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // Real-time safe. Knobs glide to their new values; Bright switches at once.
    void setSettings(const Settings& s) noexcept;

    // Real-time safe, in place, any block size up to the prepared maximum.
    void process(float* data, int numSamples) noexcept;

    int oversamplingFactor() const noexcept { return oversampler_.factor(); }

    // Oversampling factor used at a sample rate.
    static int factorForRate(double sampleRate) noexcept;

    // Volts at the amp input for a full-scale (1.0) digital sample. The input
    // level parameter before the amp moves this, as a hotter pickup would.
    static constexpr double kInputVoltsAtFullScale = 1.5;

    // Knob rotation (0 to 10) to the fraction of an audio-taper pot (10% at mid travel).
    static double audioTaper(double knob) noexcept;

    // The tone stack and volume network, for measurement in tests: node voltages per volt at
    // the plate of V1, for the given knob settings at the given (oversampled) rate.
    using ToneNetwork = RcNetwork<7, 7, 5>;
    static void configureToneNetwork(ToneNetwork& network, double sampleRate, const Settings& s) noexcept;

private:
    void updateToneNetwork() noexcept;
    void processOversampled(float* hi, int n) noexcept;

    static constexpr int kControlInterval = 16;  // base-rate samples between knob coefficient updates

    Oversampler oversampler_;
    Biquad inputFilter_;
    TriodeStage v1_;
    TriodeStage v2_;
    ToneNetwork tone_;
    Biquad coupling_;
    Combo40PowerAmp power_;

    OnePoleSmoother volume_;
    OnePoleSmoother treble_;
    OnePoleSmoother bass_;
    LinearSmoother output_;
    Settings settings_;
    Settings applied_;
    float outputDbTarget_ = 0.0f;
    double sampleRate_ = 48000.0;
    double highRate_ = 192000.0;
    int maxBlock_ = 0;
    int controlPhase_ = 0;
};

}  // namespace gigrig::amp
