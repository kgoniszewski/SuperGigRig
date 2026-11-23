#include "engine.hpp"

#include <gigrig/gigrig.h>

#include <cstring>

namespace gigrig {

bool Engine::prepare(double sampleRate, int maxBlockSize) {
    prepared_ = false;
    loaderRate_.store(0.0, std::memory_order_release);
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
    cab_.prepare(sampleRate);
    reset();
    prepared_ = true;
    loaderRate_.store(sampleRate, std::memory_order_release);
    return true;
}

void Engine::reset() noexcept {
    inputDb_ = params_.get(GIGRIG_PARAM_INPUT_LEVEL);
    outputDb_ = params_.get(GIGRIG_PARAM_OUTPUT_LEVEL);
    inputGain_.snap(static_cast<float>(dbToGain(static_cast<double>(inputDb_))));
    outputGain_.snap(static_cast<float>(dbToGain(static_cast<double>(outputDb_))));
    readCabParameters();
    cab_.reset();
}

std::optional<Retired> Engine::applyCommand(const Command& c) noexcept {
    switch (c.type) {
        case CommandType::Noop:
            return Retired{c.object, c.destroy};
        case CommandType::CabLoad:
            return cab_.applyLoad(c.slot, static_cast<CabIr*>(c.object));
    }
    // Unknown type (cannot happen with a valid enum): hand the object back unused.
    return Retired{c.object, c.destroy};
}

void Engine::readCabParameters() noexcept {
    const auto on = [this](int id) { return params_.get(id) >= 0.5f; };
    std::array<CabSlotParams, Cabinet::kSlots> slots{};
    slots[0] = {on(GIGRIG_PARAM_CAB_MIC1_ON), params_.get(GIGRIG_PARAM_CAB_MIC1_LEVEL),
                on(GIGRIG_PARAM_CAB_MIC1_PHASE), params_.get(GIGRIG_PARAM_CAB_MIC1_ROOM) * 0.01f};
    slots[1] = {on(GIGRIG_PARAM_CAB_MIC2_ON), params_.get(GIGRIG_PARAM_CAB_MIC2_LEVEL),
                on(GIGRIG_PARAM_CAB_MIC2_PHASE), params_.get(GIGRIG_PARAM_CAB_MIC2_ROOM) * 0.01f};
    cab_.setParameters(on(GIGRIG_PARAM_CAB_ON), slots, static_cast<int>(params_.get(GIGRIG_PARAM_CAB_ROOM_SIZE)));
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
    readCabParameters();
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

    cab_.retireFinished([this](const Retired& r) noexcept { return commands_.retire(r); });
    commands_.drain([this](const Command& c) noexcept { return applyCommand(c); });
    readParameters();

    // At 0 dB both gains are exactly 1 and an empty cabinet passes through,
    // so the chain stays bit exact at its defaults.
    for (int i = 0; i < numSamples; ++i) {
        output[i] = input[i] * inputGain_.next();
    }
    cab_.process(output, output, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        output[i] *= outputGain_.next();
    }
}

}  // namespace gigrig
