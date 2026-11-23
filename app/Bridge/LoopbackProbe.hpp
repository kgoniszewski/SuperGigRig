// LoopbackProbe.hpp - round-trip latency measurement with a cable from output to input.
//
// The probe mutes the normal output, waits for the input to settle, writes a short
// click into the output stream and counts the frames until the click comes back on
// the input. Output and input positions are counted on the same frame counter, so the
// result is the true round trip (buffers, converters and USB safety offsets together),
// measured rather than estimated (docs/architecture.md, section 6).
//
// Threading: arm() and the result readers run on the main thread; processInput() and
// processOutput() run on the audio thread. They share only atomics. Nothing here
// allocates, locks or makes system calls.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace gigrig::app {

class LoopbackProbe {
public:
    static constexpr int kMaxPings = 8;
    static constexpr int kClickFrames = 4;
    static constexpr float kClickLevel = 0.5f;  // -6 dBFS: loud enough to survive the input gain
    static constexpr int32_t kFailed = -1;

    enum class Phase : int { Idle = 0, Settling, Emitting, Listening, Done };

    // Main thread. Starts a measurement of numPings clicks. settleFrames of silence
    // precede each click; a click not detected within timeoutFrames counts as failed.
    // threshold is the absolute input level that counts as the click arriving.
    // Call only while phase() is Idle or Done (the bridge keeps track of that).
    void arm(int numPings, int settleFrames, int timeoutFrames, float threshold) noexcept {
        if (numPings < 1) numPings = 1;
        if (numPings > kMaxPings) numPings = kMaxPings;
        reqPings_ = numPings;
        reqSettle_ = settleFrames > 0 ? settleFrames : 1;
        reqTimeout_ = timeoutFrames > 0 ? timeoutFrames : 1;
        reqThreshold_ = threshold;
        resultCount_.store(0, std::memory_order_relaxed);
        // Release publishes the req* fields to the audio thread.
        request_.fetch_add(1, std::memory_order_release);
    }

    // Main thread, audio stopped. Drops any measurement in progress and its results.
    void reset() noexcept {
        seenRequest_ = request_.load(std::memory_order_acquire);
        state_ = Phase::Idle;
        phase_.store(static_cast<int>(state_), std::memory_order_release);
        resultCount_.store(0, std::memory_order_release);
        done_ = 0;
        clickWritten_ = 0;
    }

    // Main thread. Phase as last seen by the audio thread.
    Phase phase() const noexcept { return static_cast<Phase>(phase_.load(std::memory_order_acquire)); }

    // Main thread. Results published so far, each in frames, or kFailed.
    int resultCount() const noexcept { return resultCount_.load(std::memory_order_acquire); }
    int32_t result(int index) const noexcept {
        if (index < 0 || index >= kMaxPings) return kFailed;
        return results_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
    }

    // Audio thread. Call with the captured input of this cycle (may be nullptr when the
    // selected input channel does not exist), before processOutput for the same cycle.
    // firstFrame is the shared frame counter value of in[0].
    void processInput(const float* in, int numFrames, uint64_t firstFrame) noexcept {
        pickUpRequest(firstFrame);
        if (state_ != Phase::Listening) return;
        for (int i = 0; i < numFrames; ++i) {
            const uint64_t frame = firstFrame + static_cast<uint64_t>(i);
            if (frame <= clickFrame_) continue;
            if (frame - clickFrame_ > static_cast<uint64_t>(timeout_)) {
                publish(kFailed, frame);
                return;
            }
            if (in != nullptr && std::fabs(in[i]) >= threshold_) {
                publish(static_cast<int32_t>(frame - clickFrame_), frame);
                return;
            }
        }
        // The selected input may vanish; time out on the counter alone.
        const uint64_t end = firstFrame + static_cast<uint64_t>(numFrames);
        if (end > clickFrame_ && end - clickFrame_ > static_cast<uint64_t>(timeout_)) {
            publish(kFailed, end);
        }
    }

    // Audio thread. Returns true while a measurement owns the output: out then holds
    // silence or the click and must be sent to the device instead of the guitar signal.
    bool processOutput(float* out, int numFrames, uint64_t firstFrame) noexcept {
        if (state_ == Phase::Idle || state_ == Phase::Done) return false;
        for (int i = 0; i < numFrames; ++i) out[i] = 0.0f;

        if (state_ == Phase::Settling && firstFrame >= settleUntil_) {
            state_ = Phase::Emitting;
        }
        if (state_ == Phase::Emitting) {
            clickFrame_ = firstFrame;
            clickWritten_ = 0;
            state_ = Phase::Listening;
            phase_.store(static_cast<int>(state_), std::memory_order_release);
        }
        // The click may straddle two callbacks when the buffer is shorter than it.
        if (state_ == Phase::Listening && clickWritten_ < kClickFrames) {
            for (int i = 0; i < numFrames && clickWritten_ < kClickFrames; ++i) {
                const uint64_t frame = firstFrame + static_cast<uint64_t>(i);
                if (frame >= clickFrame_) {
                    out[i] = kClickLevel;
                    ++clickWritten_;
                }
            }
        }
        return true;
    }

private:
    void pickUpRequest(uint64_t now) noexcept {
        const uint32_t req = request_.load(std::memory_order_acquire);
        if (req == seenRequest_) return;
        seenRequest_ = req;
        pings_ = reqPings_;
        settle_ = reqSettle_;
        timeout_ = reqTimeout_;
        threshold_ = reqThreshold_;
        done_ = 0;
        startSettling(now);
    }

    void startSettling(uint64_t now) noexcept {
        settleUntil_ = now + static_cast<uint64_t>(settle_);
        state_ = Phase::Settling;
        phase_.store(static_cast<int>(state_), std::memory_order_release);
    }

    void publish(int32_t value, uint64_t now) noexcept {
        results_[static_cast<std::size_t>(done_)].store(value, std::memory_order_relaxed);
        ++done_;
        resultCount_.store(done_, std::memory_order_release);
        if (done_ >= pings_) {
            state_ = Phase::Done;
            phase_.store(static_cast<int>(state_), std::memory_order_release);
        } else {
            startSettling(now);
        }
    }

    // Written by the main thread before request_ is bumped, read by the audio thread after.
    int reqPings_ = 1;
    int reqSettle_ = 1;
    int reqTimeout_ = 1;
    float reqThreshold_ = 0.05f;
    std::atomic<uint32_t> request_{0};

    // Shared results.
    std::array<std::atomic<int32_t>, kMaxPings> results_{};
    std::atomic<int> resultCount_{0};
    std::atomic<int> phase_{static_cast<int>(Phase::Idle)};

    // Audio thread only.
    uint32_t seenRequest_ = 0;
    Phase state_ = Phase::Idle;
    int pings_ = 1;
    int settle_ = 1;
    int timeout_ = 1;
    float threshold_ = 0.05f;
    int done_ = 0;
    int clickWritten_ = 0;
    uint64_t settleUntil_ = 0;
    uint64_t clickFrame_ = 0;
};

}  // namespace gigrig::app
