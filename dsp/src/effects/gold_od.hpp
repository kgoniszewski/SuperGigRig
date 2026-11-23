// gold_od.hpp - Gold OD, the mid-gain "transparent" overdrive of the Pre FX
// section (plan section 4, row 3).
//
// The circuit family sums a clean path and a clipped path. A dual-gang Gain
// control raises the drive of the clipped path while it turns the clean path
// down, so low settings sound like a fattened clean boost and high settings
// like an overdrive that still keeps the attack of the clean signal. The
// clipped path is band-limited before hard germanium-style diode clipping,
// and an active treble control follows the sum.
//
// Both paths run at the oversampled rate: the clean path has to see the same
// oversampling filter delay as the clipped path, or their sum would comb.
#pragma once

#include <vector>

#include "blocks/biquad.hpp"
#include "blocks/oversampler.hpp"
#include "blocks/waveshaper.hpp"
#include "effect_common.hpp"

namespace gigrig {

class GoldOd {
public:
    struct Settings {
        bool on = false;
        float gain = 3.0f;  // 0 to 10
        float levelDb = 0.0f;
        float treble = 5.0f;  // 0 to 10, 5 is flat
    };

    // Clipping level of the diode pair (full scale = 1).
    static constexpr double kClipLevel = 0.3;
    // Treble shelf corner and range.
    static constexpr double kTrebleHz = 1200.0;
    static constexpr double kTrebleRangeDb = 10.0;

    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Gain knob to the clipped path's drive, and to the two path levels.
    static double dirtyDrive(float gainKnob) noexcept;
    static double cleanLevel(float gainKnob) noexcept;
    static double dirtyLevel(float gainKnob) noexcept;

private:
    void resetState() noexcept;
    void computeWet(const float* in, float* wet, int n) noexcept;
    void updateTreble(float knob) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    BypassFade fade_;
    LinearSmoother gain_;    // knob position
    LinearSmoother level_;   // linear gain
    LinearSmoother treble_;  // knob position
    ControlClock clock_;
    float appliedTreble_ = 5.0f;
    Oversampler os_;
    Biquad inputHighPass_;
    Biquad dirtyHighPass_;  // at the high rate: bass kept out of the clipper
    Biquad dirtyLowPass_;   // at the high rate: the gain stage's high cut
    Biquad outputLowPass_;
    Biquad trebleShelf_;
    WaveshaperTable diodeCurve_;
    AdaaWaveshaper<shapes::Table> clipper_;
    std::vector<float> gainBuffer_;
    std::vector<float> wet_;
};

}  // namespace gigrig
