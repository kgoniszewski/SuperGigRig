#include "studio_reverb.hpp"

#include <cmath>

namespace gigrig {

namespace {

// In-place fast Walsh-Hadamard transform of 16 values, scaled to be orthogonal
// (energy preserving), so the network's decay is set by the line gains alone.
void hadamard16(std::array<float, 16>& v) noexcept {
    for (int len = 1; len < 16; len <<= 1) {
        for (int i = 0; i < 16; i += 2 * len) {
            for (int j = i; j < i + len; ++j) {
                const auto a = static_cast<std::size_t>(j);
                const auto b = static_cast<std::size_t>(j + len);
                const float x = v[a];
                const float y = v[b];
                v[a] = x + y;
                v[b] = x - y;
            }
        }
    }
    for (float& x : v) {
        x *= 0.25f;
    }
}

// Gain per pass for a loop of delaySeconds so that the level falls 60 dB in decaySeconds.
double loopGain(double delaySeconds, double decaySeconds) noexcept {
    return std::pow(10.0, -3.0 * delaySeconds / std::max(decaySeconds, 0.05));
}

// Line lengths of the hall, in ms: spread between about 30 and 100 ms and
// chosen so no two share a small common factor, which would stack their echoes.
constexpr std::array<double, HallTank::kLines> kHallMs = {31.7, 37.1, 41.3, 43.9, 47.3, 53.1, 57.7, 61.3,
                                                          67.9, 71.3, 73.7, 79.1, 83.9, 89.3, 97.1, 101.3};
constexpr std::array<double, 4> kHallDiffuserMs = {4.7, 3.6, 12.7, 9.3};
// Highs decay this much faster than lows.
constexpr double kHallHighDecayRatio = 0.35;
constexpr double kHallModDepthMs = 0.4;

}  // namespace

// ---------------------------------------------------------------- Hall

void HallTank::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (int k = 0; k < kLines; ++k) {
        const auto i = static_cast<std::size_t>(k);
        delays_[i] = std::round(kHallMs[i] * 0.001 * sampleRate);
        lines_[i].prepare(static_cast<int>(delays_[i] + kHallModDepthMs * 0.001 * sampleRate) + 4);
    }
    for (std::size_t i = 0; i < diffusers_.size(); ++i) {
        diffuserDelays_[i] = std::round(kHallDiffuserMs[i] * 0.001 * sampleRate);
        diffusers_[i].prepare(static_cast<int>(diffuserDelays_[i]) + 2);
    }
    lfo_.prepare(sampleRate);
    lfo_.setRate(0.31);
    setDecay(2.5);
    reset();
}

void HallTank::reset() noexcept {
    for (auto& l : lines_) l.reset();
    for (auto& d : diffusers_) d.reset();
    dampState_.fill(0.0f);
    lfo_.reset();
}

void HallTank::setDecay(double seconds) noexcept {
    for (int k = 0; k < kLines; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const double t = delays_[i] / sampleRate_;
        const double low = loopGain(t, seconds);
        const double high = loopGain(t, seconds * kHallHighDecayRatio);
        // One-pole low-pass whose gain at Nyquist is high / low (Jot's absorbent filter).
        const double r = high / low;
        gains_[i] = static_cast<float>(low);
        dampCoeff_[i] = static_cast<float>((1.0 - r) / (1.0 + r));
    }
}

float HallTank::process(float x) noexcept {
    // Input diffusion smears the attack before it enters the network.
    for (std::size_t i = 0; i < diffusers_.size(); ++i) {
        x = diffusers_[i].process(x, diffuserDelays_[i], 0.7f);
    }
    const double mod = static_cast<double>(lfo_.next()) * kHallModDepthMs * 0.001 * sampleRate_;
    std::array<float, kLines> v{};
    float out = 0.0f;
    for (int k = 0; k < kLines; ++k) {
        const auto i = static_cast<std::size_t>(k);
        float y = 0.0f;
        if (k == 0) {
            y = lines_[i].readLinear(delays_[i] + mod);
        } else if (k == 5) {
            y = lines_[i].readLinear(delays_[i] - mod);
        } else {
            y = lines_[i].read(static_cast<int>(delays_[i]));
        }
        out += (k & 1) ? -y : y;
        dampState_[i] = y + dampCoeff_[i] * (dampState_[i] - y);
        v[i] = gains_[i] * dampState_[i];
    }
    hadamard16(v);
    for (int k = 0; k < kLines; ++k) {
        const auto i = static_cast<std::size_t>(k);
        lines_[i].write(v[i] + ((k & 2) ? -x : x) * 0.25f);
    }
    return out * 0.25f;
}

// ---------------------------------------------------------------- Plate

