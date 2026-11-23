#include "env_filter.hpp"

#include <cmath>

namespace gigrig {

void EnvFilter::prepare(double sampleRate, int maxBlockSize) {
    env_.setMode(EnvelopeFollower::Mode::Peak);
    env_.prepare(sampleRate);
    svf_.prepare(sampleRate);
    sensitivity_.prepare(sampleRate, 0.02);
    q_.prepare(sampleRate, 0.02);
    fade_.prepare(sampleRate);
    wet_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    reset();
}

void EnvFilter::reset() noexcept {
    fade_.snap(settings_.on);
    resetState();
}

void EnvFilter::resetState() noexcept {
    env_.reset();
    svf_.reset();
    sensitivity_.snap(settings_.gain);
    q_.snap(static_cast<float>(resonance(settings_.peak)));
}

double EnvFilter::sweepPosition(double envelope, float gainKnob) noexcept {
    // Gain spans -18 to +18 dB of detector gain around a level where a firm
    // pick (about 0.3 of full scale) moves the filter most of the way up.
    const double sensitivity = 6.0 * dbToGain((static_cast<double>(gainKnob) - 5.0) * 3.6);
    // A soft-saturating map, as the optical cell of the originals: the filter
    // opens fast for the attack and then glides with the decay.
    return 1.0 - std::exp(-envelope * sensitivity);
}

double EnvFilter::resonance(float peakKnob) noexcept {
    return expSweep(0.7, 6.0, knobFraction(peakKnob));
}

void EnvFilter::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    processWithBypass(
        fade_, s.on, io, wet_.data(), n, [this] { resetState(); },
        [this](const float* in, float* wet, int count) { computeWet(in, wet, count); });
}

void EnvFilter::computeWet(const float* in, float* wet, int n) noexcept {
    env_.setTimes(kAttackSeconds, settings_.speed == Slow ? kSlowReleaseSeconds : kFastReleaseSeconds);
    sensitivity_.setTarget(settings_.gain);
    q_.setTarget(static_cast<float>(resonance(settings_.peak)));
    const double* range = settings_.range == Hi ? kHiRange : kLoRange;
    const double ratio = range[1] / range[0];
    const bool down = settings_.direction == Down;
    for (int i = 0; i < n; ++i) {
        const double e = static_cast<double>(env_.process(in[i]));
        double pos = sweepPosition(e, sensitivity_.next());
        if (down) {
            pos = 1.0 - pos;
        }
        // The SVF is designed to be retuned every sample (one tan per sample).
        svf_.setParameters(range[0] * std::pow(ratio, pos), static_cast<double>(q_.next()));
        // Low-pass with resonance gives the vocal sweep; the peak gain is left
        // in, as on the originals, so higher Peak settings are louder.
        wet[i] = svf_.process(in[i]).lowPass;
    }
}

}  // namespace gigrig
