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
    reset();
    prepared_ = true;
    return true;
}

void Engine::reset() noexcept {
    // Passthrough has no state yet.
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
    if (input != output) {
        std::memmove(output, input, bytes);  // memmove: buffers may overlap
    }
}

}  // namespace gigrig
