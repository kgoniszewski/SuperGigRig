// amp_section.hpp - the Amp section of the chain (plan section 4, row 8): the amp models,
// the choice between them (amp.model) and the amp bypass (amp.on).
//
// Only the selected amp runs. Changing the model crossfades from the old amp to the new
// one over kCrossfadeSeconds, the only time two amps run at once; the incoming amp starts
// from rest, as a freshly switched-on amp would. A bypassed amp keeps no stale state:
// switching it back on starts it from rest.
#pragma once

#include <vector>

#include "amps/combo40.hpp"
#include "amps/steel100.hpp"
#include "parameters.hpp"

namespace gigrig::amp {

class AmpSection {
public:
    enum Model { kCombo40 = 0, kSteel100 = 1, kModelCount = 2 };

    static constexpr double kCrossfadeSeconds = 0.02;

    // Not real-time safe.
    void prepare(double sampleRate, int maxBlockSize, const ParameterValues& params);
    // Real-time safe. Returns every amp to rest and takes the parameters as they are, with
    // no crossfade.
    void reset(const ParameterValues& params) noexcept;

    // Real-time safe: reads the amp parameters once per block. Call before process.
    void readParameters(const ParameterValues& params) noexcept;

    // Real-time safe, in place, any block size up to the prepared maximum.
    void process(float* data, int numSamples) noexcept;

    bool isOn() const noexcept { return on_; }
    Model model() const noexcept { return model_; }

private:
    void processModel(Model m, float* data, int numSamples) noexcept;
    void resetModel(Model m) noexcept;
    static Model modelFrom(const ParameterValues& params) noexcept;

    Combo40 combo40_;
    Steel100 steel100_;
    std::vector<float> scratch_;
    Model model_ = kCombo40;
    Model fadingOut_ = kCombo40;
    int fadeRemaining_ = 0;  // samples left in the model crossfade; 0 = no crossfade
    int fadeLength_ = 1;
    bool on_ = true;
};

}  // namespace gigrig::amp
