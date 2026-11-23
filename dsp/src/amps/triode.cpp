#include "amps/triode.hpp"

#include <algorithm>
#include <cmath>

namespace gigrig::amp {
namespace {

// log(1 + e^x) without overflow.
double softplus(double x) noexcept {
    return x > 30.0 ? x : std::log1p(std::exp(x));
}

// Grid current: zero well below 0 V, rising as vgk^1.5 above it, with a
// rounded onset about 50 mV wide (contact potential and electron velocity spread).
double gridCurrent(const TriodeStageDesign& d, double vgk) noexcept {
    constexpr double knee = 0.05;
    const double v = knee * softplus(vgk / knee);
    return d.gridConductance * v * std::sqrt(v);
}

// Bisection on a function that changes sign once on [lo, hi]; f(lo) > 0 > f(hi) or the reverse.
template <class F>
double bisect(F&& f, double lo, double hi) noexcept {
    const bool rising = f(lo) < f(hi);
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        if ((f(mid) < 0.0) == rising) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Plate voltage for a grid-to-cathode voltage on the load line, cathode held at vk.
double plateVolts(const TriodeStageDesign& d, double vgk, double vk) noexcept {
    return bisect(
        [&](double vp) {
            return d.supplyVolts - vp - d.plateOhms * triodePlateCurrent(d.tube, vp - vk, vgk);
        },
        0.0, d.supplyVolts);
}

}  // namespace

double triodePlateCurrent(const TriodeTube& t, double vpk, double vgk) noexcept {
    const double e1 = vpk / t.kp * softplus(t.kp * (1.0 / t.mu + vgk / std::sqrt(t.kvb + vpk * vpk)));
    return e1 > 0.0 ? 2.0 * std::pow(e1, t.ex) / t.kg1 : 0.0;
}

TriodeOperatingPoint solveOperatingPoint(const TriodeStageDesign& d) noexcept {
    // Grid at 0 V DC, cathode at Ip * Rk: find the current that satisfies both.
    const double maxAmps = d.supplyVolts / (d.plateOhms + d.cathodeOhms);
    const double ip = bisect(
        [&](double i) {
            const double vk = i * d.cathodeOhms;
            const double vp = d.supplyVolts - i * d.plateOhms;
            return triodePlateCurrent(d.tube, vp - vk, -vk) - i;
        },
        0.0, maxAmps);
    TriodeOperatingPoint op;
    op.plateAmps = ip;
    op.cathodeVolts = ip * d.cathodeOhms;
    op.plateVolts = d.supplyVolts - ip * d.plateOhms;
    const double vpk = op.plateVolts - op.cathodeVolts;
    const double vgk = -op.cathodeVolts;
    constexpr double h = 1.0e-3;
    const double gm =
        (triodePlateCurrent(d.tube, vpk, vgk + h) - triodePlateCurrent(d.tube, vpk, vgk - h)) / (2.0 * h);
    const double gp =
        (triodePlateCurrent(d.tube, vpk + h, vgk) - triodePlateCurrent(d.tube, vpk - h, vgk)) / (2.0 * h);
    op.outputOhms = 1.0 / (gp + 1.0 / d.plateOhms);
    op.gain = -gm * op.outputOhms;
    return op;
}

void buildTriodeTable(WaveshaperTable& table, const TriodeStageDesign& d, double range, std::size_t points) {
    const TriodeOperatingPoint op = solveOperatingPoint(d);
    const double vk = op.cathodeVolts;
    table.build(
        [&](double signal) {
            // Grid voltage from the source, then the grid-current divider against the source impedance.
            const double driven = signal - vk;
            const double lo = std::min(driven, 0.0) - 1.0;
            const double vgk =
                bisect([&](double u) { return u + d.sourceOhms * gridCurrent(d, u) - driven; }, lo, driven);
            return plateVolts(d, vgk, vk) - op.plateVolts;
        },
        -range, range, points);
}

}  // namespace gigrig::amp
