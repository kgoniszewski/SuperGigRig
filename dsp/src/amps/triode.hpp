// triode.hpp - a common-cathode triode gain stage for the amp models.
//
// The stage is a grey-box model (plan section 5): a static transfer curve
// from grid to plate, computed once from a triode equation and the stage's
// load line, plus one slow state for cathode bias shift.
//
// Static curve. For each grid signal the plate voltage is solved from the
// plate load line (B+ through the plate resistor) and the Koren triode
// equation, a widely published empirical fit of triode plate curves. Grid
// conduction is folded into the same curve: once the grid goes positive
// against the cathode it draws current through the driving source
// impedance, which clamps the positive grid swing. Together they give the
// asymmetric clipping of a real stage: a soft, rounded cutoff on the
// negative grid swing and a firmer grid-conduction limit on the positive one.
//
// Bias shift. The cathode resistor is bypassed by a large capacitor, so the
// cathode voltage follows the average plate current with a time constant of
// tens of milliseconds. When the stage clips asymmetrically the average
// current moves, the bias moves with it, and the stage compresses and
// "blooms" after a hard pick attack, as a pushed tube stage does.
//
// The curve is used through first-order ADAA and runs at the oversampled rate.
#pragma once

#include "blocks/waveshaper.hpp"

namespace gigrig::amp {

// Koren triode model constants.
struct TriodeTube {
    double mu;
    double ex;
    double kg1;
    double kp;
    double kvb;
};

// The common 12AX7 fit (mu 100).
inline constexpr TriodeTube k12AX7{100.0, 1.4, 1060.0, 600.0, 300.0};

// Plate current in amperes for a plate-to-cathode and grid-to-cathode voltage.
double triodePlateCurrent(const TriodeTube& tube, double vpk, double vgk) noexcept;

struct TriodeStageDesign {
    TriodeTube tube = k12AX7;
    double supplyVolts = 300.0;  // B+ at the top of the plate resistor
    double plateOhms = 100.0e3;
    double cathodeOhms = 1.5e3;
    double cathodeFarads = 25.0e-6;   // bypass capacitor
    double sourceOhms = 68.0e3;       // impedance driving the grid (grid conduction)
    double gridConductance = 5.0e-4;  // grid current = g * vgk^1.5 above 0 V (A / V^1.5)
};

// Quiescent state of a stage and its small-signal behaviour there.
struct TriodeOperatingPoint {
    double plateVolts = 0.0;
    double cathodeVolts = 0.0;
    double plateAmps = 0.0;
    double gain = 0.0;        // plate swing per grid volt, cathode bypassed (negative: inverting)
    double outputOhms = 0.0;  // plate resistance in parallel with the plate resistor
};

TriodeOperatingPoint solveOperatingPoint(const TriodeStageDesign& design) noexcept;

// Fills table with plate voltage change against grid signal (both relative to
// the operating point), over -range .. +range volts of grid signal. Not
// real-time safe.
void buildTriodeTable(WaveshaperTable& table, const TriodeStageDesign& design, double range,
                      std::size_t points);

// One stage at the oversampled rate: signal in grid volts, plate volts out
// (relative to the operating point, inverted as the real stage inverts).
class TriodeStage {
public:
    // table must outlive the stage and be built from the same design.
    void prepare(const WaveshaperTable& table, const TriodeStageDesign& design, double sampleRate) noexcept {
        shaper_.setShape(shapes::Table{&table});
        plateOhms_ = design.plateOhms;
        cathodeOhms_ = design.cathodeOhms;
        shiftCoefficient_ = 1.0 / (design.cathodeFarads * sampleRate);
        reset();
    }

    void reset() noexcept {
        shaper_.reset();
        cathodeShift_ = 0.0;
    }

    float process(float grid) noexcept {
        const float plate = shaper_.process(static_cast<float>(static_cast<double>(grid) - cathodeShift_));
        // Bypass capacitor: C dVk/dt = (Ip - Ip0) - Vk / Rk, with Ip - Ip0 = -plate / Ra.
        cathodeShift_ +=
            shiftCoefficient_ * (-static_cast<double>(plate) / plateOhms_ - cathodeShift_ / cathodeOhms_);
        return plate;
    }

    double cathodeShift() const noexcept { return cathodeShift_; }

private:
    AdaaWaveshaper<shapes::Table> shaper_;
    double plateOhms_ = 100.0e3;
    double cathodeOhms_ = 1.5e3;
    double shiftCoefficient_ = 0.0;
    double cathodeShift_ = 0.0;
};

}  // namespace gigrig::amp
