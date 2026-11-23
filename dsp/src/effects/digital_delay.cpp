#include "digital_delay.hpp"

#include <cmath>

namespace gigrig {

double DigitalDelay::effectiveTimeMs(const Settings& s) noexcept {
    double ms = static_cast<double>(s.timeMs);
    if (s.sync == Tempo) {
        const int d = std::clamp(s.division, 0, kDivisionCount - 1);
        const double bpm = std::max(static_cast<double>(s.tempoBpm), 1.0);
        ms = 60000.0 / bpm * kDivisionBeats[d];
    }
    return std::clamp(ms, 1.0, kMaxTimeMs);
}

double DigitalDelay::hardnessCutoff(float hardness) noexcept {
    return expSweep(1500.0, 18000.0, knobFraction(hardness));
}

void DigitalDelay::prepare(double sampleRate, int maxBlockSize) {
    (void)maxBlockSize;  // runs sample by sample, no block buffers
    sampleRate_ = sampleRate;
    line_.prepare(static_cast<int>(std::ceil(kMaxTimeMs * 0.001 * sampleRate)) + 4);
    glideCoeff_ = 1.0 - onePoleCoefficient(kTimeGlideSeconds, sampleRate);
    cutoff_.prepare(sampleRate, 0.03);
    send_.prepare(sampleRate, BypassFade::kFadeSeconds);
    feedback_.prepare(sampleRate, 0.02);
    mix_.prepare(sampleRate, 0.02);
    loopHighPass_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 20.0));
    reset();
}

void DigitalDelay::reset() noexcept {
    line_.reset();
    delay_ = effectiveTimeMs(settings_) * 0.001 * sampleRate_;
    delayTarget_ = delay_;
    const auto fc = static_cast<float>(hardnessCutoff(settings_.hardness));
    cutoff_.snap(fc);
    appliedCutoff_ = fc;
    loopLowPass_.setCoefficients(
        BiquadCoefficients::lowPass1(sampleRate_, std::min(static_cast<double>(fc), 0.45 * sampleRate_)));
    send_.snap(settings_.on ? 1.0f : 0.0f);
    feedback_.snap(settings_.feedbackPercent * 0.01f);
    mix_.snap(settings_.mixPercent * 0.01f);
    clock_.reset();
    loopLowPass_.reset();
    loopHighPass_.reset();
    quietSamples_ = 0;
    idle_ = !settings_.on;
}

void DigitalDelay::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    send_.setTarget(s.on ? 1.0f : 0.0f);
    if (s.on) {
        idle_ = false;
        quietSamples_ = 0;
    }
    if (idle_) {
        return;
    }
    delayTarget_ = effectiveTimeMs(s) * 0.001 * sampleRate_;
    cutoff_.setTarget(static_cast<float>(hardnessCutoff(s.hardness)));
    feedback_.setTarget(s.feedbackPercent * 0.01f);
    mix_.setTarget(s.mixPercent * 0.01f);
    const int quietLimit = line_.maxDelay() + 1;

    for (int i = 0; i < n; ++i) {
        if (idle_) {
            continue;
        }
        const float fc = cutoff_.next();
        if (clock_.tick() && fc != appliedCutoff_) {
            appliedCutoff_ = fc;
            loopLowPass_.setCoefficients(BiquadCoefficients::lowPass1(
                sampleRate_, std::min(static_cast<double>(fc), 0.45 * sampleRate_)));
        }
        const float x = io[i];
        // Cubic interpolation keeps the repeats clean while the read point glides.
        if (delay_ != delayTarget_) {
            const double step =
                std::clamp((delayTarget_ - delay_) * glideCoeff_, -kMaxGlideRate, kMaxGlideRate);
            delay_ = std::fabs(delayTarget_ - delay_) < 1.0e-6 ? delayTarget_ : delay_ + step;
        }
        const float wet = line_.readCubic(delay_);
        const float loop = loopLowPass_.process(loopHighPass_.process(wet));
        const float written = send_.next() * x + feedback_.next() * loop;
        line_.write(written);
        io[i] = x + mix_.next() * wet;

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
