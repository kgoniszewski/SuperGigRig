// cab_model.hpp - the original speaker-and-mic model that generates the factory
// cabinet IRs in irs/ (work package 3).
//
// The factory IRs are not recordings: Krzysztof chose (2026-11-23) to bundle
// IRs we generate ourselves, so there is no third-party licence to track. Each
// IR is a chain of minimum-phase filters that stand for what a close-miked
// guitar speaker does to the signal:
//
//   cone resonance     a resonant second-order highpass at the speaker's in-box resonance
//   rear wave          open-back cabinets only: the back of the cone, delayed and low-passed,
//                      partly cancelling the front at low frequencies
//   voicing            peaks and dips of the cone's breakup region, 500 Hz to 5 kHz
//   breakup roll-off   fourth-order lowpass where the cone stops moving as one piece
//   texture            a few narrow, small peaks and dips that make it less sterile
//   mic                proximity bass and a dynamic mic's presence; cap is brighter than edge
//   floor              one quiet, dull reflection off the floor
//
// All filters are minimum phase, so the IR's energy starts at sample 0 and the
// cabinet adds no delay. Deterministic: the same model always gives the same bits.
#pragma once

#include <string>
#include <vector>

namespace gigrig::tools {

struct EqBand {
    double freq;
    double q;
    double gainDb;
};

struct CabModel {
    std::string name;    // file name stem, generic (no trademarks)
    double resonanceHz;  // in-box cone resonance
    double resonanceQ;
    double rearDelayMs;  // 0 for a closed back
    double rearGain;
    std::vector<EqBand> voicing;
    double rolloffHz;
    unsigned textureSeed;
};

struct MicPosition {
    std::string name;  // "cap" or "edge"
    double rolloffScale;
    double proximityDb;
    EqBand presence;
    double highShelfDb;  // at 3 kHz
};

// The three factory cabinets and two mic positions.
const std::vector<CabModel>& factoryCabinets();
const std::vector<MicPosition>& factoryMics();

inline constexpr double kIrSampleRate = 48000.0;
inline constexpr int kIrLength = 4096;  // 85 ms at 48 kHz: the tail is far below -100 dB by then

// The IR of one cabinet and mic, at kIrSampleRate, peak normalised to 0.9.
std::vector<float> generateCabIr(const CabModel& cab, const MicPosition& mic);

}  // namespace gigrig::tools
