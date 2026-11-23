#include "amps/steel100.hpp"

#include <algorithm>
#include <cmath>

#include "blocks/dsp_math.hpp"

namespace gigrig::amp {
namespace {

// ---- Preamp. The family's typical values; the supply is higher than on Combo 40, which
// is a large part of the extra clean headroom of these heads. ----

TriodeStageDesign v1Design() noexcept {
    TriodeStageDesign d;
    d.supplyVolts = 330.0;
    d.plateOhms = 100.0e3;
    d.cathodeOhms = 1.5e3;
    d.cathodeFarads = 25.0e-6;
    d.sourceOhms = 68.0e3;
    return d;
}

TriodeStageDesign v2Design() noexcept {
    TriodeStageDesign d = v1Design();
    d.cathodeOhms = 820.0;   // hotter bias: more gain, and it clips later on the negative swing
    d.sourceOhms = 100.0e3;  // fed from the Gain wiper (typical value, see Combo 40)
    return d;
}

constexpr double kTriodeRange = 40.0;
constexpr std::size_t kTriodePoints = 8193;

const WaveshaperTable& v1Table() {
    static const WaveshaperTable table = [] {
        WaveshaperTable t;
        buildTriodeTable(t, v1Design(), kTriodeRange, kTriodePoints);
        return t;
    }();
    return table;
}

const WaveshaperTable& v2Table() {
    static const WaveshaperTable table = [] {
        WaveshaperTable t;
        buildTriodeTable(t, v2Design(), kTriodeRange, kTriodePoints);
        return t;
    }();
    return table;
}

double v1OutputOhms() {
    static const double ohms = solveOperatingPoint(v1Design()).outputOhms;
    return ohms;
}

// Tone stack with a Middle pot. The slightly smaller treble cap than Combo 40 keeps the top
// sweet rather than glassy; the large bass cap is part of the "deep" voicing.
constexpr double kTrebleCap = 220.0e-12;
constexpr double kBassCap = 0.1e-6;
constexpr double kMidCap = 0.047e-6;
constexpr double kSlopeOhms = 100.0e3;
constexpr double kTreblePotOhms = 250.0e3;
constexpr double kBassPotOhms = 250.0e3;
constexpr double kMiddlePotOhms = 25.0e3;  // linear
constexpr double kMidBoostOhms = 33.0e3;   // switched in series with the Middle pot
constexpr double kGainPotOhms = 1.0e6;
constexpr double kBrightCap = 120.0e-12;
constexpr double kV2MillerCap = 120.0e-12;  // hotter V2 stage: a little more Miller capacitance

constexpr double kInputCornerHz = 1.0 / (2.0 * 3.14159265358979 * 68.0e3 * 110.0e-12);

// FET input: voltage gain of the JFET stage, where it starts to clip (volts at its
// output, far above guitar level) and its small treble lift.
constexpr double kFetGain = 2.8;
constexpr double kFetHeadroomVolts = 8.0;
constexpr double kFetShelfHz = 3000.0;
constexpr double kFetShelfDb = 2.0;

constexpr double kCouplingCornerHz = 4.0;
// Fixed deep voicing: low shelf below about 120 Hz.
constexpr double kDeepHz = 120.0;
constexpr double kDeepDb = 3.0;

// V2 plate volts to phase-inverter drive with the Master at full: the main voicing
// constant for where the power section starts to work. The Master is a linear pot here
// (an audio-taper one leaves the default level far below Combo 40's), so the default
// settings of the two amps play at about the same level.
constexpr double kInterstage = 0.05;

// ---- Power section: 100 W, four 6L6-style tubes, solid-state rectifier, strong feedback. ----

PowerAmpDesign powerDesign() noexcept {
    PowerAmpDesign d;
    d.inverterLimit = 1.6;  // big-headroom inverter
    d.classAbBias = 0.14;   // biased warmer than Combo 40: less crossover at low levels
    d.feedback = 0.8;       // about 9 dB of NFB: tight, flat and clean
    d.sagDepth = 0.05;      // silicon diodes: a stiff supply
    d.sagAttackSeconds = 0.01;
    d.sagReleaseSeconds = 0.08;
    d.coreCornerHz = 22.0;  // a big transformer: more low end before it saturates
    d.coreSaturation = 1.0;
    d.transformerTopHz = 15000.0;
    d.presenceMaxCut = 0.5;  // presence is fixed on this head (set in prepare)
    d.presenceHz = 3000.0;
    return d;
}
constexpr double kFixedPresence = 0.4;

// Full power at about -9 dBFS, as Combo 40, so switching amps keeps a similar level.
constexpr double kOutputScale = 0.35;

}  // namespace

void Steel100::configureToneNetwork(ToneNetwork& n, double sampleRate, const Settings& s) noexcept {
    enum Node { Plate, TrebleTop, Wiper, BassTop, Slope, MidTop, Gain };
    double trebleUpper = 0.0, trebleLower = 0.0, gainUpper = 0.0, gainLower = 0.0;
    splitPot(kTreblePotOhms, audioTaperPot(static_cast<double>(s.treble)), trebleUpper, trebleLower);
    splitPot(kGainPotOhms, audioTaperPot(static_cast<double>(s.gain)), gainUpper, gainLower);
    const double bassOhms = std::max(kMinPotOhms, audioTaperPot(static_cast<double>(s.bass)) * kBassPotOhms);
    const double midOhms = std::max(kMinPotOhms, linearPot(static_cast<double>(s.middle)) * kMiddlePotOhms) +
                           (s.midBoost ? kMidBoostOhms : 0.0);

    n.setSampleRate(sampleRate);
    n.setSource(Plate, v1OutputOhms());
    n.setOutput(Gain);
    n.setResistor(0, TrebleTop, Wiper, trebleUpper);
    n.setResistor(1, Wiper, BassTop, trebleLower);
    n.setResistor(2, Plate, Slope, kSlopeOhms);
    n.setResistor(3, BassTop, MidTop, bassOhms);
    n.setResistor(4, MidTop, ToneNetwork::kGround, midOhms);
    n.setResistor(5, Wiper, Gain, gainUpper);
    n.setResistor(6, Gain, ToneNetwork::kGround, gainLower);
    n.setCapacitor(0, Plate, TrebleTop, kTrebleCap);
    n.setCapacitor(1, Slope, BassTop, kBassCap);
    n.setCapacitor(2, Slope, MidTop, kMidCap);
    n.setCapacitor(3, Wiper, Gain, s.bright ? kBrightCap : 0.0);
    n.setCapacitor(4, Gain, ToneNetwork::kGround, kV2MillerCap);
    n.update();
}

void Steel100::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    const int factor = ampOversamplingFactor(sampleRate);
    oversampler_.prepare(factor, maxBlock_);
    highRate_ = sampleRate * factor;

