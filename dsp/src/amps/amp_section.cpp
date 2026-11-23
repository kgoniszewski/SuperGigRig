#include "amps/amp_section.hpp"

#include <gigrig/gigrig.h>

#include <algorithm>
#include <cmath>

namespace gigrig::amp {

AmpSection::Model AmpSection::modelFrom(const ParameterValues& params) noexcept {
    const auto m = static_cast<int>(std::lround(params.get(GIGRIG_PARAM_AMP_MODEL)));
    return static_cast<Model>(std::clamp(m, 0, kModelCount - 1));
}

void AmpSection::prepare(double sampleRate, int maxBlockSize, const ParameterValues& params) {
    combo40_.prepare(sampleRate, maxBlockSize);
    steel100_.prepare(sampleRate, maxBlockSize);
    scratch_.assign(static_cast<std::size_t>(std::max(1, maxBlockSize)), 0.0f);
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kCrossfadeSeconds * sampleRate)));
    reset(params);
}

void AmpSection::reset(const ParameterValues& params) noexcept {
    readParameters(params);
    model_ = modelFrom(params);
    fadeRemaining_ = 0;
    combo40_.reset();
    steel100_.reset();
}

void AmpSection::resetModel(Model m) noexcept {
    switch (m) {
        case kCombo40:
            combo40_.reset();
            break;
        case kSteel100:
            steel100_.reset();
            break;
        case kModelCount:
            break;
    }
}

void AmpSection::readParameters(const ParameterValues& params) noexcept {
    const auto on = [&params](int id) { return params.get(id) >= 0.5f; };

    Combo40::Settings combo;
    combo.bright = on(GIGRIG_PARAM_COMBO40_BRIGHT);
    combo.volume = params.get(GIGRIG_PARAM_COMBO40_VOLUME);
    combo.treble = params.get(GIGRIG_PARAM_COMBO40_TREBLE);
    combo.bass = params.get(GIGRIG_PARAM_COMBO40_BASS);
    combo.outputDb = params.get(GIGRIG_PARAM_COMBO40_OUTPUT);
    combo40_.setSettings(combo);

    Steel100::Settings steel;
    steel.fetInput = on(GIGRIG_PARAM_STEEL100_INPUT);
    steel.bright = on(GIGRIG_PARAM_STEEL100_BRIGHT);
    steel.midBoost = on(GIGRIG_PARAM_STEEL100_MIDBOOST);
    steel.gain = params.get(GIGRIG_PARAM_STEEL100_GAIN);
    steel.bass = params.get(GIGRIG_PARAM_STEEL100_BASS);
    steel.middle = params.get(GIGRIG_PARAM_STEEL100_MIDDLE);
    steel.treble = params.get(GIGRIG_PARAM_STEEL100_TREBLE);
    steel.master = params.get(GIGRIG_PARAM_STEEL100_MASTER);
    steel.outputDb = params.get(GIGRIG_PARAM_STEEL100_OUTPUT);
    steel100_.setSettings(steel);

    const bool ampOn = on(GIGRIG_PARAM_AMP_ON);
    const Model wanted = modelFrom(params);
    if (ampOn && !on_) {
        // Back from bypass: start the selected amp from rest, without a crossfade.
        model_ = wanted;
        fadeRemaining_ = 0;
        resetModel(model_);
    } else if (wanted != model_) {
        // A new model while another crossfade runs simply starts a fresh one from the amp
        // now selected; the amp that was fading out is cut, which is inaudible in practice
        // because it is already mostly faded.
        fadingOut_ = model_;
        model_ = wanted;
        resetModel(model_);
        fadeRemaining_ = ampOn ? fadeLength_ : 0;
    }
    on_ = ampOn;
}

void AmpSection::processModel(Model m, float* data, int numSamples) noexcept {
    switch (m) {
        case kCombo40:
            combo40_.process(data, numSamples);
            break;
        case kSteel100:
            steel100_.process(data, numSamples);
            break;
        case kModelCount:
            break;
    }
}

void AmpSection::process(float* data, int numSamples) noexcept {
    if (!on_ || numSamples <= 0) {
        return;
    }
    if (fadeRemaining_ <= 0) {
        processModel(model_, data, numSamples);
        return;
    }
    // Crossfade: the outgoing amp runs on a copy of the input.
    const int n = std::min(numSamples, static_cast<int>(scratch_.size()));
    std::copy(data, data + n, scratch_.data());
    processModel(fadingOut_, scratch_.data(), n);
    processModel(model_, data, n);
    const float step = 1.0f / static_cast<float>(fadeLength_);
    for (int i = 0; i < n; ++i) {
        // Equal-gain linear fade: the two amps are unrelated signals, but a 20 ms fade
        // is short enough that the small dip in the middle is not heard.
        const float in = fadeRemaining_ > 0 ? 1.0f - static_cast<float>(fadeRemaining_) * step : 1.0f;
        data[i] = in * data[i] + (1.0f - in) * scratch_[static_cast<std::size_t>(i)];
        if (fadeRemaining_ > 0) {
            --fadeRemaining_;
        }
    }
}

}  // namespace gigrig::amp
