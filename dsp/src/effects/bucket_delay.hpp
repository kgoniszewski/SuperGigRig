// bucket_delay.hpp - Bucket Delay, the analog bucket-brigade delay of the Pre
// FX section (plan section 4, row 5).
//
// A bucket-brigade chip has a fixed number of stages, so longer delays need a
// slower clock, and the anti-alias and reconstruction filters around the chip
// have to follow the clock down: long delays are darker. The chip's small
// dynamic range is stretched by a 2:1 compander (compress before the chip,
// expand after it), which breathes a little on decaying repeats. The
// feedback path saturates gently, so feedback above 100% self-oscillates at a
// bounded level instead of running away. Moving Time glides the clock, which
// bends the pitch of the repeats as on the hardware.
//
// The dry signal passes untouched and with zero latency; Blend adds the
// repeats. Switching the pedal off stops new input but lets the repeats ring
// out (trails); once they are silent the block stops running.
#pragma once

#include <vector>

#include "blocks/biquad.hpp"
#include "blocks/delay_line.hpp"
#include "blocks/envelope.hpp"
#include "blocks/waveshaper.hpp"
#include "effect_common.hpp"

namespace gigrig {

class BucketDelay {
public:
    struct Settings {
        bool on = false;
        float timeMs = 350.0f;
        float feedbackPercent = 35.0f;
        float blendPercent = 35.0f;
    };

    static constexpr double kMaxTimeMs = 800.0;
    static constexpr double kTimeGlideSeconds = 0.12;
    static constexpr double kFeedbackLimit = 0.8;  // level where the feedback path saturates
    static constexpr double kCompanderReference = 0.25;
    static constexpr float kSilence = 1.0e-6f;  // -120 dBFS

    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Bandwidth of the chip's filters for a delay time: brighter for short delays.
    static double bandwidthHz(double timeMs) noexcept;

    bool isIdle() const noexcept { return idle_; }

private:
    void updateFilters(double delaySamples) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    DelayLine line_;
    OnePoleSmoother delaySamples_;
    LinearSmoother send_;
    LinearSmoother feedback_;
    LinearSmoother blend_;
    ControlClock clock_;
    EnvelopeFollower compressorEnv_;
    EnvelopeFollower expanderEnv_;
    Biquad antiAlias_;
    Biquad reconstruction_;
    Biquad feedbackHighPass_;  // keeps DC and rumble from building up in the loop
    AdaaWaveshaper<shapes::Tanh> saturator_;
    double appliedDelay_ = 0.0;
    int quietSamples_ = 0;
    bool idle_ = true;
};

}  // namespace gigrig
