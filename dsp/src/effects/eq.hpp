// eq.hpp - EQ, the four-band semi-parametric equaliser of the EQ and Comp
// section (plan section 4, row 10). The compressor of that section is not in
// version 1 (CLAUDE.md).
//
// Second-order high and low pass, a low shelf, two peaking bands and a high
// shelf, each with its own frequency and gain (plus or minus 12 dB). The
// peaking bands have a fixed, musical bandwidth, as on a semi-parametric
// console EQ. The filters are linear and add no latency.
//
// High pass at its lowest setting and low pass at its highest are "off": the
// filter is then bypassed, so a default EQ is exactly flat.
#pragma once

#include <array>
#include <complex>
#include <vector>

#include "blocks/biquad.hpp"
#include "effect_common.hpp"
#include "parameters.hpp"

namespace gigrig {

class Eq {
public:
    static constexpr int kBands = 4;

    struct Settings {
        bool on = false;
        float highPassHz = 20.0f;
        float lowPassHz = 20000.0f;
        std::array<float, kBands> frequencyHz{100.0f, 500.0f, 2000.0f, 5000.0f};
        std::array<float, kBands> gainDb{0.0f, 0.0f, 0.0f, 0.0f};
    };

    static constexpr float kHighPassOffHz = 20.0f;
    static constexpr float kLowPassOffHz = 20000.0f;
    static constexpr double kPeakQ = 0.9;
    static constexpr double kShelfQ = 0.7071;
    static constexpr double kFilterQ = 0.7071;  // Butterworth

    // Reads the EQ parameters (ids 40 to 50) from the flat parameter array.
    static Settings fromParameters(const ParameterValues& p) noexcept;

    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Design response of the whole EQ (all six sections) for these settings.
    static std::complex<double> designResponse(const Settings& s, double frequencyHz, double sampleRate);

private:
    // Index of each section in the filter array, in signal order.
    enum Section { HighPass = 0, LowShelf, Peak1, Peak2, HighShelf, LowPass, kSections };

    static BiquadCoefficients sectionCoefficients(int section, double sampleRate, double freq,
                                                  double gainDb) noexcept;
    void updateSection(int section) noexcept;
    void resetState() noexcept;
    void computeWet(const float* in, float* wet, int n) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    BypassFade fade_;
    ControlClock clock_;
    // Per section: frequency (one-pole, heard on a log scale) and gain in dB (linear ramp).
    std::array<OnePoleSmoother, kSections> freq_;
    std::array<LinearSmoother, kSections> gain_;
    std::array<float, kSections> appliedFreq_{};
    std::array<float, kSections> appliedGain_{};
    std::array<Biquad, kSections> filters_;
    std::vector<float> wet_;
};

}  // namespace gigrig
