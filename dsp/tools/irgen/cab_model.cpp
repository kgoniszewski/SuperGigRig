#include "cab_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "blocks/biquad.hpp"

namespace gigrig::tools {

namespace {

// Double-precision biquad, so the generated files do not depend on float rounding order.
struct Filter {
    BiquadCoefficients c;
    double z1 = 0.0, z2 = 0.0;

    double process(double x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

void filterWith(std::vector<double>& x, const BiquadCoefficients& c) {
    Filter f{c};
    for (double& v : x) {
        v = f.process(v);
    }
}

// Adds a delayed, low-passed copy (gain may be negative).
void addReflection(std::vector<double>& x, double delayMs, double gain, double lowpassHz) {
    const auto d = static_cast<std::size_t>(std::lround(delayMs * 0.001 * kIrSampleRate));
    if (d == 0 || gain == 0.0) {
        return;
    }
    std::vector<double> copy(x.size(), 0.0);
    for (std::size_t i = d; i < x.size(); ++i) {
        copy[i] = x[i - d];
    }
    filterWith(copy, BiquadCoefficients::lowPass(kIrSampleRate, lowpassHz, 0.7071));
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += gain * copy[i];
    }
}

}  // namespace

const std::vector<CabModel>& factoryCabinets() {
    static const std::vector<CabModel> cabs = {
        // Open-back combo with one 15-inch speaker: deep, round, soft top end (for Combo 40).
        {"combo-1x15-open",
         72.0,
         1.4,
         1.3,
         -0.35,
         {{600.0, 1.0, 2.0}, {1300.0, 2.0, -3.0}, {2200.0, 2.0, 4.0}, {3300.0, 3.0, 3.0}},
         4200.0,
         15u},
        // Open-back 2x12: tighter lows, a lifted upper midrange, sparkle (for Steel 100).
        {"open-2x12",
         90.0,
         1.2,
         1.0,
         -0.30,
         {{1000.0, 1.0, -2.0}, {2600.0, 2.5, 5.0}, {4000.0, 3.0, 2.0}},
         5200.0,
         212u},
        // Closed-back 2x12: boxed low-mid thump, focused mids (for Proto 50).
        {"closed-2x12",
         100.0,
         1.5,
         0.0,
         0.0,
         {{180.0, 1.0, 1.0}, {800.0, 1.0, 1.5}, {2800.0, 2.0, 4.0}, {3600.0, 4.0, -4.0}, {4600.0, 4.0, 2.5}},
         5500.0,
         312u},
    };
    return cabs;
}

const std::vector<MicPosition>& factoryMics() {
    static const std::vector<MicPosition> mics = {
        {"cap", 1.15, 3.0, {5000.0, 1.5, 3.0}, 0.0},
        {"edge", 0.75, 2.0, {4000.0, 1.5, 1.0}, -4.0},
    };
    return mics;
}

std::vector<float> generateCabIr(const CabModel& cab, const MicPosition& mic) {
    const double fs = kIrSampleRate;
    std::vector<double> x(static_cast<std::size_t>(kIrLength), 0.0);
    x[0] = 1.0;

    filterWith(x, BiquadCoefficients::highPass(fs, cab.resonanceHz, cab.resonanceQ));
    addReflection(x, cab.rearDelayMs, cab.rearGain, 800.0);
    for (const EqBand& b : cab.voicing) {
        filterWith(x, BiquadCoefficients::peak(fs, b.freq, b.q, b.gainDb));
    }
    const double rolloff = cab.rolloffHz * mic.rolloffScale;
    filterWith(x, BiquadCoefficients::lowPass(fs, rolloff, 0.54));
    filterWith(x, BiquadCoefficients::lowPass(fs, rolloff, 1.31));

    // Texture: six narrow resonances between 1.5 and 6 kHz, at most 1.5 dB, from a fixed seed.
    std::uint32_t seed = cab.textureSeed;
    auto random = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double>(seed >> 8) / 16777216.0;
    };
    for (int i = 0; i < 6; ++i) {
        const double f = 1500.0 * std::pow(4.0, random());
        const double g = 3.0 * random() - 1.5;
        filterWith(x, BiquadCoefficients::peak(fs, f, 6.0 + 6.0 * random(), g));
    }

    filterWith(x, BiquadCoefficients::lowShelf(fs, 160.0, 0.7071, mic.proximityDb));
    filterWith(x, BiquadCoefficients::peak(fs, mic.presence.freq, mic.presence.q, mic.presence.gainDb));
    if (mic.highShelfDb != 0.0) {
        filterWith(x, BiquadCoefficients::highShelf(fs, 3000.0, 0.7071, mic.highShelfDb));
    }
    addReflection(x, 3.2, 0.08, 2500.0);

    double peak = 0.0;
    for (double v : x) {
        peak = std::max(peak, std::fabs(v));
    }
    std::vector<float> out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        out[i] = static_cast<float>(0.9 * x[i] / peak);
    }
    return out;
}

}  // namespace gigrig::tools
