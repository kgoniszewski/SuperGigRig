// room.hpp - the small room the two mic slots send into (plan section 5, "Room").
//
// An 8-line feedback delay network with a Householder feedback matrix: every
// line feeds every other one, the matrix is lossless, and the decay comes from
// a gain per line set from the room's RT60. A one-pole lowpass in each line
// makes the highs die first, as walls and air do. Three sizes scale the line
// lengths and the decay together.
//
// Changing the size fades the room out over 10 ms, clears it and starts the
// new size from silence, so the delay lines never jump mid-tail.
#pragma once

#include <array>
#include <vector>

namespace gigrig {

class Room {
public:
    static constexpr int kLines = 8;
    static constexpr int kSizes = 3;  // small, medium, large

    // Not real-time safe.
    void prepare(double sampleRate);

    // Real-time safe.
    void reset() noexcept;

    // Real-time safe. size is 0, 1 or 2; takes effect after a short fade.
    void setSize(int size) noexcept;

    // Real-time safe. Wet output only; adds nothing to in when silent. In-place allowed.
    float processSample(float in) noexcept;

private:
    void applySize(int size) noexcept;

    double sampleRate_ = 48000.0;
    std::array<std::vector<float>, kLines> lines_;
    std::array<int, kLines> length_{};
    std::array<int, kLines> pos_{};
    std::array<float, kLines> gain_{};
    std::array<float, kLines> lowpassState_{};
    float lowpassCoeff_ = 0.0f;

    int size_ = 1;
    int pendingSize_ = 1;
    int fadeLength_ = 1;
    int fadeRemaining_ = 0;  // > 0 while fading out before a size change
};

}  // namespace gigrig
