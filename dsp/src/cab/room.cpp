#include "room.hpp"

#include <algorithm>
#include <cmath>

#include "blocks/dsp_math.hpp"

namespace gigrig {

namespace {

// Line lengths of the small room in ms. Spread over roughly an octave and
// mutually prime once rounded at any common rate, so no mode repeats.
constexpr double kLineMs[Room::kLines] = {7.3, 8.9, 10.1, 11.7, 13.3, 14.9, 16.7, 18.7};

struct SizeSetting {
    double scale;     // multiplies kLineMs
    double rt60;      // seconds
    double dampingHz; // corner of the in-loop lowpass
};
constexpr SizeSetting kSizeSettings[Room::kSizes] = {
    {1.0, 0.30, 7500.0},  // small: a booth
    {1.7, 0.55, 6000.0},  // medium: a live room
    {2.6, 0.90, 4800.0},  // large: a hall-sized stage
};

constexpr float kInputGain = 0.5f;
constexpr double kFadeSeconds = 0.01;

int lineSamples(int line, int size, double sampleRate) {
    return std::max(1, static_cast<int>(std::lround(kLineMs[line] * kSizeSettings[size].scale * 0.001 * sampleRate)));
}

}  // namespace

void Room::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kLines; ++i) {
        lines_[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(lineSamples(i, kSizes - 1, sampleRate)),
                                                   0.0f);
    }
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kFadeSeconds * sampleRate)));
    applySize(pendingSize_);
}

void Room::reset() noexcept {
    applySize(pendingSize_);
}

void Room::applySize(int size) noexcept {
    size_ = std::clamp(size, 0, kSizes - 1);
    pendingSize_ = size_;
    fadeRemaining_ = 0;
    const SizeSetting& s = kSizeSettings[size_];
    for (std::size_t i = 0; i < kLines; ++i) {
        length_[i] = lineSamples(static_cast<int>(i), size_, sampleRate_);
        pos_[i] = 0;
        lowpassState_[i] = 0.0f;
        std::fill(lines_[i].begin(), lines_[i].end(), 0.0f);
        // Each pass through a line must lose length / (rt60 * fs) of 60 dB.
        gain_[i] = static_cast<float>(std::pow(10.0, -3.0 * length_[i] / (s.rt60 * sampleRate_)));
    }
    lowpassCoeff_ = static_cast<float>(std::exp(-kTwoPi * s.dampingHz / sampleRate_));
}

void Room::setSize(int size) noexcept {
    size = std::clamp(size, 0, kSizes - 1);
    if (size == pendingSize_) {
        return;
    }
    pendingSize_ = size;
    if (fadeRemaining_ == 0) {
        fadeRemaining_ = fadeLength_;
    }
}

float Room::processSample(float in) noexcept {
    std::array<float, kLines> v{};
    float out = 0.0f;
    float sum = 0.0f;
    for (std::size_t i = 0; i < kLines; ++i) {
        const float o = lines_[i][static_cast<std::size_t>(pos_[i])];
        // Alternating output signs keep the sum from favouring the modes all lines share.
        out += (i & 1u) != 0 ? -o : o;
        lowpassState_[i] = o + lowpassCoeff_ * (lowpassState_[i] - o);
        v[i] = gain_[i] * lowpassState_[i];
        sum += v[i];
    }
    // Householder reflection: lossless, and costs one sum instead of a matrix multiply.
    const float reflect = sum * (2.0f / kLines);
    const float x = in * kInputGain;
    for (std::size_t i = 0; i < kLines; ++i) {
        lines_[i][static_cast<std::size_t>(pos_[i])] = v[i] - reflect + ((i & 2u) != 0 ? -x : x);
        if (++pos_[i] == length_[i]) {
            pos_[i] = 0;
        }
    }
    out *= 0.35355339f;  // 1 / sqrt(kLines)

    if (fadeRemaining_ > 0) {
        out *= static_cast<float>(fadeRemaining_) / static_cast<float>(fadeLength_);
        if (--fadeRemaining_ == 0) {
            applySize(pendingSize_);
        }
    }
    return out;
}

}  // namespace gigrig
