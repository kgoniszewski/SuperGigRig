// wav.hpp - minimal WAV reading and writing for the render tool and tests.
// Not part of the real-time core; it allocates and reports errors as strings.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gigrig::tools {

struct WavData {
    double sampleRate = 0.0;
    int channels = 0;
    std::vector<float> samples;  // interleaved, -1 to 1

    std::size_t frames() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    // One channel (0-based) as a mono signal.
    std::vector<float> channel(int index) const;
};

enum class WavFormat { Float32, Pcm16, Pcm24 };

// Reads 16, 24 or 32 bit PCM and 32 or 64 bit float WAV files (plain or extensible).
// Returns false and sets error on failure.
bool readWav(const std::string& path, WavData& out, std::string& error);

// Writes mono or interleaved samples. PCM formats clip at full scale.
bool writeWav(const std::string& path, const WavData& data, WavFormat format, std::string& error);

}  // namespace gigrig::tools
