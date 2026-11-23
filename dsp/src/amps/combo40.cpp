#include "amps/combo40.hpp"

#include <algorithm>
#include <cmath>

namespace gigrig::amp {
namespace {

// ---- Preamp component values (AB763-family vibrato channel). ----

TriodeStageDesign v1Design() noexcept {
    TriodeStageDesign d;
    d.supplyVolts = 300.0;
    d.plateOhms = 100.0e3;
    d.cathodeOhms = 1.5e3;
    d.cathodeFarads = 25.0e-6;
    d.sourceOhms = 68.0e3;  // grid stopper; the guitar's own impedance adds to it in reality
    return d;
}

TriodeStageDesign v2Design() noexcept {
    TriodeStageDesign d = v1Design();
    // The grid is fed from the volume wiper. Its impedance moves with the knob (up to
    // about 250k at mid travel); a fixed typical value keeps the curve static.
    d.sourceOhms = 100.0e3;
    return d;
}

// Grid signal range covered by the triode tables. Beyond it the stage is held in
// cutoff or grid conduction, as it saturates there anyway.
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

constexpr double kTrebleCap = 250.0e-12;
constexpr double kBassCap = 0.1e-6;
constexpr double kMidCap = 0.047e-6;
constexpr double kSlopeOhms = 100.0e3;
constexpr double kTreblePotOhms = 250.0e3;
constexpr double kBassPotOhms = 250.0e3;
constexpr double kMidOhms = 6.8e3;  // the vibrato channel has no mid pot
constexpr double kVolumePotOhms = 1.0e6;
constexpr double kBrightCap = 120.0e-12;
constexpr double kV2MillerCap = 100.0e-12;  // Cgk + (1 + gain) Cgp of the second 12AX7 stage

// V1 grid: 68k stopper into Cgk + (1 + gain) Cgp, about 110 pF.
constexpr double kInputCornerHz = 1.0 / (2.0 * 3.14159265358979 * 68.0e3 * 110.0e-12);

// After V2: 0.047 uF coupling cap into the mixing network, then the level loss of
// that network. The reverb mixer and its recovery stage are left out (plan: two
// triode stages), so this one factor sets where the power section starts to clip
// and is the main voicing constant for "edge of breakup". Units: V2 plate volts to
// phase-inverter drive, where about 1.0 reaches full power.
constexpr double kCouplingCornerHz = 3.4;
constexpr double kInterstage = 0.055;

// ---- Power section: 40 W, two 6L6-style tubes, tube rectifier, moderate feedback. ----

PowerAmpDesign powerDesign() noexcept {
    PowerAmpDesign d;
    d.inverterLimit = 1.4;
    d.classAbBias = 0.12;
    d.feedback = 0.45;  // about 5 dB of NFB
    d.sagDepth = 0.16;  // tube rectifier: the supply drops ~16% at full power
    d.sagAttackSeconds = 0.025;
    d.sagReleaseSeconds = 0.18;
    d.coreCornerHz = 30.0;
    d.coreSaturation = 1.5;
    d.transformerTopHz = 14000.0;
    return d;
}

constexpr double kOutputScale =
    0.35;  // full power at about -9 dBFS: transients above it stay clear of 0 dBFS

}  // namespace

// ---------------------------------------------------------------- amp

int Combo40::factorForRate(double sampleRate) noexcept {
    return ampOversamplingFactor(sampleRate);
}

double Combo40::audioTaper(double knob) noexcept {
    return audioTaperPot(knob);
}

void Combo40::configureToneNetwork(ToneNetwork& n, double sampleRate, const Settings& s) noexcept {
    enum Node { Plate, TrebleTop, Wiper, BassTop, Slope, MidTop, Volume };
    double trebleUpper = 0.0, trebleLower = 0.0, volumeUpper = 0.0, volumeLower = 0.0;
    splitPot(kTreblePotOhms, audioTaper(static_cast<double>(s.treble)), trebleUpper, trebleLower);
    splitPot(kVolumePotOhms, audioTaper(static_cast<double>(s.volume)), volumeUpper, volumeLower);
    const double bassOhms = std::max(kMinPotOhms, audioTaper(static_cast<double>(s.bass)) * kBassPotOhms);

    n.setSampleRate(sampleRate);
    n.setSource(Plate, v1OutputOhms());
    n.setOutput(Volume);
    n.setResistor(0, TrebleTop, Wiper, trebleUpper);
    n.setResistor(1, Wiper, BassTop, trebleLower);
    n.setResistor(2, Plate, Slope, kSlopeOhms);
    n.setResistor(3, BassTop, MidTop, bassOhms);
    n.setResistor(4, MidTop, ToneNetwork::kGround, kMidOhms);
    n.setResistor(5, Wiper, Volume, volumeUpper);
    n.setResistor(6, Volume, ToneNetwork::kGround, volumeLower);
    n.setCapacitor(0, Plate, TrebleTop, kTrebleCap);
    n.setCapacitor(1, Slope, BassTop, kBassCap);
    n.setCapacitor(2, Slope, MidTop, kMidCap);
    n.setCapacitor(3, Wiper, Volume, s.bright ? kBrightCap : 0.0);
    n.setCapacitor(4, Volume, ToneNetwork::kGround, kV2MillerCap);
    n.update();
}

void Combo40::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    const int factor = factorForRate(sampleRate);
    oversampler_.prepare(factor, maxBlock_);
    highRate_ = sampleRate * factor;

