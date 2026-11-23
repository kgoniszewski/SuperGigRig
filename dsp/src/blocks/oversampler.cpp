#include "oversampler.hpp"

#include <algorithm>

#include "dsp_math.hpp"

namespace gigrig {
namespace {

double integerPower(double x, int n) noexcept {
    double r = 1.0;
    for (int i = 0; i < n; ++i) {
        r *= x;
    }
    return r;
}

// Theta-function style series of the elliptic design; they converge after a few terms.
double seriesNumerator(double q, int order, int c) noexcept {
    double sum = 0.0;
    double sign = 1.0;
    for (int i = 0; i < 64; ++i) {
        const double term = integerPower(q, i * (i + 1)) * std::sin((i * 2 + 1) * c * kPi / order) * sign;
        sum += term;
        sign = -sign;
        if (std::fabs(term) < 1.0e-100) {
            break;
        }
    }
    return sum;
}

double seriesDenominator(double q, int order, int c) noexcept {
    double sum = 0.0;
    double sign = -1.0;
    for (int i = 1; i < 64; ++i) {
        const double term = integerPower(q, i * i) * std::cos(i * 2 * c * kPi / order) * sign;
        sum += term;
        sign = -sign;
        if (std::fabs(term) < 1.0e-100) {
            break;
        }
    }
    return sum;
}

}  // namespace

std::array<double, kMaxHalfbandCoefficients> designHalfband(int numCoefficients, double transition) noexcept {
    std::array<double, kMaxHalfbandCoefficients> coefs{};
    const int n = std::clamp(numCoefficients, 1, kMaxHalfbandCoefficients);
    const double tb = std::clamp(transition, 1.0e-4, 0.4999);

    double k = std::tan((1.0 - tb * 2.0) * kPi / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e4 = integerPower(e, 4);
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

    const int order = n * 2 + 1;
    for (int index = 0; index < n; ++index) {
        const int c = index + 1;
        const double num = seriesNumerator(q, order, c) * std::pow(q, 0.25);
        const double den = seriesDenominator(q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[static_cast<std::size_t>(index)] = (1.0 - x) / (1.0 + x);
    }
    return coefs;
}

void HalfbandStage::design(int numCoefficients, double transition) noexcept {
    n_ = std::clamp(numCoefficients, 1, kMaxHalfbandCoefficients);
    c_ = designHalfband(n_, transition);
    reset();
}

void HalfbandStage::reset() noexcept {
    up_.fill({});
    down_.fill({});
}

double HalfbandStage::branch(std::array<Allpass, kMaxHalfbandCoefficients>& state, int start,
                             double x) noexcept {
    for (int i = start; i < n_; i += 2) {
        const auto idx = static_cast<std::size_t>(i);
        Allpass& s = state[idx];
        const double y = c_[idx] * (x - s.y1) + s.x1;
        s.x1 = x;
        s.y1 = y;
        x = y;
    }
    return x;
}

// H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2. Zero-stuffing then filtering with 2 H
// leaves A0 on the even output phase and A1 on the odd one.
void HalfbandStage::upsample(float input, float& out0, float& out1) noexcept {
    const auto x = static_cast<double>(input);
    out0 = static_cast<float>(branch(up_, 0, x));
    out1 = static_cast<float>(branch(up_, 1, x));
}

// Filtering with H and keeping the second sample of each pair: A0 sees that
// newest sample and A1 (behind the z^-1) the one before it.
float HalfbandStage::downsample(float in0, float in1) noexcept {
    const double a = branch(down_, 0, static_cast<double>(in1));
    const double b = branch(down_, 1, static_cast<double>(in0));
    return static_cast<float>(0.5 * (a + b));
}

void Oversampler::prepare(int factor, int maxBlockSize) {
    factor_ = factor == 1 ? 1 : (factor == 2 ? 2 : 4);
    maxBlock_ = std::max(1, maxBlockSize);
    stage1_.design(8, 0.04);
    stage2_.design(6, 0.1);
    buffer_.assign(static_cast<std::size_t>(maxBlock_) * 4, 0.0f);
    reset();
}

void Oversampler::reset() noexcept {
    stage1_.reset();
    stage2_.reset();
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
}

float* Oversampler::upsample(const float* input, int numSamples) noexcept {
    const int n = std::min(numSamples, maxBlock_);
    float* hi = buffer_.data();
    if (factor_ == 1) {
        std::copy(input, input + n, hi);
        return hi;
    }
    if (factor_ == 2) {
        for (int i = 0; i < n; ++i) {
            stage1_.upsample(input[i], hi[2 * i], hi[2 * i + 1]);
        }
        return hi;
    }
    for (int i = 0; i < n; ++i) {
        float m0 = 0.0f, m1 = 0.0f;
        stage1_.upsample(input[i], m0, m1);
        stage2_.upsample(m0, hi[4 * i], hi[4 * i + 1]);
        stage2_.upsample(m1, hi[4 * i + 2], hi[4 * i + 3]);
    }
    return hi;
}

void Oversampler::downsample(float* output, int numSamples) noexcept {
    const int n = std::min(numSamples, maxBlock_);
    const float* hi = buffer_.data();
    if (factor_ == 1) {
        std::copy(hi, hi + n, output);
        return;
    }
    if (factor_ == 2) {
        for (int i = 0; i < n; ++i) {
            output[i] = stage1_.downsample(hi[2 * i], hi[2 * i + 1]);
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        const float m0 = stage2_.downsample(hi[4 * i], hi[4 * i + 1]);
        const float m1 = stage2_.downsample(hi[4 * i + 2], hi[4 * i + 3]);
        output[i] = stage1_.downsample(m0, m1);
    }
}

}  // namespace gigrig
