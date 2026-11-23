// engine.hpp - the C++ engine behind the C interface in gigrig.h.
//
// Work package 2: parameters, command queues, and Input and Output level.
// Work package 9: the Pre FX section after the input level. Later packages add
// the rest of the signal chain from docs/architecture.md, section 4, and meters. Keep this class free of virtual dispatch on
// the audio path.
#pragma once

#include <cstdint>

#include "blocks/smoother.hpp"
#include "commands.hpp"
#include "effects/pre_fx.hpp"
#include "parameters.hpp"

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

    // Real-time safe. Clears audio state; smoothers jump to the current parameter values.
    void reset() noexcept;

    // Real-time safe. 0 <= numSamples <= maxBlockSize(); in-place allowed.
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    ParameterValues& parameters() noexcept { return params_; }
    PreFx& preFx() noexcept { return preFx_; }
    const ParameterValues& parameters() const noexcept { return params_; }

    // One producer thread (the app's worker queue). False when full; the caller then
    // still owns the command's object and should dispose of it or retry later.
    bool pushCommand(const Command& c) noexcept { return commands_.push(c); }

    // One non-audio thread. Frees objects the audio thread has retired.
    int collectGarbage() noexcept { return commands_.collectGarbage(); }

    bool isPrepared() const noexcept { return prepared_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }
    int latencySamples() const noexcept { return 0; }

    // Gain ramps take this long whatever the block size, so output does not depend on it.
    static constexpr double kGainRampSeconds = 0.02;

private:
    Retired applyCommand(const Command& c) noexcept;
    void readParameters() noexcept;

    ParameterValues params_;
    CommandQueues commands_;

    float inputDb_ = 0.0f;
    float outputDb_ = 0.0f;
    LinearSmoother inputGain_;
    LinearSmoother outputGain_;
    PreFx preFx_;

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
};

}  // namespace gigrig
