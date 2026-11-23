// fft.hpp - real-input FFT of power-of-two size, for the cabinet convolver.
//
// A real sequence of n samples is packed into a complex sequence of n/2
// points, transformed with an iterative radix-2 FFT and unpacked into the
// n/2 + 1 bins of the real spectrum. Twiddles and the bit-reversal order are
// tabulated in prepare, so forward and inverse never allocate and never call
// sin or cos. The spectrum is held as separate real and imaginary arrays
// (split complex), the layout the convolver's multiply-accumulate loops want.
//
// Scaling: inverse(forward(x)) == n * x. The convolver folds 1/n into its
// stored filter spectra so the audio path pays nothing for it.
#pragma once

#include <cstddef>
#include <vector>

namespace gigrig {

class RealFft {
public:
    // Not real-time safe. n must be a power of two, at least 4.
    void prepare(int n);

    int size() const noexcept { return n_; }
    int bins() const noexcept { return n_ / 2 + 1; }

    // n real samples in; bins() real and imaginary parts out.
    void forward(const float* in, float* re, float* im) noexcept;

    // bins() real and imaginary parts in (imaginary parts of bin 0 and n/2 are ignored);
    // n real samples out, scaled by n.
    void inverse(const float* re, const float* im, float* out) noexcept;

private:
    // In-place complex FFT of m_ points on (zr_, zi_); sign -1 forward, +1 inverse.
    void complexFft(float sign) noexcept;

    int n_ = 0;
    int m_ = 0;  // n / 2, the size of the complex FFT
    std::vector<std::size_t> bitReverse_;
    std::vector<float> twCos_, twSin_;      // m_/2 twiddles of the complex FFT
    std::vector<float> packCos_, packSin_;  // m_+1 twiddles of the real packing step
    std::vector<float> zr_, zi_;            // work buffer
};

}  // namespace gigrig
