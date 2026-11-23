// waveshaper.hpp - static nonlinearities with first-order antiderivative anti-aliasing.
//
// A waveshaper y = f(x) creates harmonics far above Nyquist that fold back as
// inharmonic aliasing. First-order antiderivative anti-aliasing (ADAA) outputs
// the average of f over the straight line between consecutive inputs:
//     y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),  F' = f
// which strongly damps the folded components at the cost of a gentle high-cut
// and half a sample of delay at the (oversampled) rate. Combined with 4x
// oversampling this keeps aliasing far below the audible floor at a fraction
// of the cost of oversampling alone.
//
// A shape is any type with const member functions f(double) and F(double).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gigrig {

namespace shapes {

// Symmetric soft clipper; the reference shape for tests and simple stages.
struct Tanh {
    double f(double x) const noexcept { return std::tanh(x); }
    // log(cosh(x)), written so it cannot overflow for large |x|.
    double F(double x) const noexcept {
        const double a = std::fabs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - std::log(2.0);
    }
};

// Hard clip at +-1, the limit case of diode clipping.
struct HardClip {
    double f(double x) const noexcept { return std::clamp(x, -1.0, 1.0); }
    double F(double x) const noexcept {
        const double a = std::fabs(x);
        return a <= 1.0 ? 0.5 * x * x : a - 0.5;
    }
};

// Rational soft clipper x / sqrt(1 + x^2): softer knee than tanh, cheap.
struct SoftSqrt {
    double f(double x) const noexcept { return x / std::sqrt(1.0 + x * x); }
    double F(double x) const noexcept { return std::sqrt(1.0 + x * x) - 1.0; }
};

}  // namespace shapes

// A transfer curve sampled on a uniform grid, for curves without a closed form
// (triode plate curves, measured diode pairs). Between grid points f is linear,
// so its antiderivative is an exact piecewise quadratic. Outside the grid f is
// held at its edge values, as a tube or diode stage saturates.
class WaveshaperTable {
public:
    // Not real-time safe. fn is any callable double -> double; points >= 2.
    template <class Fn>
    void build(Fn&& fn, double xMin, double xMax, std::size_t points) {
        points = std::max<std::size_t>(points, 2);
        xMin_ = xMin;
        xMax_ = xMax > xMin ? xMax : xMin + 1.0;
        step_ = (xMax_ - xMin_) / static_cast<double>(points - 1);
        f_.resize(points);
        F_.resize(points);
        for (std::size_t i = 0; i < points; ++i) {
            f_[i] = fn(xMin_ + step_ * static_cast<double>(i));
        }
        // Integrate exactly (trapezoids are exact for a piecewise-linear f), with F(0) = 0
        // when 0 lies on the grid range so that silence stays silent through ADAA.
        F_[0] = 0.0;
        for (std::size_t i = 1; i < points; ++i) {
            F_[i] = F_[i - 1] + 0.5 * step_ * (f_[i - 1] + f_[i]);
        }
        const double offset = antiderivativeRaw(0.0);
        for (double& v : F_) {
            v -= offset;
        }
    }

    double f(double x) const noexcept {
        if (x <= xMin_) return f_.front();
        if (x >= xMax_) return f_.back();
        const double pos = (x - xMin_) / step_;
        const auto i = std::min(static_cast<std::size_t>(pos), f_.size() - 2);
        const double t = pos - static_cast<double>(i);
        return f_[i] + t * (f_[i + 1] - f_[i]);
    }

    double F(double x) const noexcept { return antiderivativeRaw(x); }

    bool empty() const noexcept { return f_.empty(); }

private:
    double antiderivativeRaw(double x) const noexcept {
        if (x <= xMin_) return F_.front() + f_.front() * (x - xMin_);
        if (x >= xMax_) return F_.back() + f_.back() * (x - xMax_);
        const double pos = (x - xMin_) / step_;
        const auto i = std::min(static_cast<std::size_t>(pos), f_.size() - 2);
        const double t = pos - static_cast<double>(i);
        return F_[i] + step_ * t * (f_[i] + 0.5 * t * (f_[i + 1] - f_[i]));
    }

    std::vector<double> f_;
    std::vector<double> F_;
    double xMin_ = -1.0;
    double xMax_ = 1.0;
    double step_ = 1.0;
};

namespace shapes {

// Non-owning view of a WaveshaperTable, so tables can be shared between stages.
struct Table {
    const WaveshaperTable* table = nullptr;
    double f(double x) const noexcept { return table->f(x); }
    double F(double x) const noexcept { return table->F(x); }
};

}  // namespace shapes

// Runs a shape with first-order ADAA. Process at the oversampled rate.
template <class Shape>
class AdaaWaveshaper {
public:
    AdaaWaveshaper() = default;
    explicit AdaaWaveshaper(Shape shape) : shape_(shape) {}

    void setShape(Shape shape) noexcept {
        shape_ = shape;
        reset();
    }
    const Shape& shape() const noexcept { return shape_; }

    void reset() noexcept {
        x1_ = 0.0;
        F1_ = shape_.F(0.0);
    }

    float process(float input) noexcept {
        const auto x = static_cast<double>(input);
        const double Fx = shape_.F(x);
        const double dx = x - x1_;
        // When consecutive inputs nearly coincide the quotient is ill-conditioned;
        // f at the midpoint is the limit of the same average.
        const double y = std::fabs(dx) > kIllConditioned ? (Fx - F1_) / dx : shape_.f(0.5 * (x + x1_));
        x1_ = x;
        F1_ = Fx;
        return static_cast<float>(y);
    }

    void process(float* data, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            data[i] = process(data[i]);
        }
    }

private:
    static constexpr double kIllConditioned = 1.0e-5;
    Shape shape_{};
    double x1_ = 0.0;
    double F1_ = 0.0;
};

// Plain (non-anti-aliased) shaper with the same interface, for comparison in tests
// and for stages where the input is already band limited enough.
template <class Shape>
class NaiveWaveshaper {
public:
    NaiveWaveshaper() = default;
    explicit NaiveWaveshaper(Shape shape) : shape_(shape) {}
    void reset() noexcept {}
    float process(float input) noexcept { return static_cast<float>(shape_.f(static_cast<double>(input))); }

private:
    Shape shape_{};
};

}  // namespace gigrig