namespace {
constexpr double kPlateRate = 29761.0;
constexpr int kPlateExcursion = 16;
// One trip around both branches of the tank, at the design rate.
constexpr double kPlateLoopSamples = 672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163;
}  // namespace

int PlateTank::scaled(int samplesAt29761) const noexcept {
    return static_cast<int>(std::lround(samplesAt29761 * sampleRate_ / kPlateRate));
}

void PlateTank::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    const int in[4] = {142, 107, 379, 277};
    for (std::size_t i = 0; i < 4; ++i) {
        inputDelays_[i] = scaled(in[i]);
        inputDiffusers_[i].prepare(inputDelays_[i] + 2);
    }
    const int mod[2] = {672, 908}, d1[2] = {4453, 4217}, ap[2] = {1800, 2656}, d2[2] = {3720, 3163};
    for (std::size_t b = 0; b < 2; ++b) {
        modDelay_[b] = scaled(mod[b]);
        delay1Len_[b] = scaled(d1[b]);
        decayApDelay_[b] = scaled(ap[b]);
        delay2Len_[b] = scaled(d2[b]);
        modAllpass_[b].prepare(modDelay_[b] + scaled(kPlateExcursion) + 2);
        delay1_[b].prepare(delay1Len_[b] + 2);
        decayAllpass_[b].prepare(decayApDelay_[b] + 2);
        delay2_[b].prepare(delay2Len_[b] + 2);
    }
    // Damping of the published design (0.0005 at 29.8 kHz), kept at the same corner frequency.
    dampCoeff_ = static_cast<float>(std::pow(0.0005, kPlateRate / sampleRate));
    lfo_.prepare(sampleRate);
    lfo_.setRate(1.0);
    setDecay(2.5);
    reset();
}

void PlateTank::reset() noexcept {
    for (auto& d : inputDiffusers_) d.reset();
    for (std::size_t b = 0; b < 2; ++b) {
        modAllpass_[b].reset();
        delay1_[b].reset();
        decayAllpass_[b].reset();
        delay2_[b].reset();
    }
    damp_.fill(0.0f);
    feedback_.fill(0.0f);
    lfo_.reset();
}

void PlateTank::setDecay(double seconds) noexcept {
    // The decay gain is applied four times per trip around the tank (twice in each branch).
    decay_ = static_cast<float>(std::min(loopGain(kPlateLoopSamples / kPlateRate * 0.25, seconds), 0.95));
}

float PlateTank::process(float x) noexcept {
    const float g[4] = {0.75f, 0.75f, 0.625f, 0.625f};
    for (std::size_t i = 0; i < 4; ++i) {
        x = inputDiffusers_[i].process(x, inputDelays_[i], g[i]);
    }
    const double excursion = static_cast<double>(lfo_.next()) * scaled(kPlateExcursion);
    const std::array<float, 2> fromOther = {feedback_[1], feedback_[0]};
    for (std::size_t b = 0; b < 2; ++b) {
        const double modDelay = static_cast<double>(modDelay_[b]) + (b == 0 ? excursion : -excursion);
        float v = modAllpass_[b].process(x + decay_ * fromOther[b], modDelay, -0.7f);
        const float d1 = delay1_[b].read(delay1Len_[b]);
        delay1_[b].write(v);
        damp_[b] = d1 + dampCoeff_ * (damp_[b] - d1);
        v = decayAllpass_[b].process(decay_ * damp_[b], decayApDelay_[b], 0.5f);
        feedback_[b] = delay2_[b].read(delay2Len_[b]);
        delay2_[b].write(v);
    }
    // Output taps of the published design's left channel (the output is mono).
    const float out = delay1_[1].read(scaled(266)) + delay1_[1].read(scaled(2974)) -
                      decayAllpass_[1].tap(scaled(1913)) + delay2_[1].read(scaled(1996)) -
                      delay1_[0].read(scaled(1990)) - decayAllpass_[0].tap(scaled(187)) -
                      delay2_[0].read(scaled(1066));
    return 0.6f * out;
}

// ---------------------------------------------------------------- Studio Reverb

void StudioReverb::prepare(double sampleRate, int maxBlockSize) {
    (void)maxBlockSize;
    sampleRate_ = sampleRate;
    preDelay_.prepare(static_cast<int>(std::ceil(kMaxPreDelayMs * 0.001 * sampleRate)) + 4);
    preDelaySamples_.prepare(sampleRate, 0.08);
    lowCut_.prepare(sampleRate, 0.03);
    highCut_.prepare(sampleRate, 0.03);
    decay_.prepare(sampleRate, 0.05);
    mix_.prepare(sampleRate, 0.02);
    for (auto& s : send_) s.prepare(sampleRate, 0.02);
    // A tail that has been below -120 dB for a second is over.
    quietLimit_ = static_cast<int>(sampleRate);
    hall_.prepare(sampleRate);
    plate_.prepare(sampleRate);
    reset();
}

