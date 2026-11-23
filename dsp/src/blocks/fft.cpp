#include "fft.hpp"

#include <cmath>
#include <utility>

#include "dsp_math.hpp"

namespace gigrig {

void RealFft::prepare(int n) {
    n_ = n;
    m_ = n / 2;
    const auto m = static_cast<std::size_t>(m_);

    int bits = 0;
    while ((1 << bits) < m_) {
        ++bits;
    }
    bitReverse_.assign(m, 0);
    for (std::size_t i = 0; i < m; ++i) {
        std::size_t r = 0;
        for (int b = 0; b < bits; ++b) {
            r |= ((i >> b) & 1u) << (bits - 1 - b);
        }
        bitReverse_[i] = r;
    }

    // Twiddles are computed in double and rounded once, so the float FFT error
    // does not grow with the table position.
    twCos_.assign(m / 2 + 1, 0.0f);
    twSin_.assign(m / 2 + 1, 0.0f);
    for (std::size_t t = 0; t <= m / 2; ++t) {
        const double a = kTwoPi * static_cast<double>(t) / static_cast<double>(m_);
        twCos_[t] = static_cast<float>(std::cos(a));
        twSin_[t] = static_cast<float>(std::sin(a));
    }
    packCos_.assign(m + 1, 0.0f);
    packSin_.assign(m + 1, 0.0f);
    for (std::size_t k = 0; k <= m; ++k) {
        const double a = kTwoPi * static_cast<double>(k) / static_cast<double>(n_);
        packCos_[k] = static_cast<float>(std::cos(a));
        packSin_[k] = static_cast<float>(std::sin(a));
    }
    zr_.assign(m, 0.0f);
    zi_.assign(m, 0.0f);
}

void RealFft::complexFft(float sign) noexcept {
    const auto m = static_cast<std::size_t>(m_);
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t j = bitReverse_[i];
        if (i < j) {
            std::swap(zr_[i], zr_[j]);
            std::swap(zi_[i], zi_[j]);
        }
    }
    for (std::size_t len = 2; len <= m; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t step = m / len;
        for (std::size_t i = 0; i < m; i += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const float wr = twCos_[k * step];
                const float wi = sign * twSin_[k * step];
                const std::size_t a = i + k;
                const std::size_t b = a + half;
                const float vr = zr_[b] * wr - zi_[b] * wi;
                const float vi = zr_[b] * wi + zi_[b] * wr;
                zr_[b] = zr_[a] - vr;
                zi_[b] = zi_[a] - vi;
                zr_[a] += vr;
                zi_[a] += vi;
            }
        }
    }
}

void RealFft::forward(const float* in, float* re, float* im) noexcept {
    const auto m = static_cast<std::size_t>(m_);
    for (std::size_t k = 0; k < m; ++k) {
        zr_[k] = in[2 * k];
        zi_[k] = in[2 * k + 1];
    }
    complexFft(-1.0f);
    // Split the packed spectrum into the spectra of the even and odd samples
    // and recombine them into the spectrum of the real input.
    for (std::size_t k = 0; k <= m; ++k) {
        const std::size_t ka = k == m ? 0 : k;
        const std::size_t kb = k == 0 ? 0 : m - k;
        const float ar = zr_[ka], ai = zi_[ka];
        const float br = zr_[kb], bi = -zi_[kb];
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
        const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
        const float c = packCos_[k], s = packSin_[k];
        re[k] = er + c * di - s * dr;
        im[k] = ei - c * dr - s * di;
    }
}

void RealFft::inverse(const float* re, const float* im, float* out) noexcept {
    const auto m = static_cast<std::size_t>(m_);
    for (std::size_t k = 0; k < m; ++k) {
        const float xr = re[k];
        const float xi = k == 0 ? 0.0f : im[k];
        const float yr = re[m - k];
        const float yi = (m - k == m) ? 0.0f : -im[m - k];  // conj(X[m - k]); X[m] is real
        const float pr = xr + yr, pi = xi + yi;
        const float qr = xr - yr, qi = xi - yi;
        const float c = packCos_[k], s = packSin_[k];
        zr_[k] = pr - qr * s - qi * c;
        zi_[k] = pi + qr * c - qi * s;
    }
    complexFft(1.0f);
    for (std::size_t k = 0; k < m; ++k) {
        out[2 * k] = zr_[k];
        out[2 * k + 1] = zi_[k];
    }
}

}  // namespace gigrig