    inputFilter_.setCoefficients(
        BiquadCoefficients::lowPass1(highRate_, std::min(kInputCornerHz, 0.45 * highRate_)));
    v1_.prepare(v1Table(), v1Design(), highRate_);
    v2_.prepare(v2Table(), v2Design(), highRate_);
    coupling_.setCoefficients(BiquadCoefficients::highPass1(highRate_, kCouplingCornerHz));
    power_.prepare(powerDesign(), highRate_);

    constexpr double knobGlideSeconds = 0.03;
    volume_.prepare(sampleRate, knobGlideSeconds);
    treble_.prepare(sampleRate, knobGlideSeconds);
    bass_.prepare(sampleRate, knobGlideSeconds);
    output_.prepare(sampleRate, 0.02);
    reset();
}

void Combo40::reset() noexcept {
    oversampler_.reset();
    inputFilter_.reset();
    v1_.reset();
    v2_.reset();
    tone_.reset();
    coupling_.reset();
    power_.reset();
    volume_.snap(settings_.volume);
    treble_.snap(settings_.treble);
    bass_.snap(settings_.bass);
    outputDbTarget_ = settings_.outputDb;
    output_.snap(static_cast<float>(dbToGain(static_cast<double>(outputDbTarget_))));
    applied_ = settings_;
    configureToneNetwork(tone_, highRate_, applied_);
    controlPhase_ = 0;
}

void Combo40::setSettings(const Settings& s) noexcept {
    settings_ = s;
    volume_.setTarget(s.volume);
    treble_.setTarget(s.treble);
    bass_.setTarget(s.bass);
    if (s.outputDb != outputDbTarget_) {
        outputDbTarget_ = s.outputDb;
        output_.setTarget(static_cast<float>(dbToGain(static_cast<double>(s.outputDb))));
    }
}

void Combo40::updateToneNetwork() noexcept {
    // Knob glides advance in steps of kControlInterval samples, counted from prepare,
    // so coefficient updates fall on the same samples whatever the block size.
    for (OnePoleSmoother* s : {&volume_, &treble_, &bass_}) {
        if (s->isSmoothing()) {
            for (int i = 0; i < kControlInterval; ++i) {
                s->next();
            }
        }
    }
    Settings now = settings_;
    now.volume = volume_.current();
    now.treble = treble_.current();
    now.bass = bass_.current();
    if (now.volume != applied_.volume || now.treble != applied_.treble || now.bass != applied_.bass ||
        now.bright != applied_.bright) {
        applied_ = now;
        configureToneNetwork(tone_, highRate_, applied_);
    }
}

void Combo40::processOversampled(float* hi, int n) noexcept {
    for (int i = 0; i < n; ++i) {
        const float grid1 = inputFilter_.process(hi[i]);
        const float plate1 = v1_.process(grid1);
        const auto grid2 = static_cast<float>(tone_.process(static_cast<double>(plate1)));
        const float plate2 = v2_.process(grid2);
        const float drive = static_cast<float>(kInterstage) * coupling_.process(plate2);
        hi[i] = power_.process(drive);
    }
}

void Combo40::process(float* data, int numSamples) noexcept {
    const int total = std::min(numSamples, maxBlock_);
    int done = 0;
    while (done < total) {
        if (controlPhase_ == 0) {
            updateToneNetwork();
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
