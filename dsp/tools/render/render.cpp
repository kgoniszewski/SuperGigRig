#include "render.hpp"

#include <gigrig/gigrig.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <utility>

#include "wav.hpp"

namespace gigrig::tools {
namespace {

const char* const kUsage =
    "usage: gigrig_render -i input.wav -o output.wav [options]\n"
    "\n"
    "Renders a WAV file through the GigRigDSP core, offline, at the file's sample rate.\n"
    "\n"
    "options:\n"
    "  -i, --input FILE      input WAV (16/24/32 bit PCM or 32/64 bit float)\n"
    "  -o, --output FILE     output WAV, mono\n"
    "  -p, --preset FILE     parameter values, one 'key = value' per line, '#' starts a comment\n"
    "  -s, --set KEY=VALUE   set one parameter (after the preset; may repeat)\n"
    "      --ir1 FILE        cabinet IR for mic 1 (WAV, any rate and channel count)\n"
    "      --ir2 FILE        cabinet IR for mic 2; also switches mic 2 on (a preset may override)\n"
    "  -c, --channel N       input channel to use, from 1 (default 1)\n"
    "  -b, --block N         processing block size (default 64)\n"
    "  -t, --tail SECONDS    silence appended to the input, for decays (default 0)\n"
    "  -f, --format F        output format: f32 (default), s24 or s16\n"
    "  -l, --list-params     print the parameter table and exit\n"
    "  -h, --help            print this help\n";

struct EngineDeleter {
    void operator()(GigRigEngine* e) const { gigrig_destroy(e); }
};

bool parseNumber(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtod(text.c_str(), &end);
    return errno == 0 && end != nullptr && *end == '\0' && std::isfinite(value);
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Parses "key=value" (spaces allowed around '='). Returns false with a message on error.
bool parseAssignment(const std::string& text, std::pair<int, float>& out, std::string& error) {
    const auto eq = text.find('=');
    if (eq == std::string::npos) {
        error = "expected key=value, got '" + text + "'";
        return false;
    }
    const std::string key = trim(text.substr(0, eq));
    const std::string valueText = trim(text.substr(eq + 1));
    const int id = gigrig_param_find(key.c_str());
    if (id < 0) {
        error = "unknown parameter '" + key + "' (see --list-params)";
        return false;
    }
    double value = 0.0;
    if (!parseNumber(valueText, value)) {
        error = "invalid number '" + valueText + "' for " + key;
        return false;
    }
    out = {id, static_cast<float>(value)};
    return true;
}

bool readPreset(const std::string& path, std::vector<std::pair<int, float>>& values, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open preset " + path;
        return false;
    }
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const auto hash = line.find('#');
        const std::string content = trim(hash == std::string::npos ? line : line.substr(0, hash));
        if (content.empty()) {
            continue;
        }
        std::pair<int, float> v;
        if (!parseAssignment(content, v, error)) {
            error = path + ":" + std::to_string(lineNumber) + ": " + error;
            return false;
        }
        values.push_back(v);
    }
    return true;
}

void listParameters(std::FILE* out) {
    std::fprintf(out, "%-4s %-24s %-16s %-6s %10s %10s %10s\n", "id", "key", "name", "unit", "min", "max",
                 "default");
    for (int32_t id = 0; id < gigrig_param_count(); ++id) {
        GigRigParamInfo p{};
        gigrig_param_info(id, &p);
        std::fprintf(out, "%-4d %-24s %-16s %-6s %10g %10g %10g\n", p.id, p.key, p.name, p.unit,
                     static_cast<double>(p.min_value), static_cast<double>(p.max_value),
                     static_cast<double>(p.default_value));
    }
}

}  // namespace

