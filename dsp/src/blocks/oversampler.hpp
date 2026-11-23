// oversampler.hpp - 2x and 4x oversampling with polyphase IIR half-band filters.
//
// Each 2x stage is a half-band lowpass built from two parallel chains of
// first-order allpass sections in z^-2 (the polyphase form), so the filter
// runs at the low rate and costs one multiply per coefficient per sample.
// Being IIR and causal it needs no lookahead: the only delay is the filter's
// own group delay (about 3.6 samples at 2x in the passband for stage one),
// unlike linear-phase FIR oversampling, which adds half its length as
// latency. This is how the core keeps "zero added latency" with nonlinear
// stages.
//
// Stage one (base <-> 2x) uses 8 coefficients with a 0.04 transition band:
// passband flat to about 0.42 of the base rate, stopband rejection about 99 dB.
// Stage two (2x <-> 4x) only has to reject images of an already band-limited
// signal, so it uses 6 coefficients with a 0.1 transition band (about 104 dB).
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace gigrig {

// Allpass coefficients of a half-band filter with the given number of
// coefficients (1 to kMaxHalfbandCoefficients) and normalised transition
// bandwidth (0 < tb < 0.5, relative to the high sample rate). Elliptic design
// after Valenzuela and Constantinides; even indices form the first branch.
// Not real-time safe (uses transcendental series), call from prepare.
inline constexpr int kMaxHalfbandCoefficients = 16;
std::array<double, kMaxHalfbandCoefficients> designHalfband(int numCoefficients, double transition) noexcept;

// One 2x stage. Holds separate state for upsampling and downsampling, because
// a nonlinear block runs an upsampler and a downsampler around itself.
class HalfbandStage {
public:
    void design(int numCoefficients, double transition) noexcept;
    void reset() noexcept;

    // One low-rate sample in, two high-rate samples out (gain 1 in the passband).
    void upsample(float input, float& out0, float& out1) noexcept;

    // Two high-rate samples in (in0 earlier), one low-rate sample out.
    float downsample(float in0, float in1) noexcept;

    int numCoefficients() const noexcept { return n_; }
    const std::array<double, kMaxHalfbandCoefficients>& coefficients() const noexcept { return c_; }

private:
    // State of one first-order allpass (a + z^-1) / (1 + a z^-1) at the low rate.
    struct Allpass {
        double x1 = 0.0;
        double y1 = 0.0;
    };

    // Runs a branch: sections start, start + 2, start + 4 ... of the coefficient list.
    double branch(std::array<Allpass, kMaxHalfbandCoefficients>& state, int start, double x) noexcept;

    std::array<double, kMaxHalfbandCoefficients> c_{};
    int n_ = 0;
    std::array<Allpass, kMaxHalfbandCoefficients> up_{};
    std::array<Allpass, kMaxHalfbandCoefficients> down_{};
};

// Oversamples a block by 1, 2 or 4. Typical use around a nonlinear stage:
//     float* hi = os.upsample(in, n);        // n * factor() samples
//     for (int i = 0; i < n * os.factor(); ++i) hi[i] = shaper.process(hi[i]);
//     os.downsample(out, n);
class Oversampler {
public:
    // Not real-time safe: allocates the high-rate buffer. factor is 1, 2 or 4
    // (anything else is treated as 4, the plan's default for nonlinear blocks).
    void prepare(int factor, int maxBlockSize);
    void reset() noexcept;

    // Upsamples n low-rate samples into the internal buffer and returns it.
    float* upsample(const float* input, int numSamples) noexcept;

    // Downsamples the internal buffer (n * factor() samples) into n output samples.
    void downsample(float* output, int numSamples) noexcept;

    float* buffer() noexcept { return buffer_.data(); }
    int factor() const noexcept { return factor_; }

private:
    int factor_ = 1;
    int maxBlock_ = 0;
    HalfbandStage stage1_;  // base <-> 2x
    HalfbandStage stage2_;  // 2x <-> 4x
    std::vector<float> buffer_;
};

}  // namespace gigrig