    inputFilter_.setCoefficients(
        BiquadCoefficients::lowPass1(highRate_, std::min(kInputCornerHz, 0.45 * highRate_)));
    fetTone_.setCoefficients(BiquadCoefficients::highShelf1(highRate_, kFetShelfHz, kFetShelfDb));
    v1_.prepare(v1Table(), v1Design(), highRate_);
    v2_.prepare(v2Table(), v2Design(), highRate_);
    coupling_.setCoefficients(BiquadCoefficients::highPass1(highRate_, kCouplingCornerHz));
    deep_.setCoefficients(BiquadCoefficients::lowShelf1(highRate_, kDeepHz, kDeepDb));
    power_.prepare(powerDesign(), highRate_);
    power_.setPresence(kFixedPresence);

    constexpr double knobGlideSeconds = 0.03;
    gain_.prepare(sampleRate, knobGlideSeconds);
    bass_.prepare(sampleRate, knobGlideSeconds);
    middle_.prepare(sampleRate, knobGlideSeconds);
    treble_.prepare(sampleRate, knobGlideSeconds);
    master_.prepare(highRate_, knobGlideSeconds);
    output_.prepare(sampleRate, 0.02);
    reset();
}

void Steel100::reset() noexcept {
    oversampler_.reset();
    inputFilter_.reset();
    fet_.reset();
    fetTone_.reset();
    v1_.reset();
    v2_.reset();
    tone_.reset();
    coupling_.reset();
    deep_.reset();
    power_.reset();
    gain_.snap(settings_.gain);
    bass_.snap(settings_.bass);
    middle_.snap(settings_.middle);
    treble_.snap(settings_.treble);
    master_.snap(static_cast<float>(linearPot(static_cast<double>(settings_.master))));
    outputDbTarget_ = settings_.outputDb;
    output_.snap(static_cast<float>(dbToGain(static_cast<double>(outputDbTarget_))));
    applied_ = settings_;
    configureToneNetwork(tone_, highRate_, applied_);
    controlPhase_ = 0;
}

