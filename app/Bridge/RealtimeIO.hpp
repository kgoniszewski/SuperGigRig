// RealtimeIO.hpp - the part of the render callback that does not depend on Core Audio.
//
// The Objective-C++ engine (GRAudioEngine.mm) pulls the device input, then hands the
// channel pointers to renderMono(). Keeping this in plain C++ lets it be unit tested on
// Linux (app/Bridge/tests), since the app itself can only be built on a Mac.
//
// Real-time rules (CLAUDE.md): no allocation, locks, logging or system calls. All
// buffers are allocated by prepare() on the main thread while the I/O unit is stopped.
#pragma once

#include <gigrig/gigrig.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

#include "LoopbackProbe.hpp"

namespace gigrig::app {

class RealtimeIO {
public:
    // Upper bound for one render cycle, and the block size the DSP core is prepared with.
    // Core Audio normally asks for the granted buffer size (32 or 64); larger requests
    // can happen around device changes and are still processed correctly.
    static constexpr int kMaxFrames = GIGRIG_MAX_BLOCK_SIZE;

    RealtimeIO() = default;
    RealtimeIO(const RealtimeIO&) = delete;
    RealtimeIO& operator=(const RealtimeIO&) = delete;

    // Main thread, I/O stopped. The DSP engine is owned by the caller.
    bool prepare(GigRigEngine* dsp, double sampleRate) {
        dsp_ = dsp;
        if (!scratch_) scratch_ = std::make_unique<float[]>(kMaxFrames);
        frameCounter_ = 0;
        probe_.reset();
        return dsp_ != nullptr && gigrig_prepare(dsp_, sampleRate, kMaxFrames) == GIGRIG_OK;
    }

    // Any thread. Zero-based device channel indices.
    void setInputChannel(int channel) noexcept { inputChannel_.store(channel, std::memory_order_relaxed); }
    void setOutputToBoth(bool both) noexcept { outputBoth_.store(both, std::memory_order_relaxed); }
    int inputChannel() const noexcept { return inputChannel_.load(std::memory_order_relaxed); }
    bool outputToBoth() const noexcept { return outputBoth_.load(std::memory_order_relaxed); }

    LoopbackProbe& probe() noexcept { return probe_; }

    // Audio thread. in[c] / out[c] are the non-interleaved device channels of this cycle.
    // Mono in from the selected input channel, mono out on output 1 (and 2 if enabled),
    // silence on every other output channel.
    void renderMono(const float* const* in, int numIn, float* const* out, int numOut,
                    int numFrames) noexcept {
        if (numFrames <= 0) return;
        if (numFrames > kMaxFrames || !scratch_) {
            clearOutputs(out, numOut, numFrames);
            return;
        }
        const int ch = inputChannel_.load(std::memory_order_relaxed);
        const float* src = (in != nullptr && ch >= 0 && ch < numIn) ? in[ch] : nullptr;
        float* mono = scratch_.get();

        probe_.processInput(src, numFrames, frameCounter_);
        gigrig_process(dsp_, src, mono, numFrames);  // src == nullptr gives silence
        probe_.processOutput(mono, numFrames, frameCounter_);
        frameCounter_ += static_cast<uint64_t>(numFrames);

        const bool both = outputBoth_.load(std::memory_order_relaxed);
        for (int c = 0; c < numOut; ++c) {
            float* dst = out[c];
            if (dst == nullptr) continue;
            if (c == 0 || (c == 1 && both)) {
                std::copy(mono, mono + numFrames, dst);
            } else {
                std::fill(dst, dst + numFrames, 0.0f);
            }
        }
    }

    static void clearOutputs(float* const* out, int numOut, int numFrames) noexcept {
        if (out == nullptr) return;
        for (int c = 0; c < numOut; ++c) {
            if (out[c] != nullptr) std::fill(out[c], out[c] + numFrames, 0.0f);
        }
    }

private:
    GigRigEngine* dsp_ = nullptr;
    std::unique_ptr<float[]> scratch_;
    uint64_t frameCounter_ = 0;
    std::atomic<int> inputChannel_{0};
    std::atomic<bool> outputBoth_{false};
    LoopbackProbe probe_;
};

}  // namespace gigrig::app
