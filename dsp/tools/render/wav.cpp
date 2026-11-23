#include "wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gigrig::tools {
namespace {

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t le16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

void put16(std::vector<unsigned char>& b, std::uint32_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xff));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
}

void put32(std::vector<unsigned char>& b, std::uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

void putTag(std::vector<unsigned char>& b, const char* tag) {
    b.insert(b.end(), tag, tag + 4);
}

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xfffe;

}  // namespace

std::vector<float> WavData::channel(int index) const {
    std::vector<float> mono(frames());
    if (index < 0 || index >= channels) {
        return mono;
    }
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = samples[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(index)];
    }
    return mono;
}

bool readWav(const std::string& path, WavData& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = path + " is not a RIFF/WAVE file";
        return false;
    }

    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* chunk = bytes.data() + pos;
        const std::size_t size = le32(chunk + 4);
        const std::size_t body = pos + 8;
        const std::size_t available = std::min(size, bytes.size() - body);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = le32(chunk + 12);
            bits = le16(chunk + 22);
            if (format == kFormatExtensible && available >= 26) {
                format = le16(chunk + 8 + 24);  // first two bytes of the sub-format GUID
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = available;  // tolerate truncated files and 0xffffffff streaming sizes
        }
        pos = body + size + (size & 1u);  // chunks are padded to even length
    }
    if (channels == 0 || rate == 0 || data == nullptr) {
        error = path + ": missing fmt or data chunk";
        return false;
    }
    const bool isFloat = format == kFormatFloat && (bits == 32 || bits == 64);
    const bool isPcm = format == kFormatPcm && (bits == 16 || bits == 24 || bits == 32);
    if (!isFloat && !isPcm) {
        error = path + ": unsupported sample format (" + std::to_string(format) + ", " +
                std::to_string(bits) + " bit)";
        return false;
    }

    const std::size_t bytesPerSample = bits / 8u;
    const std::size_t count = dataSize / bytesPerSample / channels * channels;
    out.sampleRate = rate;
    out.channels = channels;
    out.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char* p = data + i * bytesPerSample;
        float v = 0.0f;
        if (isFloat && bits == 32) {
            const std::uint32_t u = le32(p);
            std::memcpy(&v, &u, 4);
        } else if (isFloat) {
            const std::uint64_t u = le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32);
            double d = 0.0;
            std::memcpy(&d, &u, 8);
            v = static_cast<float>(d);
        } else if (bits == 16) {
            v = static_cast<float>(static_cast<std::int16_t>(le16(p))) / 32768.0f;
        } else if (bits == 24) {
            // Shift into the top of an int32 to sign-extend, then scale.
            const std::uint32_t u = (static_cast<std::uint32_t>(p[0]) << 8) |
                                    (static_cast<std::uint32_t>(p[1]) << 16) |
                                    (static_cast<std::uint32_t>(p[2]) << 24);
            v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(u)) / 2147483648.0);
        } else {
            v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(le32(p))) / 2147483648.0);
        }
        out.samples[i] = v;
    }
    return true;
}

bool writeWav(const std::string& path, const WavData& data, WavFormat format, std::string& error) {
    if (data.channels <= 0 || !(data.sampleRate > 0.0)) {
        error = "invalid channel count or sample rate";
        return false;
    }
    const std::uint32_t bits = format == WavFormat::Pcm16 ? 16 : (format == WavFormat::Pcm24 ? 24 : 32);
    const std::uint32_t bytesPerSample = bits / 8;
    const auto channels = static_cast<std::uint32_t>(data.channels);
    const auto rate = static_cast<std::uint32_t>(std::lround(data.sampleRate));
    const auto dataBytes = static_cast<std::uint32_t>(data.samples.size() * bytesPerSample);

    std::vector<unsigned char> b;
    b.reserve(44 + dataBytes + 1);
    putTag(b, "RIFF");
    put32(b, 36 + dataBytes + (dataBytes & 1u));
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, format == WavFormat::Float32 ? kFormatFloat : kFormatPcm);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * bytesPerSample);
    put16(b, channels * bytesPerSample);
    put16(b, bits);
    putTag(b, "data");
    put32(b, dataBytes);
    for (float s : data.samples) {
        if (format == WavFormat::Float32) {
            std::uint32_t u = 0;
            std::memcpy(&u, &s, 4);
            put32(b, u);
        } else {
            // Same scale as the reader (2^(bits-1)), so a round trip is exact to half a step.
            const double full = format == WavFormat::Pcm16 ? 32768.0 : 8388608.0;
            const double scaled = std::round(static_cast<double>(s) * full);
            const auto v = static_cast<std::int32_t>(std::clamp(scaled, -full, full - 1.0));
            const auto u = static_cast<std::uint32_t>(v);
            b.push_back(static_cast<unsigned char>(u & 0xff));
            b.push_back(static_cast<unsigned char>((u >> 8) & 0xff));
            if (format == WavFormat::Pcm24) {
                b.push_back(static_cast<unsigned char>((u >> 16) & 0xff));
            }
        }
    }
    if (dataBytes & 1u) {
        b.push_back(0);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot write " + path;
        return false;
    }
    file.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!file) {
        error = "write failed for " + path;
        return false;
    }
    return true;
}

}  // namespace gigrig::tools
