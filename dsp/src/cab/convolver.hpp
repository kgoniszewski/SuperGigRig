// convolver.hpp - zero-latency convolution for cabinet impulse responses.
//
// Plan section 5, "Cabinet": the first taps run as a direct FIR, the rest as
// non-uniformly partitioned FFT convolution, with the FFT work spread evenly
// over the blocks. The IR is split three ways:
//
//   taps [0, 64)        direct FIR, computed every sample, so there is no latency
//   taps [64, 1024)     uniformly partitioned overlap-save, partitions of 64
//   taps [1024, end)    uniformly partitioned overlap-save, partitions of 512
//
// Why these offsets: the 64-partitions start at tap 64, so a block of 64
// inputs is complete exactly when its first output sample is due. The
// 512-partitions start at tap 1024 = 2 x 512, so their result for a block is
// due one whole 512-sample period after that block is complete. That period
// is used to spread the large FFTs and the multiply-accumulate over eight
// 64-sample ticks instead of one burst.
//
// Everything is driven by the sample count, never by the caller's block size,
// so the output is bit-identical for any block size from 1 upward, and the
// work per 64 samples is roughly constant whatever the host buffer is.
//
// A Convolver is built off the audio thread (build allocates) and then only
// processed and reset on the audio thread.
#pragma once

#include <cstddef>
#include <vector>

#include "blocks/fft.hpp"

namespace gigrig {

class Convolver {
public:
    static constexpr int kDirectTaps = 64;
    static constexpr int kSmallBlock = 64;
    static constexpr int kLargeBlock = 512;
    static constexpr int kLargeStart = 1024;  // first tap handled by the large partitions

    // Not real-time safe. Builds the filter for ir[0, length) and clears the state.
    // A length of 0 gives a convolver that outputs silence.
    void build(const float* ir, int length);

    // Real-time safe. Clears the input history and pending output.
    void reset() noexcept;

    // Real-time safe. Any n >= 0; in-place allowed.
    void process(const float* in, float* out, int n) noexcept;

    int length() const noexcept { return length_; }

private:
    // One uniformly partitioned overlap-save stage over taps [offset, offset + parts * block).
    struct Stage {
        int block = 0;
        int parts = 0;
        int bins = 0;
        RealFft fft;
        std::vector<float> hRe, hIm;      // parts x bins, scaled by 1 / fft size
        std::vector<float> fdlRe, fdlIm;  // parts x bins, frequency-domain delay line
        std::vector<float> accRe, accIm;  // bins
        std::vector<float> window;        // 2 * block: previous and current input block
        std::vector<float> time;          // 2 * block: inverse FFT result
        std::vector<float> outNow;        // block: output being played
        std::vector<float> outNext;       // block: output being computed (large stage)
        int fdlPos = 0;

        void build(const float* ir, int length, int offset, int blockSize, int partitions);
        void reset() noexcept;
        void transformInput() noexcept;        // FFT of the window into the FDL head
        void accumulate(int from, int to) noexcept;  // acc += sum of FDL x H over partitions [from, to)
        void inverseInto(float* dst) noexcept;
        bool active() const noexcept { return parts > 0; }
    };

    void tick() noexcept;  // every 64 samples

    int length_ = 0;
    std::vector<float> direct_;   // reversed direct taps, kDirectTaps
    std::vector<float> history_;  // 2 * kDirectTaps, written twice so the dot product is contiguous
    int historyPos_ = 0;

    Stage small_;
    Stage large_;
    std::vector<float> largeInput_;  // kLargeBlock: input being collected for the large stage
    int smallPos_ = 0;               // 0 .. kSmallBlock - 1
    int largePos_ = 0;               // 0 .. kLargeBlock - 1
};

}  // namespace gigrig
