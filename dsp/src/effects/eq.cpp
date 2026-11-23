#include "eq.hpp"

namespace gigrig {

namespace {

// Target frequency and gain of a section for some settings.
void sectionTarget(const Eq::Settings& s, int section, float& freq, float& gainDb) noexcept {
    switch (section) {
        case 0:
            freq = s.highPassHz;
            gainDb = 0.0f;
            return;
        case 5:
            freq = s.lowPassHz;
            gainDb = 0.0f;
            return;
        default:
            freq = s.frequencyHz[static_cast<std::size_t>(section - 1)];
            gainDb = s.gainDb[static_cast<std::size_t>(section - 1)];
            return;
    }
}

}  // namespace

BiquadCoefficients Eq::sectionCoefficients(int section, double sampleRate, double freq,
                                           double gainDb) noexcept {
    // Keep every corner below Nyquist at 44.1 kHz too.
    const double f = std::min(freq, 0.45 * sampleRate);
    switch (section) {
        case HighPass:
            return freq <= static_cast<double>(kHighPassOffHz)
                       ? BiquadCoefficients{}
                       : BiquadCoefficients::highPass(sampleRate, f, kFilterQ);
        case LowPass:
            return freq >= static_cast<double>(kLowPassOffHz)
                       ? BiquadCoefficients{}
                       : BiquadCoefficients::lowPass(sampleRate, f, kFilterQ);
        case LowShelf:
            return BiquadCoefficients::lowShelf(sampleRate, f, kShelfQ, gainDb);
        case HighShelf:
            return BiquadCoefficients::highShelf(sampleRate, f, kShelfQ, gainDb);
        default:
            return BiquadCoefficients::peak(sampleRate, f, kPeakQ, gainDb);
    }
}

std::complex<double> Eq::designResponse(const Settings& s, double frequencyHz, double sampleRate) {
    std::complex<double> h = 1.0;
    for (int k = 0; k < kSections; ++k) {
        float f = 0.0f, g = 0.0f;
        sectionTarget(s, k, f, g);
        h *= sectionCoefficients(k, sampleRate, static_cast<double>(f), static_cast<double>(g))
                 .response(frequencyHz, sampleRate);
    }
    return h;
}

Eq::Settings Eq::fromParameters(const ParameterValues& p) noexcept {
    Settings s;
    s.on = p.get(GIGRIG_PARAM_EQ_ON) >= 0.5f;
    s.highPassHz = p.get(GIGRIG_PARAM_EQ_HIGHPASS);
    s.lowPassHz = p.get(GIGRIG_PARAM_EQ_LOWPASS);
    s.frequencyHz = {p.get(GIGRIG_PARAM_EQ_LOW_FREQ), p.get(GIGRIG_PARAM_EQ_MID1_FREQ),
                     p.get(GIGRIG_PARAM_EQ_MID2_FREQ), p.get(GIGRIG_PARAM_EQ_HIGH_FREQ)};
    s.gainDb = {p.get(GIGRIG_PARAM_EQ_LOW_GAIN), p.get(GIGRIG_PARAM_EQ_MID1_GAIN),
                p.get(GIGRIG_PARAM_EQ_MID2_GAIN), p.get(GIGRIG_PARAM_EQ_HIGH_GAIN)};
    return s;
}

void Eq::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    for (int k = 0; k < kSections; ++k) {
        freq_[static_cast<std::size_t>(k)].prepare(sampleRate, 0.03);
        gain_[static_cast<std::size_t>(k)].prepare(sampleRate, 0.03);
    }
    fade_.prepare(sampleRate);
    wet_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
    reset();
}

void Eq::reset() noexcept {
    fade_.snap(settings_.on);
    resetState();
}

void Eq::resetState() noexcept {
    clock_.reset();
    for (int k = 0; k < kSections; ++k) {
        const auto i = static_cast<std::size_t>(k);
        float f = 0.0f, g = 0.0f;
        sectionTarget(settings_, k, f, g);
        freq_[i].snap(f);
        gain_[i].snap(g);
        updateSection(k);
        filters_[i].reset();
    }
}

void Eq::updateSection(int section) noexcept {
    const auto i = static_cast<std::size_t>(section);
    appliedFreq_[i] = freq_[i].current();
    appliedGain_[i] = gain_[i].current();
    filters_[i].setCoefficients(sectionCoefficients(
        section, sampleRate_, static_cast<double>(appliedFreq_[i]), static_cast<double>(appliedGain_[i])));
}

void Eq::process(float* io, int n, const Settings& s) noexcept {
    settings_ = s;
    processWithBypass(
        fade_, s.on, io, wet_.data(), n, [this] { resetState(); },
        [this](const float* in, float* wet, int count) { computeWet(in, wet, count); });
}

void Eq::computeWet(const float* in, float* wet, int n) noexcept {
    for (int k = 0; k < kSections; ++k) {
        float f = 0.0f, g = 0.0f;
        sectionTarget(settings_, k, f, g);
        freq_[static_cast<std::size_t>(k)].setTarget(f);
        gain_[static_cast<std::size_t>(k)].setTarget(g);
    }
    for (int i = 0; i < n; ++i) {
        const bool due = clock_.tick();
        for (int k = 0; k < kSections; ++k) {
            const auto s = static_cast<std::size_t>(k);
            freq_[s].next();
            gain_[s].next();
            if (due && (freq_[s].current() != appliedFreq_[s] || gain_[s].current() != appliedGain_[s])) {
                updateSection(k);
            }
        }
        float x = in[i];
        for (auto& filter : filters_) {
            x = filter.process(x);
        }
        wet[i] = x;
    }
}

}  // namespace gigrig
