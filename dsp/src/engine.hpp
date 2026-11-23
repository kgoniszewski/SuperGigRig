// engine.hpp - the C++ engine behind the C interface in gigrig.h.
//
// Chain so far: Input level, Pre FX (work package 9), Combo 40 amp (work package 4),
// Cab (work package 3), EQ (work package 11), Output level. Later packages add the other blocks of docs/architecture.md,
// section 4, and meters. Keep this class free of virtual dispatch on
// the audio path.
#pragma once

#include <atomic>
#include <cstdint>

#include "amps/combo40.hpp"
#include "blocks/smoother.hpp"
#include "cab/cabinet.hpp"
#include "commands.hpp"
#include "effects/eq.hpp"
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
    const ParameterValues& parameters() const noexcept { return params_; }

    // One producer thread (the app's worker queue). False when full; the caller then
    // still owns the command's object and should dispose of it or retry later.
    bool pushCommand(const Command& c) noexcept { return commands_.push(c); }

    // One non-audio thread. Frees objects the audio thread has retired.
    int collectGarbage() noexcept { return commands_.collectGarbage(); }

    // Any thread: the rate the engine was last prepared at, or 0. The IR loader builds for this rate.
    double loaderSampleRate() const noexcept { return loaderRate_.load(std::memory_order_acquire); }

    const Cabinet& cabinet() const noexcept { return cab_; }

    bool isPrepared() const noexcept { return prepared_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }
    int latencySamples() const noexcept { return 0; }

    // Gain ramps take this long whatever the block size, so output does not depend on it.
    static constexpr double kGainRampSeconds = 0.02;

private:
    std::optional<Retired> applyCommand(const Command& c) noexcept;
    void readParameters() noexcept;
    void readCabParameters() noexcept;

    ParameterValues params_;
    CommandQueues commands_;

    float inputDb_ = 0.0f;
    float outputDb_ = 0.0f;
    LinearSmoother inputGain_;
    LinearSmoother outputGain_;
    PreFx preFx_;
    Eq eq_;

    amp::Combo40 combo40_;
    bool ampOn_ = true;

    Cabinet cab_;
    std::atomic<double> loaderRate_{0.0};

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
};

}  // namespace gigrig
