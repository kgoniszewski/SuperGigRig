// steel100.hpp - "Steel 100", a rare 100 W high-headroom clean head (plan sections 4 and 5).
//
// Circuit basis: the general topology of the American boutique "steel string" clean
// heads, themselves descended from the AB763 family. Exact values of the original are
// not public, so the values below are the family's typical ones, voiced by ear (plan
// section 5). Signal path, all at the oversampled rate:
//
//   input (grid stopper and Miller capacitance)
//   optional FET input: a JFET stage before the first tube, about +9 dB of very clean
//     gain with a slight treble lift, clipping only far above guitar level
//   V1, 12AX7 gain stage on a high (330 V) supply
//   tone stack: treble, bass and a real Middle pot, with the Mid boost switch adding
//     resistance in the mid leg; then the 1M audio-taper Gain pot with the Bright cap
//     across it and the Miller capacitance of V2 at its wiper. One RC network solved
//     exactly (blocks/rc_network.hpp), so the stack, the Gain pot and the Bright cap
//     interact as on the amp.
//   V2, 12AX7 gain stage with a hotter cathode
//   fixed deep voicing: a low shelf that these heads build in, lifting the bottom octave
//   Master volume before the phase inverter
//   push-pull power section: four 6L6-style tubes, solid-state rectifier (stiff supply,
//     little sag), strong negative feedback, presence set at the factory
//
// Controls follow the front panel: Input (normal or FET), Bright, Mid boost (switches),
// Gain, Bass, Middle, Treble, Master (0 to 10), and Output in dB.
//
// Oversampling as in Combo 40: 4x at 44.1 and 48 kHz, 2x at 88.2 and 96 kHz.
#pragma once

#include "amps/amp_common.hpp"
#include "amps/power_amp.hpp"
#include "amps/triode.hpp"
#include "blocks/biquad.hpp"
#include "blocks/oversampler.hpp"
#include "blocks/rc_network.hpp"
#include "blocks/smoother.hpp"
#include "blocks/waveshaper.hpp"

namespace gigrig::amp {

class Steel100 {
public:
    struct Settings {
        bool fetInput = false;
        bool bright = false;
        bool midBoost = false;
        float gain = 5.0f;
        float bass = 5.0f;
        float middle = 5.0f;
        float treble = 6.0f;
        float master = 5.0f;
        float outputDb = 0.0f;
    };

    // Not real-time safe.
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // Real-time safe. Knobs glide to their new values; switches act at once.
    void setSettings(const Settings& s) noexcept;

    // Real-time safe, in place, any block size up to the prepared maximum.
    void process(float* data, int numSamples) noexcept;

    int oversamplingFactor() const noexcept { return oversampler_.factor(); }

    // Volts at the amp input for a full-scale (1.0) digital sample (same as Combo 40).
    static constexpr double kInputVoltsAtFullScale = 1.5;

    // The tone stack and Gain network, for measurement in tests: node voltages per volt at
    // the plate of V1, for the given settings at the given (oversampled) rate.
    using ToneNetwork = RcNetwork<7, 7, 5>;
    static void configureToneNetwork(ToneNetwork& network, double sampleRate, const Settings& s) noexcept;

private:
    void updateControls() noexcept;
    void processOversampled(float* hi, int n) noexcept;

    static constexpr int kControlInterval = 16;  // base-rate samples between knob coefficient updates

    Oversampler oversampler_;
    Biquad inputFilter_;
    AdaaWaveshaper<shapes::SoftSqrt> fet_;
    Biquad fetTone_;
    TriodeStage v1_;
    TriodeStage v2_;
    ToneNetwork tone_;
    Biquad coupling_;
    Biquad deep_;
    PushPullPowerAmp power_;

    OnePoleSmoother gain_;
    OnePoleSmoother bass_;
    OnePoleSmoother middle_;
    OnePoleSmoother treble_;
    OnePoleSmoother master_;  // runs at the oversampled rate
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
