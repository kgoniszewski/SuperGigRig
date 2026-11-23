// delay_line.hpp - fractional delay line on a power-of-two circular buffer.
//
// Convention: in each sample, read first and then write. read(d) returns the
// input from d samples ago, so a delay of 1 is the previous input. Cubic reads
// need two samples on each side of the read point, so their delay is clamped
// to at least 2 samples; delay effects never need less.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gigrig {

class DelayLine {
public:
    // Not real-time safe: allocates room for maxDelaySamples plus the interpolation margin.
    void prepare(int maxDelaySamples);

    void reset() noexcept;

    void write(float x) noexcept {
        buffer_[writeIndex_] = x;
        writeIndex_ = (writeIndex_ + 1) & mask_;
    }

    // Integer delay, clamped to [1, maxDelay()].
    float read(int delaySamples) const noexcept {
        const int d = delaySamples < 1 ? 1 : (delaySamples > maxDelay_ ? maxDelay_ : delaySamples);
        return at(d);
    }

    // Fractional delay with linear interpolation, clamped to [1, maxDelay()].
    float readLinear(double delaySamples) const noexcept;

    // Fractional delay with cubic (Catmull-Rom) interpolation, clamped to [2, maxDelay()].
    // Cubic keeps the high end intact when the delay time is modulated or moved.
    float readCubic(double delaySamples) const noexcept;

    int maxDelay() const noexcept { return maxDelay_; }

private:
    float at(int delay) const noexcept {
        return buffer_[(writeIndex_ - static_cast<std::size_t>(delay)) & mask_];
    }

    std::vector<float> buffer_ = std::vector<float>(4, 0.0f);
    std::size_t mask_ = 3;
    std::size_t writeIndex_ = 0;
    int maxDelay_ = 1;
};

}  // namespace gigrig
