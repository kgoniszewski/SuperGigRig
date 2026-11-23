#include "amps/power_amp.hpp"

#include <algorithm>
#include <cmath>

#include "blocks/dsp_math.hpp"
#include "blocks/smoother.hpp"

namespace gigrig::amp {
namespace {

double softplus(double x) noexcept {
    return x > 30.0 ? x : std::log1p(std::exp(x));
}

}  // namespace

void PushPullPowerAmp::prepare(const PowerAmpDesign& design, double sampleRate) {
    design_ = design;
    const double limit = design.inverterLimit;
    inverterTable_.build([limit](double x) { return limit * std::tanh(x / limit); }, -16.0, 16.0, 4097);

    // One output tube's plate current against its grid drive: a 3/2-power law from a
    // rounded cutoff, idling at the class AB bias. The push-pull difference cancels even
    // harmonics; near zero both tubes conduct and the crossover is smooth unless sag
    // pushes the drive around it. Plate saturation (the tube running out of plate
    // voltage) then limits the swing with a fairly firm knee.
    const double bias = design.classAbBias;
    auto tube = [bias](double v) {
        constexpr double knee = 0.04;
        const double s = knee * softplus((v + bias) / knee);
        return s * std::sqrt(s);
    };
    const double norm = tube(1.0) - tube(-1.0);
    outputPairTable_.build(
        [&](double d) {
            const double p = (tube(d) - tube(-d)) / norm;
            return p / std::cbrt(1.0 + std::fabs(p * p * p));
        },
        -6.0, 6.0, 8193);

    inverter_.setShape(shapes::Table{&inverterTable_});
    outputPair_.setShape(shapes::Table{&outputPairTable_});
    transformerLowPass_.setCoefficients(BiquadCoefficients::lowPass(
        sampleRate, std::min(design.transformerTopHz, 0.45 * sampleRate), 0.7071));
    sagAttack_ = 1.0 - onePoleCoefficient(design.sagAttackSeconds, sampleRate);
    sagRelease_ = 1.0 - onePoleCoefficient(design.sagReleaseSeconds, sampleRate);
    coreBase_ = kTwoPi * design.coreCornerHz / sampleRate;
    // Flux is the integral of the winding voltage, scaled so a full-power 40 Hz tone reaches 1.
    fluxScale_ = kTwoPi * 40.0 / sampleRate;
    fluxLeak_ = onePoleCoefficient(1.0 / (kTwoPi * 3.0), sampleRate);
    presenceCoefficient_ = 1.0 - std::exp(-kTwoPi * design.presenceHz / sampleRate);
    presence_ = std::min(presence_, design.presenceMaxCut);
    reset();
}

void PushPullPowerAmp::reset() noexcept {
    inverter_.reset();
    outputPair_.reset();
    transformerLowPass_.reset();
    supplyEnvelope_ = 0.0;
    supply_ = 1.0;
    coreLow_ = 0.0;
    flux_ = 0.0;
    feedback_ = 0.0;
    feedbackLow_ = 0.0;
}

float PushPullPowerAmp::process(float input) noexcept {
    // Feedback from the speaker winding enters at the inverter's tail; one sample of
    // loop delay at 176 kHz or more is far outside the audio band, and the loop
    // gain stays below 1 at every frequency, so the loop is unconditionally stable.
    const double error = static_cast<double>(input) - design_.feedback * feedback_;
    const auto drive = static_cast<double>(inverter_.process(static_cast<float>(error)));
    const double plate =
        supply_ * static_cast<double>(outputPair_.process(static_cast<float>(drive / supply_)));

    // Rectifier sag: the supply follows the current drawn, with the filter caps'
    // slower recovery. A lower supply both lowers the clipping point and compresses.
    const double current = std::fabs(plate);
    supplyEnvelope_ += (current > supplyEnvelope_ ? sagAttack_ : sagRelease_) * (current - supplyEnvelope_);
    supply_ = 1.0 - design_.sagDepth * supplyEnvelope_;

    // Output transformer: the primary inductance is a high-pass whose corner rises
    // as low notes push the core toward saturation, so big bass notes lose weight
    // and gain a little grit instead of growing without limit.
    flux_ = fluxLeak_ * flux_ + fluxScale_ * plate;
    const double coreCoefficient = std::min(0.05, coreBase_ * (1.0 + design_.coreSaturation * flux_ * flux_));
    coreLow_ += coreCoefficient * (plate - coreLow_);
    const auto speaker =
        static_cast<double>(transformerLowPass_.process(static_cast<float>(plate - coreLow_)));

    // Presence: the feedback loses part of its top end, so the loop corrects the treble
    // less and the amp gets brighter and more open, as with the real presence control
    // (a cap and pot shunting the top of the feedback divider to ground).
    feedbackLow_ += presenceCoefficient_ * (speaker - feedbackLow_);
    feedback_ = speaker - presence_ * (speaker - feedbackLow_);
    return static_cast<float>(speaker);
}

}  // namespace gigrig::amp
