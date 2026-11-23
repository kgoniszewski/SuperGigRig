// post_fx.hpp - the Post FX section: Digital Delay then Studio Reverb (plan
// section 4, rows 12 and 13), at the end of the chain before the output level.
#pragma once

#include "digital_delay.hpp"
#include "parameters.hpp"
#include "studio_reverb.hpp"

namespace gigrig {

class PostFx {
public:
    // Not real-time safe.
    void prepare(double sampleRate, int maxBlockSize, const ParameterValues& params);
    void reset(const ParameterValues& params) noexcept;

    // Real-time safe; in place; 1 <= n <= prepared maximum.
    void process(float* io, int n, const ParameterValues& params) noexcept;

    DigitalDelay& delay() noexcept { return delay_; }
    StudioReverb& reverb() noexcept { return reverb_; }

private:
    void readSettings(const ParameterValues& p) noexcept;

    DigitalDelay::Settings delaySettings_;
    StudioReverb::Settings reverbSettings_;
    DigitalDelay delay_;
    StudioReverb reverb_;
};

}  // namespace gigrig
