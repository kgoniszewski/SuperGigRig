// impulse_response.hpp - turns a raw IR file's samples into what the cabinet plays.
//
// Plan section 5, "Cabinet": IRs are converted to mono, resampled to the
// session rate, trimmed to 200 ms and normalised. All of this runs on the
// app's worker queue (or in prepare), never on the audio thread.
#pragma once

#include <vector>

namespace gigrig {

inline constexpr double kMaxIrSeconds = 0.2;

// A loaded IR as it came from the file, mixed to mono, kept so the cabinet can
// rebuild itself when the session sample rate changes.
struct SourceIr {
    std::vector<float> samples;
    double sampleRate = 0.0;
};

// Mixes interleaved frames to mono by averaging the channels. Returns an empty
// IR for invalid arguments (no frames, no channels, a rate outside 8 to 384 kHz,
// or any non-finite sample).
SourceIr makeSourceIr(const float* interleaved, int frames, int channels, double sampleRate);

// Windowed-sinc resampling of an impulse response. The kernel is linear phase,
// but nothing before time 0 is kept: an IR starts with its direct sound, and
// keeping the kernel's pre-ringing would delay the whole cabinet.
std::vector<float> resampleIr(const std::vector<float>& ir, double fromRate, double toRate);

// The full preparation: resample to sampleRate, cut to kMaxIrSeconds (with a
// short fade where a long IR is cut), drop the silent tail, and normalise.
// Leading silence is kept, because it carries the mic distance and so the
// timing between the two mic slots. Returns an empty vector for a silent IR.
std::vector<float> prepareIr(const SourceIr& source, double sampleRate);

// Normalisation keeps cabinets at similar loudness whatever their length or
// sample rate: the magnitude response, averaged in power over log-spaced
// frequencies from 100 Hz to 5 kHz (the guitar band that sets loudness), is
// set to 0 dB. A unit impulse is left unchanged.
double irBandGain(const std::vector<float>& ir, double sampleRate);

}  // namespace gigrig