int runRender(const std::vector<std::string>& args, std::FILE* out, std::FILE* err) {
    std::string inputPath, outputPath, presetPath;
    std::string irPaths[GIGRIG_CAB_SLOT_COUNT];
    std::vector<std::string> sets;
    double channel = 1.0, block = 64.0, tail = 0.0;
    WavFormat format = WavFormat::Float32;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& target) {
            if (i + 1 >= args.size()) {
                std::fprintf(err, "missing value after %s\n", a.c_str());
                return false;
            }
            target = args[++i];
            return true;
        };
        auto number = [&](double& target) {
            std::string text;
            if (!value(text)) {
                return false;
            }
            if (!parseNumber(text, target)) {
                std::fprintf(err, "invalid number '%s' after %s\n", text.c_str(), a.c_str());
                return false;
            }
            return true;
        };
        std::string text;
        if (a == "-h" || a == "--help") {
            std::fputs(kUsage, out);
            return 0;
        } else if (a == "-l" || a == "--list-params") {
            listParameters(out);
            return 0;
        } else if (a == "-i" || a == "--input") {
            if (!value(inputPath)) return 2;
        } else if (a == "-o" || a == "--output") {
            if (!value(outputPath)) return 2;
        } else if (a == "-p" || a == "--preset") {
            if (!value(presetPath)) return 2;
        } else if (a == "--ir1") {
            if (!value(irPaths[0])) return 2;
        } else if (a == "--ir2") {
            if (!value(irPaths[1])) return 2;
        } else if (a == "-s" || a == "--set") {
            if (!value(text)) return 2;
            sets.push_back(text);
        } else if (a == "-c" || a == "--channel") {
            if (!number(channel)) return 2;
        } else if (a == "-b" || a == "--block") {
            if (!number(block)) return 2;
        } else if (a == "-t" || a == "--tail") {
            if (!number(tail)) return 2;
        } else if (a == "-f" || a == "--format") {
            if (!value(text)) return 2;
            if (text == "f32") {
                format = WavFormat::Float32;
            } else if (text == "s24") {
                format = WavFormat::Pcm24;
            } else if (text == "s16") {
                format = WavFormat::Pcm16;
            } else {
                std::fprintf(err, "unknown format '%s' (f32, s24 or s16)\n", text.c_str());
                return 2;
            }
        } else {
            std::fprintf(err, "unknown argument '%s'\n%s", a.c_str(), kUsage);
            return 2;
        }
    }
    if (inputPath.empty() || outputPath.empty()) {
        std::fprintf(err, "%s", kUsage);
        return 2;
    }
    if (block < 1.0 || block > GIGRIG_MAX_BLOCK_SIZE || block != std::floor(block)) {
        std::fprintf(err, "block size must be a whole number from 1 to %d\n", GIGRIG_MAX_BLOCK_SIZE);
        return 2;
    }
    if (tail < 0.0 || tail > 600.0) {
        std::fprintf(err, "tail must be from 0 to 600 seconds\n");
        return 2;
    }

    std::vector<std::pair<int, float>> values;
    std::string error;
    if (!presetPath.empty() && !readPreset(presetPath, values, error)) {
        std::fprintf(err, "%s\n", error.c_str());
        return 1;
    }
    for (const auto& s : sets) {
        std::pair<int, float> v;
        if (!parseAssignment(s, v, error)) {
            std::fprintf(err, "%s\n", error.c_str());
            return 2;
        }
        values.push_back(v);
    }

    WavData in;
    if (!readWav(inputPath, in, error)) {
        std::fprintf(err, "%s\n", error.c_str());
        return 1;
    }
    if (channel < 1.0 || channel > in.channels || channel != std::floor(channel)) {
        std::fprintf(err, "channel must be from 1 to %d\n", in.channels);
        return 2;
    }

    std::unique_ptr<GigRigEngine, EngineDeleter> engine{gigrig_create()};
    const auto blockSize = static_cast<int32_t>(block);
    const GigRigStatus status =
        engine ? gigrig_prepare(engine.get(), in.sampleRate, blockSize) : GIGRIG_ERROR_OUT_OF_MEMORY;
    if (status != GIGRIG_OK) {
        std::fprintf(err, "cannot prepare the engine at %g Hz (status %d)\n", in.sampleRate,
                     static_cast<int>(status));
        return 1;
    }
    if (!irPaths[1].empty()) {
        gigrig_set_param(engine.get(), GIGRIG_PARAM_CAB_MIC2_ON, 1.0f);
    }
    for (const auto& [id, v] : values) {
        gigrig_set_param(engine.get(), id, v);
    }
    bool loadedIr = false;
    for (int32_t slot = 0; slot < GIGRIG_CAB_SLOT_COUNT; ++slot) {
        const std::string& path = irPaths[slot];
        if (path.empty()) {
            continue;
        }
        WavData ir;
        if (!readWav(path, ir, error)) {
            std::fprintf(err, "%s\n", error.c_str());
            return 1;
        }
        const GigRigStatus s = gigrig_cab_load_ir(engine.get(), slot, ir.samples.data(),
                                                  static_cast<int32_t>(ir.frames()), ir.channels, ir.sampleRate);
        if (s != GIGRIG_OK) {
            std::fprintf(err, "cannot load IR %s (status %d)\n", path.c_str(), static_cast<int>(s));
            return 1;
        }
        loadedIr = true;
    }
    if (loadedIr) {
        // Let the IR crossfades finish on silence before the real input starts.
        std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0f);
        const auto blocks = static_cast<int>(std::ceil(0.05 * in.sampleRate / blockSize));
        for (int b = 0; b < blocks; ++b) {
            gigrig_process(engine.get(), silence.data(), silence.data(), blockSize);
        }
    }
    // Apply the settings as the starting state, as loading a preset would, not as a ramp.
    gigrig_reset(engine.get());

    std::vector<float> signal = in.channel(static_cast<int>(channel) - 1);
    signal.resize(signal.size() + static_cast<std::size_t>(std::lround(tail * in.sampleRate)), 0.0f);
    WavData result;
    result.sampleRate = in.sampleRate;
    result.channels = 1;
    result.samples.resize(signal.size());
    for (std::size_t pos = 0; pos < signal.size(); pos += static_cast<std::size_t>(blockSize)) {
        const auto n = static_cast<int32_t>(
            std::min<std::size_t>(static_cast<std::size_t>(blockSize), signal.size() - pos));
        gigrig_process(engine.get(), signal.data() + pos, result.samples.data() + pos, n);
    }
    gigrig_collect_garbage(engine.get());

    if (!writeWav(outputPath, result, format, error)) {
        std::fprintf(err, "%s\n", error.c_str());
        return 1;
    }
    std::fprintf(out, "rendered %zu samples at %g Hz to %s\n", result.samples.size(), in.sampleRate,
                 outputPath.c_str());
    return 0;
}

}  // namespace gigrig::tools
