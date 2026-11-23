// power_amp.hpp - the push-pull power section shared by the amp models (plan section 5,
// steps 4 to 6): phase inverter, output pair in class AB, supply sag, output transformer
// and the global negative feedback loop, with an optional presence control.
//
// Each amp gives its own design: how hard the inverter clips, the output tubes' idle
// bias, how much feedback, how far the supply sags (tube rectifier against solid state)
// and the transformer's band limits. Signal in phase-inverter grid volts; the output is
// normalised so 1.0 is full power.
#pragma once

#include "blocks/biquad.hpp"
#include "blocks/waveshaper.hpp"

namespace gigrig::amp {

struct PowerAmpDesign {
    double inverterLimit = 1.4;  // phase inverter clips a little above full-power drive
    double classAbBias = 0.12;   // idle conduction of each output tube, in drive units
    double feedback = 0.45;      // fraction of the output fed back to the inverter
    double sagDepth = 0.16;      // supply drop at full power (tube rectifier ~0.15, diodes ~0.05)
    double sagAttackSeconds = 0.025;
    double sagReleaseSeconds = 0.18;
    double coreCornerHz = 30.0;   // primary inductance high-pass at rest
    double coreSaturation = 1.5;  // how fast that corner rises with core flux
    double transformerTopHz = 14000.0;
    // Presence: a high-pass in the feedback path takes this much of the feedback away above
    // presenceHz at full presence, so the top end gets less feedback and rises. 0 = none.
    double presenceMaxCut = 0.0;
    double presenceHz = 2500.0;
};

class PushPullPowerAmp {
public:
    // Not real-time safe: builds the inverter and output-tube curves for the design.
    void prepare(const PowerAmpDesign& design, double sampleRate);
    void reset() noexcept;

    // Real-time safe. 0 to 1; only does something when the design has a presence range.
    void setPresence(double amount) noexcept { presence_ = amount * design_.presenceMaxCut; }

    float process(float input) noexcept;

    float supply() const noexcept { return static_cast<float>(supply_); }

private:
    PowerAmpDesign design_;
    WaveshaperTable inverterTable_;
    WaveshaperTable outputPairTable_;
    AdaaWaveshaper<shapes::Table> inverter_;
    AdaaWaveshaper<shapes::Table> outputPair_;
    Biquad transformerLowPass_;
    double sagAttack_ = 0.0;
    double sagRelease_ = 0.0;
    double coreBase_ = 0.0;
    double fluxScale_ = 0.0;
    double fluxLeak_ = 0.0;
    double presenceCoefficient_ = 0.0;
    double presence_ = 0.0;
    double supplyEnvelope_ = 0.0;
    double supply_ = 1.0;
    double coreLow_ = 0.0;
    double flux_ = 0.0;
    double feedback_ = 0.0;
    double feedbackLow_ = 0.0;
};

}  // namespace gigrig::amp
