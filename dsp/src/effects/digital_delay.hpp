// digital_delay.hpp - Digital Delay, the 1980s rack-style delay of the Post FX
// section (plan section 4, row 12).
//
// A clean delay up to 2 s with a low-pass filter in the feedback loop: Hardness
// sets how bright the repeats stay, from a dark, tape-like fade to the crisp,
// full-bandwidth repeats of an early digital rack. Time is either free (ms) or
// locked to the tempo (Sync), where it is a note value of the session tempo;
// tap tempo in the app writes that same tempo parameter.
//
// Moving Time glides the read position smoothly (a short pitch bend, as on
// the rack units) instead of jumping, so there is no zipper noise or click.
// The dry signal passes untouched and with zero latency. Switching off stops
// new input and lets the repeats ring out; once silent the block stops running.
#pragma once

#include "blocks/biquad.hpp"
#include "blocks/delay_line.hpp"
#include "effect_common.hpp"

namespace gigrig {

class DigitalDelay {
public:
    enum Sync { Free = 0, Tempo = 1 };

    // Note values for tempo sync, in beats (quarter notes).
    static constexpr int kDivisionCount = 7;
    static constexpr double kDivisionBeats[kDivisionCount] = {
        2.0,        // 1/2
        1.5,        // dotted 1/4
        1.0,        // 1/4
        0.75,       // dotted 1/8
        2.0 / 3.0,  // 1/4 triplet
        0.5,        // 1/8
        0.25,       // 1/16
    };

    struct Settings {
        bool on = false;
        float timeMs = 400.0f;
        float feedbackPercent = 30.0f;
        float mixPercent = 25.0f;
        float hardness = 5.0f;  // 0 to 10
        int sync = Free;
        int division = 3;  // dotted 1/8
        float tempoBpm = 120.0f;
    };

    static constexpr double kMaxTimeMs = 2000.0;
    static constexpr double kTimeGlideSeconds = 0.08;
    static constexpr double kMaxGlideRate = 0.5;
    static constexpr float kSilence = 1.0e-6f;

    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Delay time in ms the settings ask for (free time, or the synced note value).
    static double effectiveTimeMs(const Settings& s) noexcept;
    // Hardness knob to the feedback low-pass corner.
    static double hardnessCutoff(float hardness) noexcept;

    bool isIdle() const noexcept { return idle_; }

private:
    double sampleRate_ = 48000.0;
    Settings settings_;
    DelayLine line_;
    // Read position glide: exponential, but never faster than kMaxGlideRate
    // samples per sample, so even a big jump (tap tempo, a fast knob turn)
    // bends the repeats' pitch by at most that ratio instead of squealing.
    double delay_ = 0.0;
    double delayTarget_ = 0.0;
    double glideCoeff_ = 0.0;
    OnePoleSmoother cutoff_;
    LinearSmoother send_;
    LinearSmoother feedback_;
    LinearSmoother mix_;
    ControlClock clock_;
    Biquad loopLowPass_;
    Biquad loopHighPass_;  // keeps DC out of the loop
    float appliedCutoff_ = 0.0f;
    int quietSamples_ = 0;
    bool idle_ = true;
};

}  // namespace gigrig
