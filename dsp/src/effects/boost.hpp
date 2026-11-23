// boost.hpp - Boost, the first pedal of the Pre FX section (plan section 4, row 1).
//
// Clean mode is a high-headroom gain stage: an op-amp style boost that stays
// linear for normal guitar levels and only rounds off far above them, so it
// pushes the next pedal or the amp without colouring the tone. Overdrive mode
// drives an asymmetric MOSFET-style soft clipper harder, which adds the even
// harmonics and the slight compression of a clipping boost.
#pragma once

#include <vector>

#include "blocks/biquad.hpp"
#include "blocks/oversampler.hpp"
#include "blocks/waveshaper.hpp"
#include "effect_common.hpp"

namespace gigrig {

class Boost {
public:
    enum Mode { Clean = 0, Overdrive = 1 };

    struct Settings {
        bool on = false;
        int mode = Clean;
        float volumeDb = 6.0f;
    };

    // Not real-time safe.
    // Settings that prepare and reset start from (no fade, no glide).
    void setSettings(const Settings& s) noexcept { settings_ = s; }

    // Not real-time safe. Ends in reset().
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // In place, any n from 1 to the prepared maximum.
    void process(float* io, int n, const Settings& s) noexcept;

    // Clean-mode clipping level and overdrive-mode clipping level (full scale = 1).
    static constexpr double kCleanHeadroom = 2.0;
    static constexpr double kOverdriveLevel = 0.45;
    // Extra drive of overdrive mode over the Volume setting.
    static constexpr double kOverdriveDrive = 4.0;

private:
    void resetState() noexcept;
    void computeWet(const float* in, float* wet, int n) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_;
    BypassFade fade_;
    LinearSmoother gain_;
    LinearSmoother modeMix_;  // 0 clean, 1 overdrive; crossfades when the mode switch moves
    Oversampler os_;
    Biquad inputHighPass_;  // input coupling capacitor
    Biquad odHighCut_;      // overdrive mode: the clipper's high cut, at the high rate
    Biquad dcBlock_;        // removes the DC that asymmetric clipping makes
    WaveshaperTable odCurve_;
    AdaaWaveshaper<shapes::SoftSqrt> cleanShaper_;
    AdaaWaveshaper<shapes::Table> odShaper_;
    bool cleanRunning_ = false;
    bool odRunning_ = false;
    std::vector<float> gainBuffer_;
    std::vector<float> modeBuffer_;
    std::vector<float> wet_;
};

}  // namespace gigrig
