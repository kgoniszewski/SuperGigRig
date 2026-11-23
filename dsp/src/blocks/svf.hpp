// svf.hpp - state-variable filter for swept filters (envelope filter, wah).
//
// Trapezoidal (topology-preserving) integration: the filter stays stable and
// keeps its tuning when the cutoff moves every sample, which a biquad whose
// coefficients are swapped per sample does not.
#pragma once

#include "dsp_math.hpp"

namespace gigrig {

class StateVariableFilter {
public:
    // bandPass peaks at a gain of q; scale it by 1/q for a unity-gain peak.
    struct Outputs {
        float lowPass;
        float bandPass;
        float highPass;
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        setParameters(cutoff_, q_);
        reset();
    }

    void reset() noexcept {
        ic1_ = 0.0;
        ic2_ = 0.0;
    }

    // Cheap enough to call once per sample: one tan and one division.
    void setParameters(double cutoffHz, double q) noexcept {
        cutoff_ = cutoffHz;
        q_ = q;
        const double f = std::clamp(cutoffHz, 1.0, 0.49 * sampleRate_);
        g_ = std::tan(kPi * f / sampleRate_);
        k_ = 1.0 / std::max(q, 1.0e-3);
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    Outputs process(float input) noexcept {
        const auto v0 = static_cast<double>(input);
        const double v3 = v0 - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return {static_cast<float>(v2), static_cast<float>(v1), static_cast<float>(v0 - k_ * v1 - v2)};
    }

    double cutoff() const noexcept { return cutoff_; }
    double q() const noexcept { return q_; }

private:
    double sampleRate_ = 48000.0;
    double cutoff_ = 1000.0;
    double q_ = 0.7071;
    double g_ = 0.0, k_ = 0.0, a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

}  // namespace gigrig
