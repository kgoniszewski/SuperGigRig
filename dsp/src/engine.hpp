// engine.hpp - the C++ engine behind the C interface in gigrig.h.
//
// Work package 0: passthrough. Later packages add the parameter table,
// command queues, meters and the signal chain from docs/architecture.md,
// section 4. Keep this class free of virtual dispatch on the audio path.
#pragma once

#include <cstdint>

namespace gigrig {

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Not real-time safe. Allocates everything the audio path needs.
    // Returns false and leaves the engine unprepared on invalid arguments.
    bool prepare(double sampleRate, int maxBlockSize);

    // Real-time safe.
    void reset() noexcept;

    // Real-time safe. 0 <= numSamples <= maxBlockSize(); in-place allowed.
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    bool isPrepared() const noexcept { return prepared_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }
    int latencySamples() const noexcept { return 0; }

private:
    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
};

}  // namespace gigrig
