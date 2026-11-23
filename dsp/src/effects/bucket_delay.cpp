#include "bucket_delay.hpp"

#include <cmath>

namespace gigrig {

void BucketDelay::prepare(double sampleRate, int maxBlockSize) {
    (void)maxBlockSize;  // runs sample by sample, no block buffers
    sampleRate_ = sampleRate;
    line_.prepare(static_cast<int>(std::ceil(kMaxTimeMs * 0.001 * sampleRate)) + 4);
    delaySamples_.prepare(sampleRate, kTimeGlideSeconds);
    send_.prepare(sampleRate, BypassFade::kFadeSeconds);
    feedback_.prepare(sampleRate, 0.02);
    blend_.prepare(sampleRate, 0.02);
    // The NE570-style compander's detectors: fast enough to follow the pick,
    // slow enough not to distort low notes.
    compressorEnv_.setMode(EnvelopeFollower::Mode::Peak);
    compressorEnv_.prepare(sampleRate);
    compressorEnv_.setTimes(0.004, 0.06);
    expanderEnv_.setMode(EnvelopeFollower::Mode::Peak);
    expanderEnv_.prepare(sampleRate);
    expanderEnv_.setTimes(0.004, 0.06);
    feedbackHighPass_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 50.0));
    reset();
}

void BucketDelay::reset() noexcept {
    line_.reset();
    const double d = static_cast<double>(settings_.timeMs) * 0.001 * sampleRate_;
    delaySamples_.snap(static_cast<float>(d));
    updateFilters(d);
    send_.snap(settings_.on ? 1.0f : 0.0f);
    feedback_.snap(settings_.feedbackPercent * 0.01f);
    blend_.snap(settings_.blendPercent * 0.01f);
    clock_.reset();
    compressorEnv_.reset();
    expanderEnv_.reset();
    antiAlias_.reset();
    reconstruction_.reset();
    feedbackHighPass_.reset();
    saturator_.reset();
    quietSamples_ = 0;
    idle_ = !settings_.on;
}

double BucketDelay::bandwidthHz(double timeMs) noexcept {
    // A 4096-stage chip: bandwidth scales with the clock, i.e. with 1 / time,
    // softened so that the longest delays stay usable (about 2 kHz at 800 ms).
    return std::clamp(9000.0 * std::sqrt(40.0 / std::max(timeMs, 1.0)), 2000.0, 9000.0);
}

void BucketDelay::updateFilters(double delaySamples) noexcept {
    appliedDelay_ = delaySamples;
    const double fc = bandwidthHz(delaySamples * 1000.0 / sampleRate_);
    antiAlias_.setCoefficients(BiquadCoefficients::lowPass(sampleRate_, fc, 0.7071));
    reconstruction_.setCoefficients(BiquadCoefficients::lowPass(sampleRate_, fc, 0.7071));
}

void BucketDelay::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    send_.setTarget(s.on ? 1.0f : 0.0f);
    if (s.on) {
        idle_ = false;
        quietSamples_ = 0;
    }
    if (idle_) {
        return;  // off and silent: the dry signal passes untouched
    }
    delaySamples_.setTarget(static_cast<float>(static_cast<double>(s.timeMs) * 0.001 * sampleRate_));
    feedback_.setTarget(s.feedbackPercent * 0.01f);
    blend_.setTarget(s.blendPercent * 0.01f);
    // Tail is over once nothing above -120 dB has entered the line for longer than the line is long.
    const int quietLimit = line_.maxDelay() + 1;
    constexpr double kLimit = kFeedbackLimit;
    constexpr double kRef = kCompanderReference;

    for (int i = 0; i < n; ++i) {
        if (idle_) {
            continue;  // went idle earlier in this block: io[i] is already the dry signal
        }
        const double d = static_cast<double>(delaySamples_.next());
        if (clock_.tick() && d != appliedDelay_) {
            updateFilters(d);
        }
        const float x = io[i];

        // Out of the chip: reconstruction filter, then the expander undoes the
        // compressor (gain = level / reference, the inverse of 2:1 compression).
        const float chip = reconstruction_.process(line_.readCubic(d));
        const double expLevel = static_cast<double>(expanderEnv_.process(chip));
        const double expGain = std::min(expLevel / kRef, 4.0);
        const auto wet = static_cast<float>(static_cast<double>(chip) * expGain);

        // Into the chip: input plus saturated feedback, compressed 2:1, band limited.
        const double fb =
            static_cast<double>(feedback_.next()) * static_cast<double>(feedbackHighPass_.process(wet));
        const double fbSat =
            kLimit * static_cast<double>(saturator_.process(static_cast<float>(fb / kLimit)));
        const double into = static_cast<double>(send_.next()) * static_cast<double>(x) + fbSat;
        const double compLevel =
            std::max(static_cast<double>(compressorEnv_.process(static_cast<float>(into))), 1.0e-3);
        const double compGain = std::min(std::sqrt(kRef / compLevel), 8.0);
        const float written = antiAlias_.process(static_cast<float>(into * compGain));
        line_.write(written);

        io[i] = x + blend_.next() * wet;

        if (send_.current() == 0.0f && std::fabs(written) < kSilence && std::fabs(wet) < kSilence) {
            if (++quietSamples_ > quietLimit) {
                idle_ = true;
            }
        } else {
            quietSamples_ = 0;
        }
    }
}

}  // namespace gigrig
