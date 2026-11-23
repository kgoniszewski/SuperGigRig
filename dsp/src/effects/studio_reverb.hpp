// studio_reverb.hpp - Studio Reverb, the hall and plate reverb of the Post FX
// section (plan section 4, row 13).
//
// Hall: a 16-line feedback delay network. A Hadamard mixing matrix spreads
// every line into every other, so echo density builds up fast like a large
// room, and each line has a gain and a one-pole damping filter set from the
// Decay time, so highs die faster than lows as in air. Two lines are slowly
// modulated to break up metallic resonances.
// Plate: a figure-of-eight allpass tank after Dattorro's published plate
// design: input diffusers, then two cross-coupled branches of a modulated
// allpass, delay, damping and a second allpass. It is denser and brighter
// from the first moment, as a steel plate is.
//
// Shared: pre-delay, then low cut and high cut on the signal entering the
// reverb. Everything runs in mono (the output is mono). Mix adds the reverb
// to the untouched dry signal, so the dry path has zero latency. Switching
// off, or changing Mode, stops input to the old tank and lets its tail ring
// out; a tank stops running once it is silent.
#pragma once

#include <array>
#include <vector>

#include "blocks/biquad.hpp"
#include "blocks/delay_line.hpp"
#include "blocks/lfo.hpp"
#include "effect_common.hpp"

namespace gigrig {

// Schroeder allpass on a delay line: y = -g x + d, d' = x + g y. Delay is in
// samples at the session rate; an optional modulation moves the read point.
class AllpassDelay {
public:
    void prepare(int maxDelay) { line_.prepare(maxDelay + 4); }
    void reset() noexcept { line_.reset(); }
    float process(float x, double delay, float g) noexcept {
        const float d = line_.readLinear(delay);
        const float v = x + g * d;
        line_.write(v);
        return d - g * v;
    }
    // Reads inside the allpass delay, for the plate's output taps.
    float tap(int delay) const noexcept { return line_.read(delay); }

private:
    DelayLine line_;
};

class HallTank {
public:
    static constexpr int kLines = 16;

    void prepare(double sampleRate);
    void reset() noexcept;
    // Decay is the time for the tail to fall 60 dB at low frequencies; highs
    // fall faster by the damping ratio.
    void setDecay(double seconds) noexcept;
    float process(float x) noexcept;

private:
    double sampleRate_ = 48000.0;
    std::array<DelayLine, kLines> lines_;
    std::array<double, kLines> delays_{};
    std::array<float, kLines> gains_{};
    std::array<float, kLines> dampCoeff_{};
    std::array<float, kLines> dampState_{};
    std::array<AllpassDelay, 4> diffusers_;
    std::array<double, 4> diffuserDelays_{};
    Lfo lfo_;
};

class PlateTank {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setDecay(double seconds) noexcept;
    float process(float x) noexcept;

private:
    // Delay lengths of the published design are given at 29761 Hz.
    int scaled(int samplesAt29761) const noexcept;

    double sampleRate_ = 48000.0;
    float decay_ = 0.5f;
    std::array<AllpassDelay, 4> inputDiffusers_;
    std::array<int, 4> inputDelays_{};
    // Two branches (A and B): modulated allpass, delay, damping, allpass, delay.
    std::array<AllpassDelay, 2> modAllpass_;
    std::array<DelayLine, 2> delay1_;
    std::array<float, 2> damp_{};
    std::array<AllpassDelay, 2> decayAllpass_;
    std::array<DelayLine, 2> delay2_;
    std::array<int, 2> modDelay_{}, delay1Len_{}, decayApDelay_{}, delay2Len_{};
    std::array<float, 2> feedback_{};  // output of each branch, fed to the other
    Lfo lfo_;
    float dampCoeff_ = 0.0005f;
};

class StudioReverb {
public:
    enum Mode { Hall = 0, Plate = 1 };

    struct Settings {
        bool on = false;
        int mode = Hall;
        float mixPercent = 20.0f;
        float preDelayMs = 20.0f;
        float decaySeconds = 2.5f;
        float lowCutHz = 100.0f;
        float highCutHz = 8000.0f;
    };

    static constexpr double kMaxPreDelayMs = 250.0;
    static constexpr float kSilence = 1.0e-6f;

    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    bool isIdle() const noexcept { return !running_[Hall] && !running_[Plate]; }

private:
    void updateFilters() noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    DelayLine preDelay_;
    OnePoleSmoother preDelaySamples_;
    OnePoleSmoother lowCut_;
    OnePoleSmoother highCut_;
    OnePoleSmoother decay_;
    LinearSmoother mix_;
    // Input level into each tank: the selected tank fades in, the other fades out.
    std::array<LinearSmoother, 2> send_;
    std::array<bool, 2> running_{};
    std::array<int, 2> quietSamples_{};
    int quietLimit_ = 0;
    ControlClock clock_;
    float appliedLowCut_ = 0.0f, appliedHighCut_ = 0.0f, appliedDecay_ = 0.0f;
    Biquad lowCutFilter_;
    Biquad highCutFilter_;
    HallTank hall_;
    PlateTank plate_;
};

}  // namespace gigrig
