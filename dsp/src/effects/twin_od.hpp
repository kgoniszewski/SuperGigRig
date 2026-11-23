// twin_od.hpp - Twin OD, the dual-mode overdrive of the Pre FX section (plan
// section 4, row 4).
//
// Mid mode follows the classic green mid-hump overdrive: an op-amp stage with
// silicon diodes in its feedback loop. Only frequencies above the feedback
// network's corner (about 720 Hz) get the stage's gain, and the diodes limit
// that gained part, so the output is the clean input plus a clipped, bass-cut
// copy: the mid hump and the tight low end come from that one structure.
// Amp mode is flatter and softer, closer to a cranked clean amp: full-range
// drive into a soft clipper after only a small bass cut. Tone is a swept
// high cut and Level sets the output.
#pragma once

#include <vector>

#include "blocks/biquad.hpp"
#include "blocks/oversampler.hpp"
#include "blocks/waveshaper.hpp"
#include "effect_common.hpp"

namespace gigrig {

class TwinOd {
public:
    enum Mode { Mid = 0, Amp = 1 };

    struct Settings {
        bool on = false;
        float gain = 4.0f;  // 0 to 10
        int mode = Mid;
        float levelDb = 0.0f;
        float tone = 5.0f;  // 0 to 10
    };

    static constexpr double kMidCornerHz = 720.0;
    static constexpr double kAmpCornerHz = 120.0;
    static constexpr double kMidClipLevel = 0.25;  // forward voltage of the feedback diodes
    static constexpr double kAmpClipLevel = 0.45;

    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* io, int n, const Settings& s) noexcept;

    // Gain knob to stage gain in each mode.
    static double midDrive(float gainKnob) noexcept;
    static double ampDrive(float gainKnob) noexcept;
    // Tone knob to the high-cut corner in Hz.
    static double toneCutoff(float toneKnob) noexcept;

private:
    void resetState() noexcept;
    void resetMid() noexcept;
    void resetAmp() noexcept;
    void computeWet(const float* in, float* wet, int n) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    BypassFade fade_;
    LinearSmoother gain_;
    LinearSmoother level_;
    OnePoleSmoother tone_;
    LinearSmoother modeMix_;  // 0 mid, 1 amp
    ControlClock clock_;
    float appliedTone_ = 0.0f;
    bool midRunning_ = false;
    bool ampRunning_ = false;
    Oversampler os_;
    Biquad inputHighPass_;
    Biquad midHighPass_;  // feedback network corner, at the high rate
    Biquad midLowPass_;   // feedback capacitor's high cut, at the high rate
    Biquad ampHighPass_;
    Biquad ampLowPass_;
    Biquad toneFilter_;
    AdaaWaveshaper<shapes::Tanh> midClipper_;
    AdaaWaveshaper<shapes::SoftSqrt> ampClipper_;
    std::vector<float> gainBuffer_;
    std::vector<float> modeBuffer_;
    std::vector<float> wet_;
};

}  // namespace gigrig
