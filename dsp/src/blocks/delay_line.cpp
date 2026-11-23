#include "delay_line.hpp"

#include <algorithm>
#include <cmath>

namespace gigrig {

void DelayLine::prepare(int maxDelaySamples) {
    maxDelay_ = std::max(2, maxDelaySamples);
    // Two extra samples beyond the maximum for the cubic read's outer point.
    std::size_t size = 4;
    while (size < static_cast<std::size_t>(maxDelay_) + 3) {
        size <<= 1;
    }
    buffer_.assign(size, 0.0f);
    mask_ = size - 1;
    writeIndex_ = 0;
}

void DelayLine::reset() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

float DelayLine::readLinear(double delaySamples) const noexcept {
    const double d = std::clamp(delaySamples, 1.0, static_cast<double>(maxDelay_));
    const int i = static_cast<int>(d);
    const float frac = static_cast<float>(d - i);
    const float a = at(i);
    const float b = at(std::min(i + 1, maxDelay_ + 1));
    return a + frac * (b - a);
}

float DelayLine::readCubic(double delaySamples) const noexcept {
    const double d = std::clamp(delaySamples, 2.0, static_cast<double>(maxDelay_));
    const int i = static_cast<int>(d);
    const float t = static_cast<float>(d - i);
    // Points ordered from newest to oldest; the read point lies between y0 and y1.
    const float ym1 = at(i - 1);
    const float y0 = at(i);
    const float y1 = at(i + 1);
    const float y2 = at(i + 2);
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

}  // namespace gigrig
