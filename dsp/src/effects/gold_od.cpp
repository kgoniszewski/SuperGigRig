#include "gold_od.hpp"

#include <cmath>

namespace gigrig {

void GoldOd::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    const int factor = effectOversamplingFactor(sampleRate);
    os_.prepare(factor, maxBlockSize);
    const double hiRate = sampleRate * factor;
    inputHighPass_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 20.0));
    dirtyHighPass_.setCoefficients(BiquadCoefficients::highPass1(hiRate, 400.0));
    dirtyLowPass_.setCoefficients(BiquadCoefficients::lowPass1(hiRate, 6000.0));
    outputLowPass_.setCoefficients(BiquadCoefficients::lowPass(sampleRate, 9000.0, 0.7071));
    // Germanium diodes conduct early and round the corner more than silicon:
    // a knee between the rational soft clipper and a hard clip, x / (1 + |x|^p)^(1/p).
    diodeCurve_.build(
        [](double x) {
            constexpr double p = 2.5;
            return x / std::pow(1.0 + std::pow(std::fabs(x), p), 1.0 / p);
        },
        -12.0, 12.0, 4097);
    clipper_.setShape(shapes::Table{&diodeCurve_});
    gainBuffer_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    wet_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    fade_.prepare(sampleRate);
    gain_.prepare(sampleRate, 0.03);
    level_.prepare(sampleRate, 0.02);
    treble_.prepare(sampleRate, 0.03);
    reset();
}

void GoldOd::reset() noexcept {
    fade_.snap(settings_.on);
    resetState();
}

void GoldOd::resetState() noexcept {
    gain_.snap(settings_.gain);
    level_.snap(static_cast<float>(dbToGain(static_cast<double>(settings_.levelDb))));
    treble_.snap(settings_.treble);
    updateTreble(settings_.treble);
    clock_.reset();
    os_.reset();
    inputHighPass_.reset();
    dirtyHighPass_.reset();
    dirtyLowPass_.reset();
    outputLowPass_.reset();
    trebleShelf_.reset();
    clipper_.reset();
}

double GoldOd::dirtyDrive(float gainKnob) noexcept {
    return dbToGain(6.0 + 38.0 * knobFraction(gainKnob));
}

double GoldOd::cleanLevel(float gainKnob) noexcept {
    return 1.0 - 0.6 * knobFraction(gainKnob);
}

double GoldOd::dirtyLevel(float gainKnob) noexcept {
    return 0.25 + 0.75 * knobFraction(gainKnob);
}

void GoldOd::updateTreble(float knob) noexcept {
    appliedTreble_ = knob;
    const double db = (2.0 * knobFraction(knob) - 1.0) * kTrebleRangeDb;
    trebleShelf_.setCoefficients(BiquadCoefficients::highShelf1(sampleRate_, kTrebleHz, db));
}

void GoldOd::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    processWithBypass(
        fade_, s.on, io, wet_.data(), n, [this] { resetState(); },
        [this](const float* in, float* wet, int count) { computeWet(in, wet, count); });
}

void GoldOd::computeWet(const float* in, float* wet, int n) noexcept {
    gain_.setTarget(settings_.gain);
    level_.setTarget(static_cast<float>(dbToGain(static_cast<double>(settings_.levelDb))));
    treble_.setTarget(settings_.treble);
    for (int i = 0; i < n; ++i) {
        wet[i] = inputHighPass_.process(in[i]);
        gainBuffer_[static_cast<std::size_t>(i)] = gain_.next();
    }

    const int factor = os_.factor();
    float* hi = os_.upsample(wet, n);
    float knob = gainBuffer_[0];
    double drive = dirtyDrive(knob) / kClipLevel;
    for (int i = 0; i < n; ++i) {
        if (gainBuffer_[static_cast<std::size_t>(i)] != knob) {
            knob = gainBuffer_[static_cast<std::size_t>(i)];
            drive = dirtyDrive(knob) / kClipLevel;  // pow only while the knob moves
        }
        const double clean = cleanLevel(knob);
        const double dirtyMix = dirtyLevel(knob) * kClipLevel;
        for (int k = 0; k < factor; ++k) {
            float& v = hi[i * factor + k];
            const float pre = dirtyLowPass_.process(dirtyHighPass_.process(v));
            const double dirty =
                static_cast<double>(clipper_.process(static_cast<float>(drive * static_cast<double>(pre))));
            v = static_cast<float>(clean * static_cast<double>(v) + dirtyMix * dirty);
        }
    }
    os_.downsample(wet, n);

    for (int i = 0; i < n; ++i) {
        const float t = treble_.next();
        if (clock_.tick() && t != appliedTreble_) {
            updateTreble(t);
        }
        wet[i] = level_.next() * trebleShelf_.process(outputLowPass_.process(wet[i]));
    }
}

}  // namespace gigrig
