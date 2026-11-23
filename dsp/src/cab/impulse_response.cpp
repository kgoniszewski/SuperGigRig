#include "impulse_response.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "blocks/dsp_math.hpp"

namespace gigrig {

namespace {

// Zeroth-order modified Bessel function, for the Kaiser window.
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 50; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1.0e-12 * sum) {
            break;
        }
    }
    return sum;
}

constexpr double kKaiserBeta = 9.0;     // about 90 dB stopband
constexpr double kHalfWidthZeros = 32;  // kernel half width, in zero crossings of the lower rate
constexpr double kCutoff = 0.95;        // of the lower Nyquist, so the transition band stays above it
constexpr double kFadeSeconds = 0.005;
constexpr double kTailEnergy = 1.0e-10;  // -100 dB of the total energy may be dropped from the tail

}  // namespace

SourceIr makeSourceIr(const float* interleaved, int frames, int channels, double sampleRate) {
    SourceIr ir;
    if (interleaved == nullptr || frames <= 0 || channels <= 0 || !(sampleRate >= 8000.0) ||
        !(sampleRate <= 384000.0)) {
        return ir;
    }
    // Only as much as can survive the 200 ms cut is kept, plus room for the resampling kernel.
    const int keep = std::min(frames, static_cast<int>(std::ceil(kMaxIrSeconds * sampleRate)) + 256);
    ir.samples.resize(static_cast<std::size_t>(keep));
    const auto ch = static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < ir.samples.size(); ++i) {
        double sum = 0.0;
        for (std::size_t c = 0; c < ch; ++c) {
            const float v = interleaved[i * ch + c];
            if (!std::isfinite(v)) {
                return SourceIr{};
            }
            sum += static_cast<double>(v);
        }
        ir.samples[i] = static_cast<float>(sum / static_cast<double>(channels));
    }
    ir.sampleRate = sampleRate;
    return ir;
}

std::vector<float> resampleIr(const std::vector<float>& ir, double fromRate, double toRate) {
    if (ir.empty() || std::fabs(fromRate - toRate) <= 1.0e-9 * toRate) {
        return ir;
    }
    const double ratio = toRate / fromRate;
    const double cutoff = kCutoff * std::min(1.0, ratio);  // relative to the source Nyquist
    const double halfWidth = kHalfWidthZeros / cutoff;     // in source samples
    // An impulse response resampled must keep its frequency response, not its
    // sample values: when the rate doubles, each sample carries half the weight.
    const double gain = cutoff / ratio;
    const double i0Beta = besselI0(kKaiserBeta);
    const auto outLength = static_cast<std::size_t>(std::ceil(static_cast<double>(ir.size()) * ratio));
    std::vector<float> out(outLength, 0.0f);
    const auto last = static_cast<long>(ir.size()) - 1;
    for (std::size_t n = 0; n < outLength; ++n) {
        const double t = static_cast<double>(n) / ratio;
        const long k0 = std::max(0L, static_cast<long>(std::ceil(t - halfWidth)));
        const long k1 = std::min(last, static_cast<long>(std::floor(t + halfWidth)));
        double acc = 0.0;
        for (long k = k0; k <= k1; ++k) {
            const double d = t - static_cast<double>(k);
            const double x = cutoff * d;
            const double sinc = std::fabs(x) < 1.0e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double r = d / halfWidth;
            const double w = besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
            acc += static_cast<double>(ir[static_cast<std::size_t>(k)]) * sinc * w;
        }
        out[n] = static_cast<float>(acc * gain);
    }
    return out;
}

double irBandGain(const std::vector<float>& ir, double sampleRate) {
    constexpr int kPoints = 48;
    const double lo = 100.0;
    const double hi = std::min(5000.0, 0.45 * sampleRate);
    double power = 0.0;
    for (int p = 0; p < kPoints; ++p) {
        const double f = lo * std::pow(hi / lo, static_cast<double>(p) / (kPoints - 1));
        const double w = kTwoPi * f / sampleRate;
        double re = 0.0, im = 0.0;
        // A rotating phasor instead of cos and sin per tap; double keeps it accurate over 20k taps.
        const double cr = std::cos(w), ci = -std::sin(w);
        double pr = 1.0, pi = 0.0;
        for (float h : ir) {
            re += static_cast<double>(h) * pr;
            im += static_cast<double>(h) * pi;
            const double nr = pr * cr - pi * ci;
            pi = pr * ci + pi * cr;
            pr = nr;
        }
        power += re * re + im * im;
    }
    return std::sqrt(power / kPoints);
}

std::vector<float> prepareIr(const SourceIr& source, double sampleRate) {
    if (source.samples.empty() || !(source.sampleRate > 0.0) || !(sampleRate > 0.0)) {
        return {};
    }
    std::vector<float> ir = resampleIr(source.samples, source.sampleRate, sampleRate);

    const auto maxLength = static_cast<std::size_t>(std::lround(kMaxIrSeconds * sampleRate));
    if (ir.size() > maxLength) {
        ir.resize(maxLength);
        // A raised-cosine fade where a long IR is cut, so the cut does not ring.
        const auto fade =
            std::min(maxLength, static_cast<std::size_t>(std::lround(kFadeSeconds * sampleRate)));
        for (std::size_t i = 0; i < fade; ++i) {
            const double x = static_cast<double>(i + 1) / static_cast<double>(fade + 1);
            ir[maxLength - 1 - i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * x));
        }
    }

    // Drop the tail that holds less than kTailEnergy of the total: it costs CPU and cannot be heard.
    double total = 0.0;
    for (float h : ir) {
        total += static_cast<double>(h) * static_cast<double>(h);
    }
    if (!(total > 0.0)) {
        return {};
    }
    double tail = 0.0;
    std::size_t length = ir.size();
    while (length > 1) {
        const double h = static_cast<double>(ir[length - 1]);
        if (tail + h * h > kTailEnergy * total) {
            break;
        }
        tail += h * h;
        --length;
    }
    ir.resize(length);

    const double band = irBandGain(ir, sampleRate);
    if (!(band > 1.0e-9)) {
        return {};
    }
    const auto scale = static_cast<float>(1.0 / band);
    for (float& h : ir) {
        h *= scale;
    }
    return ir;
}

}  // namespace gigrig
