// cabinet.hpp - the Cab section: two mic slots, each an IR in a zero-latency
// convolver, with level, phase and a send into a shared room (plan section 4,
// row 9, and section 5, "Cabinet").
//
// IRs arrive as CabIr objects built on the worker queue and handed over by the
// command queue. A new IR crossfades in over 20 ms against the one it
// replaces; the old one is then handed back to be freed off the audio thread.
// While a slot is crossfading, a further load for it waits in the queue.
//
// When no slot holds an IR, or the block is switched off, the section passes
// the signal through untouched (bit exact). Switching between the two states
// is a 20 ms crossfade too, so neither loading the first IR nor the bypass
// switch clicks.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>

#include "blocks/smoother.hpp"
#include "cab/convolver.hpp"
#include "cab/impulse_response.hpp"
#include "cab/room.hpp"
#include "commands.hpp"

namespace gigrig {

// An IR ready to play at one sample rate. Keeps its source so that prepare can
// rebuild it when the session rate changes.
struct CabIr {
    SourceIr source;
    double sampleRate = 0.0;
    Convolver convolver;

    // Not real-time safe. Returns nullptr if the source is empty or silent.
    static std::unique_ptr<CabIr> create(SourceIr source, double sampleRate);

    // Not real-time safe. Returns false (and leaves the IR silent) if the source is silent.
    bool rebuild(double rate);
};

struct CabSlotParams {
    bool on = true;
    float levelDb = 0.0f;
    bool invertPhase = false;
    float roomSend = 0.0f;  // 0 to 1
};

class Cabinet {
public:
    static constexpr int kSlots = 2;
    static constexpr double kFadeSeconds = 0.02;

    Cabinet() = default;
    ~Cabinet();
    Cabinet(const Cabinet&) = delete;
    Cabinet& operator=(const Cabinet&) = delete;

    // Not real-time safe. Rebuilds loaded IRs for the new rate and finishes any crossfade.
    void prepare(double sampleRate);

    // Real-time safe. Clears audio state; finishes any crossfade (the replaced IR is retired).
    void reset() noexcept;

    // Audio thread, from the command drain. Takes ownership of ir (which may be null to
    // empty the slot). Returns nullopt while the slot is busy, so the command waits; an
    // IR built for another sample rate is refused and handed straight back.
    std::optional<Retired> applyLoad(int slot, CabIr* ir) noexcept;

    // Audio thread. Offers each IR that has finished fading out to tryRetire(const Retired&),
    // which returns true when it took it (pushed it onto the return queue). An IR that was
    // not taken is offered again next block, and its slot stays busy until then.
    template <class TryRetire>
    void retireFinished(TryRetire&& tryRetire) noexcept {
        for (Slot& s : slots_) {
            if (s.retiring != nullptr && tryRetire(Retired{s.retiring, &destroyObject<CabIr>})) {
                s.retiring = nullptr;
            }
        }
    }

    // Audio thread, once per block.
    void setParameters(bool on, const std::array<CabSlotParams, kSlots>& slots, int roomSize) noexcept;

    // Real-time safe. In-place allowed; any n.
    void process(const float* in, float* out, int n) noexcept;

    // Any thread: length in samples of the IR now playing in a slot (0 when empty).
    int irLength(int slot) const noexcept;

    // Any thread: IR loads refused because they were built for another sample rate.
    int refusedLoads() const noexcept { return refused_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        CabIr* current = nullptr;
        CabIr* next = nullptr;  // fading in while fading
        CabIr* retiring = nullptr;
        bool fading = false;
        int fadePos = 0;
        LinearSmoother gain;  // on, level and polarity in one ramp, so the phase switch passes through zero
        LinearSmoother send;
        CabSlotParams last;
        bool paramsSeen = false;
        std::atomic<int> length{0};
    };

    void processChunk(const float* in, float* out, int n) noexcept;
    void finishFade(Slot& s) noexcept;
    bool hasIr() const noexcept;
    void resetAudio() noexcept;

    std::array<Slot, kSlots> slots_;
    Room room_;
    LinearSmoother wet_;  // 0 = dry passthrough, 1 = cabinet
    bool enabled_ = true;
    bool idle_ = true;    // wet is 0 and settled: nothing runs
    double sampleRate_ = 48000.0;
    int fadeLength_ = 1;
    std::atomic<int> refused_{0};
};

}  // namespace gigrig
