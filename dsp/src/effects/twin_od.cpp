#include "twin_od.hpp"

#include <cmath>

namespace gigrig {

void TwinOd::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    const int factor = effectOversamplingFactor(sampleRate);
    os_.prepare(factor, maxBlockSize);
    const double hiRate = sampleRate * factor;
    inputHighPass_.setCoefficients(BiquadCoefficients::highPass1(sampleRate, 20.0));
    midHighPass_.setCoefficients(BiquadCoefficients::highPass1(hiRate, kMidCornerHz));
    midLowPass_.setCoefficients(BiquadCoefficients::lowPass1(hiRate, 6000.0));
    ampHighPass_.setCoefficients(BiquadCoefficients::highPass1(hiRate, kAmpCornerHz));
    ampLowPass_.setCoefficients(BiquadCoefficients::lowPass1(hiRate, 8000.0));
    gainBuffer_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    modeBuffer_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    wet_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    fade_.prepare(sampleRate);
    gain_.prepare(sampleRate, 0.03);
    level_.prepare(sampleRate, 0.02);
    tone_.prepare(sampleRate, 0.02);
    modeMix_.prepare(sampleRate, 0.01);
    reset();
}

void TwinOd::reset() noexcept {
    fade_.snap(settings_.on);
    resetState();
}

void TwinOd::resetMid() noexcept {
    midHighPass_.reset();
    midLowPass_.reset();
    midClipper_.reset();
}

void TwinOd::resetAmp() noexcept {
    ampHighPass_.reset();
    ampLowPass_.reset();
    ampClipper_.reset();
}

void TwinOd::resetState() noexcept {
    gain_.snap(settings_.gain);
    level_.snap(static_cast<float>(dbToGain(static_cast<double>(settings_.levelDb))));
    const auto cutoff = static_cast<float>(toneCutoff(settings_.tone));
    tone_.snap(cutoff);
    appliedTone_ = cutoff;
    toneFilter_.setCoefficients(BiquadCoefficients::lowPass1(sampleRate_, static_cast<double>(cutoff)));
    modeMix_.snap(settings_.mode == Amp ? 1.0f : 0.0f);
    clock_.reset();
    os_.reset();
    inputHighPass_.reset();
    toneFilter_.reset();
    resetMid();
    resetAmp();
    midRunning_ = false;
    ampRunning_ = false;
}

double TwinOd::midDrive(float gainKnob) noexcept {
    // 1 + R(drive) / R(feedback): from about 12 dB to about 41 dB of gain above the corner.
    return 4.0 + 110.0 * std::pow(knobFraction(gainKnob), 2.0);
}

double TwinOd::ampDrive(float gainKnob) noexcept {
    return dbToGain(30.0 * knobFraction(gainKnob));
}

double TwinOd::toneCutoff(float toneKnob) noexcept {
    return expSweep(600.0, 8000.0, knobFraction(toneKnob));
}

void TwinOd::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    processWithBypass(
        fade_, s.on, io, wet_.data(), n, [this] { resetState(); },
        [this](const float* in, float* wet, int count) { computeWet(in, wet, count); });
}

void TwinOd::computeWet(const float* in, float* wet, int n) noexcept {
    gain_.setTarget(settings_.gain);
    level_.setTarget(static_cast<float>(dbToGain(static_cast<double>(settings_.levelDb))));
    tone_.setTarget(static_cast<float>(toneCutoff(settings_.tone)));
    modeMix_.setTarget(settings_.mode == Amp ? 1.0f : 0.0f);
    // As in Boost: only the selected mode runs, except during the mode crossfade.
    const bool needMid = modeMix_.current() < 1.0f || modeMix_.target() < 1.0f;
    const bool needAmp = modeMix_.current() > 0.0f || modeMix_.target() > 0.0f;
    if (needMid && !midRunning_) {
        resetMid();
    }
    if (needAmp && !ampRunning_) {
        resetAmp();
    }
    midRunning_ = needMid;
    ampRunning_ = needAmp;

    for (int i = 0; i < n; ++i) {
        wet[i] = inputHighPass_.process(in[i]);
        gainBuffer_[static_cast<std::size_t>(i)] = gain_.next();
        modeBuffer_[static_cast<std::size_t>(i)] = modeMix_.next();
    }

    const int factor = os_.factor();
    float* hi = os_.upsample(wet, n);
    float knob = gainBuffer_[0];
    double mDrive = midDrive(knob) / kMidClipLevel;
    double aDrive = ampDrive(knob) / kAmpClipLevel;
    for (int i = 0; i < n; ++i) {
        if (gainBuffer_[static_cast<std::size_t>(i)] != knob) {
            knob = gainBuffer_[static_cast<std::size_t>(i)];
            mDrive = midDrive(knob) / kMidClipLevel;
            aDrive = ampDrive(knob) / kAmpClipLevel;
        }
        const float m = modeBuffer_[static_cast<std::size_t>(i)];
        for (int k = 0; k < factor; ++k) {
            float& v = hi[i * factor + k];
            float mid = 0.0f;
            float amp = 0.0f;
            if (needMid) {
                // Clean input plus the diode-limited gain of the part above the corner.
                const float hp = midHighPass_.process(v);
                const auto clipped =
                    kMidClipLevel * static_cast<double>(midClipper_.process(
                                        static_cast<float>(mDrive * static_cast<double>(hp))));
                mid = midLowPass_.process(static_cast<float>(static_cast<double>(v) + clipped));
            }
            if (needAmp) {
                const float hp = ampHighPass_.process(v);
                amp = ampLowPass_.process(static_cast<float>(
                    kAmpClipLevel * static_cast<double>(ampClipper_.process(
                                        static_cast<float>(aDrive * static_cast<double>(hp))))));
            }
            v = m == 0.0f ? mid : (m == 1.0f ? amp : mid + m * (amp - mid));
        }
    }
    os_.downsample(wet, n);

    for (int i = 0; i < n; ++i) {
        const float fc = tone_.next();
        if (clock_.tick() && fc != appliedTone_) {
            appliedTone_ = fc;
            toneFilter_.setCoefficients(BiquadCoefficients::lowPass1(sampleRate_, static_cast<double>(fc)));
        }
        wet[i] = level_.next() * toneFilter_.process(wet[i]);
    }
}

}  // namespace gigrig
