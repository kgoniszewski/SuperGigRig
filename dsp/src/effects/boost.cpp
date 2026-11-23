#include "boost.hpp"

#include <cmath>

namespace gigrig {

void Boost::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    const int factor = effectOversamplingFactor(sampleRate);
    os_.prepare(factor, maxBlockSize);
    const double hiRate = sampleRate * factor;
    inputHighPass_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 20.0));
    odHighCut_.setCoefficients(BiquadCoefficients::lowPass1(hiRate, 7000.0));
    dcBlock_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 10.0));
    // MOSFET-style pair: the negative half clips earlier and softer than the
    // positive half, so the curve is asymmetric and makes even harmonics.
    odCurve_.build([](double x) { return x >= 0.0 ? std::tanh(x) : std::tanh(1.5 * x) / 1.5; }, -8.0, 8.0,
                   4097);
    odShaper_.setShape(shapes::Table{&odCurve_});
    gainBuffer_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    modeBuffer_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    wet_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    fade_.prepare(sampleRate);
    gain_.prepare(sampleRate, 0.02);
    modeMix_.prepare(sampleRate, 0.01);
    reset();
}

void Boost::reset() noexcept {
    fade_.snap(settings_.on);
    resetState();
}

void Boost::resetState() noexcept {
    gain_.snap(static_cast<float>(dbToGain(static_cast<double>(settings_.volumeDb))));
    modeMix_.snap(settings_.mode == Overdrive ? 1.0f : 0.0f);
    os_.reset();
    inputHighPass_.reset();
    odHighCut_.reset();
    dcBlock_.reset();
    cleanShaper_.reset();
    odShaper_.reset();
    cleanRunning_ = false;
    odRunning_ = false;
}

void Boost::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    processWithBypass(
        fade_, s.on, io, wet_.data(), n, [this] { resetState(); },
        [this](const float* in, float* wet, int count) { computeWet(in, wet, count); });
}

void Boost::computeWet(const float* in, float* wet, int n) noexcept {
    gain_.setTarget(static_cast<float>(dbToGain(static_cast<double>(settings_.volumeDb))));
    modeMix_.setTarget(settings_.mode == Overdrive ? 1.0f : 0.0f);
    // Only the selected mode runs, except while the mode switch crossfades. A
    // path that starts running again starts from cleared state, so the result
    // does not depend on where earlier blocks ended.
    const bool needClean = modeMix_.current() < 1.0f || modeMix_.target() < 1.0f;
    const bool needOd = modeMix_.current() > 0.0f || modeMix_.target() > 0.0f;
    if (needClean && !cleanRunning_) {
        cleanShaper_.reset();
    }
    if (needOd && !odRunning_) {
        odShaper_.reset();
        odHighCut_.reset();
    }
    cleanRunning_ = needClean;
    odRunning_ = needOd;
    for (int i = 0; i < n; ++i) {
        wet[i] = inputHighPass_.process(in[i]);
        gainBuffer_[static_cast<std::size_t>(i)] = gain_.next();
        modeBuffer_[static_cast<std::size_t>(i)] = modeMix_.next();
    }

    const int factor = os_.factor();
    float* hi = os_.upsample(wet, n);
    constexpr double kCleanH = kCleanHeadroom;
    constexpr double kOdT = kOverdriveLevel;
    for (int i = 0; i < n; ++i) {
        const auto g = static_cast<double>(gainBuffer_[static_cast<std::size_t>(i)]);
        const float m = modeBuffer_[static_cast<std::size_t>(i)];
        for (int k = 0; k < factor; ++k) {
            float& v = hi[i * factor + k];
            const double x = static_cast<double>(v) * g;
            float clean = 0.0f;
            float od = 0.0f;
            if (needClean) {
                clean = static_cast<float>(kCleanH) * cleanShaper_.process(static_cast<float>(x / kCleanH));
            }
            if (needOd) {
                od = odHighCut_.process(static_cast<float>(
                    kOdT *
                    static_cast<double>(odShaper_.process(static_cast<float>(x * kOverdriveDrive / kOdT)))));
            }
            v = m == 0.0f ? clean : (m == 1.0f ? od : clean + m * (od - clean));
        }
    }
    os_.downsample(wet, n);
    for (int i = 0; i < n; ++i) {
        wet[i] = dcBlock_.process(wet[i]);
    }
}

}  // namespace gigrig