void StudioReverb::updateFilters() noexcept {
    appliedLowCut_ = lowCut_.current();
    appliedHighCut_ = highCut_.current();
    const double nyquistGuard = 0.45 * sampleRate_;
    lowCutFilter_.setCoefficients(BiquadCoefficients::highPass(
        sampleRate_, std::min(static_cast<double>(appliedLowCut_), nyquistGuard), 0.7071));
    highCutFilter_.setCoefficients(BiquadCoefficients::lowPass(
        sampleRate_, std::min(static_cast<double>(appliedHighCut_), nyquistGuard), 0.7071));
}

void StudioReverb::reset() noexcept {
    const Settings& s = settings_;
    preDelay_.reset();
    preDelaySamples_.snap(static_cast<float>(static_cast<double>(s.preDelayMs) * 0.001 * sampleRate_));
    lowCut_.snap(s.lowCutHz);
    highCut_.snap(s.highCutHz);
    decay_.snap(s.decaySeconds);
    mix_.snap(s.mixPercent * 0.01f);
    updateFilters();
    lowCutFilter_.reset();
    highCutFilter_.reset();
    appliedDecay_ = s.decaySeconds;
    hall_.setDecay(static_cast<double>(s.decaySeconds));
    plate_.setDecay(static_cast<double>(s.decaySeconds));
    hall_.reset();
    plate_.reset();
    for (int t = 0; t < 2; ++t) {
        const bool selected = s.on && (s.mode == Plate) == (t == Plate);
        send_[static_cast<std::size_t>(t)].snap(selected ? 1.0f : 0.0f);
        running_[static_cast<std::size_t>(t)] = selected;
        quietSamples_[static_cast<std::size_t>(t)] = 0;
    }
    clock_.reset();
}

void StudioReverb::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    if (isIdle() && s.on) {
        // Starting from silence: forget input the pre-delay and filters saw
        // while the reverb was off, and start the controls at their settings.
        preDelay_.reset();
        lowCutFilter_.reset();
        highCutFilter_.reset();
        preDelaySamples_.snap(static_cast<float>(static_cast<double>(s.preDelayMs) * 0.001 * sampleRate_));
        mix_.snap(s.mixPercent * 0.01f);
    }
    for (int t = 0; t < 2; ++t) {
        const auto i = static_cast<std::size_t>(t);
        const bool selected = s.on && (s.mode == Plate) == (t == Plate);
        send_[i].setTarget(selected ? 1.0f : 0.0f);
        if (selected) {
            running_[i] = true;
            quietSamples_[i] = 0;
        }
    }
    if (isIdle()) {
        return;  // off and silent: the dry signal passes untouched
    }
    preDelaySamples_.setTarget(static_cast<float>(static_cast<double>(s.preDelayMs) * 0.001 * sampleRate_));
    lowCut_.setTarget(s.lowCutHz);
    highCut_.setTarget(s.highCutHz);
    decay_.setTarget(s.decaySeconds);
    mix_.setTarget(s.mixPercent * 0.01f);

    for (int i = 0; i < n; ++i) {
        if (isIdle()) {
            continue;  // both tails ended earlier in this block: io[i] is the dry signal
        }
        lowCut_.next();
        highCut_.next();
        const float decay = decay_.next();
        if (clock_.tick()) {
            if (lowCut_.current() != appliedLowCut_ || highCut_.current() != appliedHighCut_) {
                updateFilters();
            }
            if (decay != appliedDecay_) {
                appliedDecay_ = decay;
                hall_.setDecay(static_cast<double>(decay));
                plate_.setDecay(static_cast<double>(decay));
            }
        }
        const float x = io[i];
        const float delayed = preDelay_.readCubic(static_cast<double>(preDelaySamples_.next()));
        preDelay_.write(x);
        const float into = highCutFilter_.process(lowCutFilter_.process(delayed));
        float wet = 0.0f;
        for (int t = 0; t < 2; ++t) {
            const auto k = static_cast<std::size_t>(t);
            const float send = send_[k].next();
            if (!running_[k]) {
                continue;
            }
            const float y = t == Hall ? hall_.process(send * into) : plate_.process(send * into);
            wet += y;
            if (send == 0.0f && std::fabs(y) < kSilence) {
                if (++quietSamples_[k] > quietLimit_) {
                    running_[k] = false;
                }
            } else {
                quietSamples_[k] = 0;
            }
        }
        io[i] = x + mix_.next() * wet;
    }
}

}  // namespace gigrig