void Steel100::setSettings(const Settings& s) noexcept {
    if (s.master != settings_.master) {
        master_.setTarget(static_cast<float>(linearPot(static_cast<double>(s.master))));
    }
    settings_ = s;
    gain_.setTarget(s.gain);
    bass_.setTarget(s.bass);
    middle_.setTarget(s.middle);
    treble_.setTarget(s.treble);
    if (s.outputDb != outputDbTarget_) {
        outputDbTarget_ = s.outputDb;
        output_.setTarget(static_cast<float>(dbToGain(static_cast<double>(s.outputDb))));
    }
}

void Steel100::updateControls() noexcept {
    // Knob glides advance in steps of kControlInterval samples, counted from prepare,
    // so coefficient updates fall on the same samples whatever the block size.
    for (OnePoleSmoother* s : {&gain_, &bass_, &middle_, &treble_}) {
        if (s->isSmoothing()) {
            for (int i = 0; i < kControlInterval; ++i) {
                s->next();
            }
        }
    }
    Settings now = settings_;
    now.gain = gain_.current();
    now.bass = bass_.current();
    now.middle = middle_.current();
    now.treble = treble_.current();
    if (now.gain != applied_.gain || now.bass != applied_.bass || now.middle != applied_.middle ||
        now.treble != applied_.treble || now.bright != applied_.bright || now.midBoost != applied_.midBoost) {
        applied_ = now;
        configureToneNetwork(tone_, highRate_, applied_);
    }
    applied_.fetInput = settings_.fetInput;
}

void Steel100::processOversampled(float* hi, int n) noexcept {
    const bool fet = applied_.fetInput;
    constexpr auto fetIn = static_cast<float>(kFetGain / kFetHeadroomVolts);
    constexpr auto fetOut = static_cast<float>(kFetHeadroomVolts);
    for (int i = 0; i < n; ++i) {
        float grid1 = inputFilter_.process(hi[i]);
        if (fet) {
            grid1 = fetTone_.process(fetOut * fet_.process(fetIn * grid1));
        }
        const float plate1 = v1_.process(grid1);
        const auto grid2 = static_cast<float>(tone_.process(static_cast<double>(plate1)));
        const float plate2 = v2_.process(grid2);
        const float pre = deep_.process(coupling_.process(plate2));
        const float drive = static_cast<float>(kInterstage) * master_.next() * pre;
        hi[i] = power_.process(drive);
    }
}

void Steel100::process(float* data, int numSamples) noexcept {
    const int total = std::min(numSamples, maxBlock_);
    int done = 0;
    while (done < total) {
        if (controlPhase_ == 0) {
            updateControls();
        }
        const int chunk = std::min(total - done, kControlInterval - controlPhase_);
        float* block = data + done;
        for (int i = 0; i < chunk; ++i) {
            block[i] *= static_cast<float>(kInputVoltsAtFullScale);
        }
        float* hi = oversampler_.upsample(block, chunk);
        processOversampled(hi, chunk * oversampler_.factor());
        oversampler_.downsample(block, chunk);
        for (int i = 0; i < chunk; ++i) {
            block[i] *= static_cast<float>(kOutputScale) * output_.next();
        }
        controlPhase_ = (controlPhase_ + chunk) % kControlInterval;
        done += chunk;
    }
}

}  // namespace gigrig::amp
