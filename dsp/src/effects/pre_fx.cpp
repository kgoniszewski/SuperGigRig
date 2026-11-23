#include "pre_fx.hpp"

namespace gigrig {

namespace {

bool isOn(const ParameterValues& p, int id) noexcept {
    return p.get(id) >= 0.5f;
}

int stepped(const ParameterValues& p, int id) noexcept {
    return static_cast<int>(p.get(id) + 0.5f);
}

}  // namespace

void PreFx::readSettings(const ParameterValues& p) noexcept {
    boostSettings_.on = isOn(p, GIGRIG_PARAM_BOOST_ON);
    boostSettings_.mode = stepped(p, GIGRIG_PARAM_BOOST_MODE);
    boostSettings_.volumeDb = p.get(GIGRIG_PARAM_BOOST_VOLUME);

    envSettings_.on = isOn(p, GIGRIG_PARAM_ENVFILTER_ON);
    envSettings_.gain = p.get(GIGRIG_PARAM_ENVFILTER_GAIN);
    envSettings_.peak = p.get(GIGRIG_PARAM_ENVFILTER_PEAK);
    envSettings_.direction = stepped(p, GIGRIG_PARAM_ENVFILTER_DIRECTION);
    envSettings_.speed = stepped(p, GIGRIG_PARAM_ENVFILTER_SPEED);
    envSettings_.range = stepped(p, GIGRIG_PARAM_ENVFILTER_RANGE);

    goldSettings_.on = isOn(p, GIGRIG_PARAM_GOLDOD_ON);
    goldSettings_.gain = p.get(GIGRIG_PARAM_GOLDOD_GAIN);
    goldSettings_.levelDb = p.get(GIGRIG_PARAM_GOLDOD_LEVEL);
    goldSettings_.treble = p.get(GIGRIG_PARAM_GOLDOD_TREBLE);

    twinSettings_.on = isOn(p, GIGRIG_PARAM_TWINOD_ON);
    twinSettings_.gain = p.get(GIGRIG_PARAM_TWINOD_GAIN);
    twinSettings_.mode = stepped(p, GIGRIG_PARAM_TWINOD_MODE);
    twinSettings_.levelDb = p.get(GIGRIG_PARAM_TWINOD_LEVEL);
    twinSettings_.tone = p.get(GIGRIG_PARAM_TWINOD_TONE);

    delaySettings_.on = isOn(p, GIGRIG_PARAM_BUCKETDELAY_ON);
    delaySettings_.timeMs = p.get(GIGRIG_PARAM_BUCKETDELAY_TIME);
    delaySettings_.feedbackPercent = p.get(GIGRIG_PARAM_BUCKETDELAY_FEEDBACK);
    delaySettings_.blendPercent = p.get(GIGRIG_PARAM_BUCKETDELAY_BLEND);
}

void PreFx::applySettings() noexcept {
    boost_.setSettings(boostSettings_);
    envFilter_.setSettings(envSettings_);
    goldOd_.setSettings(goldSettings_);
    twinOd_.setSettings(twinSettings_);
    bucketDelay_.setSettings(delaySettings_);
}

void PreFx::prepare(double sampleRate, int maxBlockSize, const ParameterValues& params) {
    readSettings(params);
    applySettings();
    boost_.prepare(sampleRate, maxBlockSize);
    envFilter_.prepare(sampleRate, maxBlockSize);
    goldOd_.prepare(sampleRate, maxBlockSize);
    twinOd_.prepare(sampleRate, maxBlockSize);
    bucketDelay_.prepare(sampleRate, maxBlockSize);
}

void PreFx::reset(const ParameterValues& params) noexcept {
    readSettings(params);
    applySettings();
    boost_.reset();
    envFilter_.reset();
    goldOd_.reset();
    twinOd_.reset();
    bucketDelay_.reset();
}

void PreFx::process(float* io, int n, const ParameterValues& params) noexcept {
    readSettings(params);
    boost_.process(io, n, boostSettings_);
    envFilter_.process(io, n, envSettings_);
    goldOd_.process(io, n, goldSettings_);
    twinOd_.process(io, n, twinSettings_);
    bucketDelay_.process(io, n, delaySettings_);
}

}  // namespace gigrig
