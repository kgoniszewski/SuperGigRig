#include "convolver.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace gigrig {

namespace {

std::size_t idx(int i) noexcept {
    return static_cast<std::size_t>(i);
}

void zero(std::vector<float>& v) noexcept {
    std::fill(v.begin(), v.end(), 0.0f);
}

}  // namespace

void Convolver::Stage::build(const float* ir, int length, int offset, int blockSize, int partitions) {
    block = blockSize;
    parts = partitions;
    if (parts <= 0) {
        parts = 0;
        return;
    }
    const int n = 2 * block;
    fft.prepare(n);
    bins = fft.bins();
    const std::size_t total = idx(parts) * idx(bins);
    hRe.assign(total, 0.0f);
    hIm.assign(total, 0.0f);
    fdlRe.assign(total, 0.0f);
    fdlIm.assign(total, 0.0f);
    accRe.assign(idx(bins), 0.0f);
    accIm.assign(idx(bins), 0.0f);
    window.assign(idx(n), 0.0f);
    time.assign(idx(n), 0.0f);
    outNow.assign(idx(block), 0.0f);
    outNext.assign(idx(block), 0.0f);

    // Each partition goes in the first half of an FFT frame, zero padded, so
    // overlap-save keeps the last half of every inverse transform. The 1/n of
    // the inverse FFT is folded in here, once.
    std::vector<float> frame(idx(n), 0.0f);
    const float scale = 1.0f / static_cast<float>(n);
    for (int s = 0; s < parts; ++s) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < block; ++i) {
            const int tap = offset + s * block + i;
            if (tap < length) {
                frame[idx(i)] = ir[tap] * scale;
            }
        }
        const std::size_t at = idx(s) * idx(bins);
        fft.forward(frame.data(), hRe.data() + at, hIm.data() + at);
    }
    reset();
}

void Convolver::Stage::reset() noexcept {
    zero(fdlRe);
    zero(fdlIm);
    zero(accRe);
    zero(accIm);
    zero(window);
    zero(time);
    zero(outNow);
    zero(outNext);
    fdlPos = 0;
}

void Convolver::Stage::transformInput() noexcept {
    fdlPos = fdlPos == 0 ? parts - 1 : fdlPos - 1;
    const std::size_t at = idx(fdlPos) * idx(bins);
    fft.forward(window.data(), fdlRe.data() + at, fdlIm.data() + at);
    zero(accRe);
    zero(accIm);
}

void Convolver::Stage::accumulate(int from, int to) noexcept {
    float* ar = accRe.data();
    float* ai = accIm.data();
    for (int s = from; s < to; ++s) {
        // The newest input block sits at fdlPos; partition s pairs with the block s periods older.
        const int slot = (fdlPos + s) % parts;
        const float* xr = fdlRe.data() + idx(slot) * idx(bins);
        const float* xi = fdlIm.data() + idx(slot) * idx(bins);
        const float* hr = hRe.data() + idx(s) * idx(bins);
        const float* hi = hIm.data() + idx(s) * idx(bins);
        for (int k = 0; k < bins; ++k) {
            ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
            ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
        }
    }
}

void Convolver::Stage::inverseInto(float* dst) noexcept {
    fft.inverse(accRe.data(), accIm.data(), time.data());
    std::memcpy(dst, time.data() + block, idx(block) * sizeof(float));
}

void Convolver::build(const float* ir, int length) {
    length_ = std::max(0, length);
    direct_.assign(kDirectTaps, 0.0f);
    for (int i = 0; i < std::min(length_, kDirectTaps); ++i) {
        direct_[idx(kDirectTaps - 1 - i)] = ir[i];
    }
    history_.assign(2 * kDirectTaps, 0.0f);

    const int smallTaps = std::clamp(length_ - kDirectTaps, 0, kLargeStart - kDirectTaps);
    small_.build(ir, length_, kDirectTaps, kSmallBlock, (smallTaps + kSmallBlock - 1) / kSmallBlock);
    const int largeTaps = std::max(0, length_ - kLargeStart);
    large_.build(ir, length_, kLargeStart, kLargeBlock, (largeTaps + kLargeBlock - 1) / kLargeBlock);
    largeInput_.assign(kLargeBlock, 0.0f);
    reset();
}

void Convolver::reset() noexcept {
    zero(history_);
    historyPos_ = 0;
    small_.reset();
    large_.reset();
    zero(largeInput_);
    smallPos_ = 0;
    largePos_ = 0;
}

void Convolver::tick() noexcept {
    // Small stage: the 64 inputs just collected produce the next 64 outputs.
    if (small_.active()) {
        small_.transformInput();
        small_.accumulate(0, small_.parts);
        small_.inverseInto(small_.outNow.data());
        std::memcpy(small_.window.data(), small_.window.data() + kSmallBlock, kSmallBlock * sizeof(float));
    }
    if (!large_.active()) {
        return;
    }
    // Large stage: one eighth of its work per tick. Slice 0 starts a block that
    // has just been collected; slice 7 finishes it, a full period before it is due.
    constexpr int kSlices = kLargeBlock / kSmallBlock;
    const int slice = largePos_ / kSmallBlock;
    if (slice == 0) {
        std::swap(large_.outNow, large_.outNext);  // swaps storage only, never allocates
        std::memcpy(large_.window.data(), large_.window.data() + kLargeBlock, kLargeBlock * sizeof(float));
        std::memcpy(large_.window.data() + kLargeBlock, largeInput_.data(), kLargeBlock * sizeof(float));
        large_.transformInput();
    } else if (slice < kSlices - 1) {
        const int p = large_.parts;
        constexpr int kMacSlices = kSlices - 2;
        large_.accumulate((slice - 1) * p / kMacSlices, slice * p / kMacSlices);
    } else {
        large_.inverseInto(large_.outNext.data());
    }
}

void Convolver::process(const float* in, float* out, int n) noexcept {
    if (length_ == 0) {
        std::memset(out, 0, idx(n) * sizeof(float));
        return;
    }
    const float* h = direct_.data();
    for (int i = 0; i < n; ++i) {
        const float x = in[i];

        historyPos_ = (historyPos_ + 1) & (kDirectTaps - 1);
        history_[idx(historyPos_)] = x;
        history_[idx(historyPos_ + kDirectTaps)] = x;
        const float* hist = history_.data() + historyPos_ + 1;
        // Four partial sums let the compiler vectorise without reassociating floats freely.
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        for (int k = 0; k < kDirectTaps; k += 4) {
            s0 += h[k] * hist[k];
            s1 += h[k + 1] * hist[k + 1];
            s2 += h[k + 2] * hist[k + 2];
            s3 += h[k + 3] * hist[k + 3];
        }
        float y = (s0 + s1) + (s2 + s3);

        if (small_.active()) {
            y += small_.outNow[idx(smallPos_)];
            small_.window[idx(kSmallBlock + smallPos_)] = x;
        }
        if (large_.active()) {
            y += large_.outNow[idx(largePos_)];
            largeInput_[idx(largePos_)] = x;
        }
        out[i] = y;

        // The large position advances with the small one, so ticks line up with 64-sample boundaries.
        largePos_ = (largePos_ + 1) & (kLargeBlock - 1);
        if (++smallPos_ == kSmallBlock) {
            smallPos_ = 0;
            tick();
        }
    }
}

}  // namespace gigrig
