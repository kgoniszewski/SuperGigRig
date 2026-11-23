#include "engine.hpp"

#include <gigrig/gigrig.h>

#include <cstring>

namespace gigrig {

bool Engine::prepare(double sampleRate, int maxBlockSize) {
    prepared_ = false;
    // Negated comparisons so that NaN is rejected too.
    if (!(sampleRate >= GIGRIG_MIN_SAMPLE_RATE && sampleRate <= GIGRIG_MAX_SAMPLE_RATE)) {
        return false;
    }
    if (maxBlockSize < 1 || maxBlockSize > GIGRIG_MAX_BLOCK_SIZE) {
        return false;
    }
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    inputGain_.prepare(sampleRate, kGainRampSeconds);
    outputGain_.prepare(sampleRate, kGainRampSeconds);
    preFx_.prepare(sampleRate, maxBlockSize, params_);
    reset();
    prepared_ = true;
    return true;
}

void Engine::reset() noexcept {
    inputDb_ = params_.get(GIGRIG_PARAM_INPUT_LEVEL);
    outputDb_ = params_.get(GIGRIG_PARAM_OUTPUT_LEVEL);
    inputGain_.snap(static_cast<float>(dbToGain(static_cast<double>(inputDb_))));
    outputGain_.snap(static_cast<float>(dbToGain(static_cast<double>(outputDb_))));
    preFx_.reset(params_);
}

Retired Engine::applyCommand(const Command& c) noexcept {
    switch (c.type) {
        case CommandType::Noop:
            return Retired{c.object, c.destroy};
    }
    // Unknown type (cannot happen with a valid enum): hand the object back unused.
    return Retired{c.object, c.destroy};
}

void Engine::readParameters() noexcept {
    // pow only runs when a value actually changed, not every block.
    const float in = params_.get(GIGRIG_PARAM_INPUT_LEVEL);
    if (in != inputDb_) {
        inputDb_ = in;
        inputGain_.setTarget(static_cast<float>(dbToGain(static_cast<double>(in))));
    }
    const float out = params_.get(GIGRIG_PARAM_OUTPUT_LEVEL);
    if (out != outputDb_) {
        outputDb_ = out;
        outputGain_.setTarget(static_cast<float>(dbToGain(static_cast<double>(out))));
    }
}

void Engine::processBlock(const float* input, float* output, int numSamples) noexcept {
    if (numSamples <= 0) {
        return;
    }
    const auto bytes = static_cast<std::size_t>(numSamples) * sizeof(float);
    if (!prepared_ || input == nullptr) {
        std::memset(output, 0, bytes);
        return;
    }

    commands_.drain([this](const Command& c) noexcept { return applyCommand(c); });
    readParameters();

    // Input level, the chain in place, then output level. At 0 dB both gains
    // are exactly 1 and bypassed blocks pass their input untouched, so a chain
    // with everything off stays bit exact.
    for (int i = 0; i < numSamples; ++i) {
        output[i] = input[i] * inputGain_.next();
    }
    preFx_.process(output, numSamples, params_);
    // Amp, cab, EQ and Post FX follow here (work packages 3, 4, 11 and 12).
    for (int i = 0; i < numSamples; ++i) {
        output[i] *= outputGain_.next();
    }
}

}  // namespace gigrig
