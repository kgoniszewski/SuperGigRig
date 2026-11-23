#include "post_fx.hpp"

namespace gigrig {

void PostFx::readSettings(const ParameterValues& p) noexcept {
    delaySettings_.on = p.get(GIGRIG_PARAM_DELAY_ON) >= 0.5f;
    delaySettings_.timeMs = p.get(GIGRIG_PARAM_DELAY_TIME);
    delaySettings_.sync = static_cast<int>(p.get(GIGRIG_PARAM_DELAY_SYNC) + 0.5f);
    delaySettings_.division = static_cast<int>(p.get(GIGRIG_PARAM_DELAY_DIVISION) + 0.5f);
    delaySettings_.feedbackPercent = p.get(GIGRIG_PARAM_DELAY_FEEDBACK);
    delaySettings_.mixPercent = p.get(GIGRIG_PARAM_DELAY_MIX);
    delaySettings_.hardness = p.get(GIGRIG_PARAM_DELAY_HARDNESS);
    delaySettings_.tempoBpm = p.get(GIGRIG_PARAM_TEMPO_BPM);

    reverbSettings_.on = p.get(GIGRIG_PARAM_REVERB_ON) >= 0.5f;
    reverbSettings_.mode = static_cast<int>(p.get(GIGRIG_PARAM_REVERB_MODE) + 0.5f);
    reverbSettings_.mixPercent = p.get(GIGRIG_PARAM_REVERB_MIX);
    reverbSettings_.preDelayMs = p.get(GIGRIG_PARAM_REVERB_PREDELAY);
    reverbSettings_.decaySeconds = p.get(GIGRIG_PARAM_REVERB_DECAY);
    reverbSettings_.lowCutHz = p.get(GIGRIG_PARAM_REVERB_LOWCUT);
    reverbSettings_.highCutHz = p.get(GIGRIG_PARAM_REVERB_HIGHCUT);
}

void PostFx::prepare(double sampleRate, int maxBlockSize, const ParameterValues& params) {
    readSettings(params);
    delay_.setSettings(delaySettings_);
    reverb_.setSettings(reverbSettings_);
    delay_.prepare(sampleRate, maxBlockSize);
    reverb_.prepare(sampleRate, maxBlockSize);
}

void PostFx::reset(const ParameterValues& params) noexcept {
    readSettings(params);
    delay_.setSettings(delaySettings_);
    reverb_.setSettings(reverbSettings_);
    delay_.reset();
    reverb_.reset();
}

void PostFx::process(float* io, int n, const ParameterValues& params) noexcept {
    readSettings(params);
    delay_.process(io, n, delaySettings_);
    reverb_.process(io, n, reverbSettings_);
}

}  // namespace gigrig
