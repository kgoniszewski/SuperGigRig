// test_render.cpp - WAV I/O and the gigrig_render command line.
#include <gigrig/gigrig.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "dsp_test_utils.hpp"
#include "cab/impulse_response.hpp"
#include "render.hpp"
#include "wav.hpp"

using namespace gigrig;
using namespace gigrig::test;
using namespace gigrig::tools;

namespace {

std::string tempPath(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "gigrig_tests";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

int render(const std::vector<std::string>& args) {
    std::FILE* sink = std::fopen("/dev/null", "w");
    const int code = runRender(args, sink != nullptr ? sink : stdout, sink != nullptr ? sink : stderr);
    if (sink != nullptr) {
        std::fclose(sink);
    }
    return code;
}

}  // namespace

TEST_CASE("WAV round trip in float, 24 and 16 bit") {
    WavData d;
    d.sampleRate = 44100.0;
    d.channels = 2;
    d.samples = noise(2 * 1001, 0.9);  // odd frame count exercises chunk padding
    std::string error;
    for (auto [format, tolerance] : {std::pair{WavFormat::Float32, 0.0}, std::pair{WavFormat::Pcm24, 1.0e-6},
                                     std::pair{WavFormat::Pcm16, 4.0e-5}}) {
        const auto path = tempPath("roundtrip.wav");
        REQUIRE(writeWav(path, d, format, error));
        WavData r;
        REQUIRE(readWav(path, r, error));
        CHECK(r.sampleRate == 44100.0);
        CHECK(r.channels == 2);
        REQUIRE(r.samples.size() == d.samples.size());
        CHECK(maxDifference(r.samples, d.samples) <= tolerance);
        CHECK(r.channel(1)[3] == r.samples[7]);
    }
}

TEST_CASE("WAV reader rejects missing and malformed files") {
    WavData r;
    std::string error;
    CHECK(!readWav(tempPath("does_not_exist.wav"), r, error));
    CHECK(!error.empty());
    const auto path = tempPath("garbage.wav");
    std::ofstream(path) << "this is not a wav file";
    CHECK(!readWav(path, r, error));
}

TEST_CASE("render tool: passthrough with the amp off, levels from a preset and --set") {
    WavData in;
    in.sampleRate = 48000.0;
    in.channels = 2;
    const auto left = sine(48000, 440.0, 48000.0, 0.25);
    in.samples.resize(left.size() * 2);
    for (std::size_t i = 0; i < left.size(); ++i) {
        in.samples[2 * i] = left[i];
        in.samples[2 * i + 1] = 0.0f;
    }
    std::string error;
    const auto inPath = tempPath("render_in.wav");
    const auto outPath = tempPath("render_out.wav");
    REQUIRE(writeWav(inPath, in, WavFormat::Float32, error));

    CHECK(render({"-i", inPath, "-o", outPath, "--block", "32", "--set", "amp.on=0"}) == 0);
    WavData out;
    REQUIRE(readWav(outPath, out, error));
    CHECK(out.channels == 1);
    CHECK(out.sampleRate == 48000.0);
    CHECK(out.samples == left);

    const auto presetPath = tempPath("render_preset.txt");
    std::ofstream(presetPath) << "# test preset\ninput.level = 6   # comment\n\noutput.level=-12\namp.on = 0\n";
    CHECK(render({"-i", inPath, "-o", outPath, "-p", presetPath, "-s", "output.level=-6", "-t", "0.5"}) == 0);
    REQUIRE(readWav(outPath, out, error));
    CHECK(out.samples.size() == left.size() + 24000);
    CHECK_NEAR(maxAbs(out.samples), 0.25 * dbToGain(6.0 - 6.0), 1e-4);  // --set overrides the preset
    CHECK(out.samples.back() == 0.0f);
}

TEST_CASE("render tool: --ir1 plays an impulse through a factory cabinet IR") {
    const std::string irPath = std::string(GIGRIG_IRS_DIR) + "/combo-1x15-open-cap.wav";
    WavData ir;
    std::string error;
    REQUIRE(readWav(irPath, ir, error));

    WavData in;
    in.sampleRate = 96000.0;
    in.channels = 1;
    in.samples.assign(16384, 0.0f);
    in.samples.front() = 1.0f;
    const auto inPath = tempPath("render_impulse.wav");
    const auto outPath = tempPath("render_cab.wav");
    REQUIRE(writeWav(inPath, in, WavFormat::Float32, error));
    CHECK(render({"-i", inPath, "-o", outPath, "--ir1", irPath, "--block", "32", "--set", "amp.on=0"}) == 0);
    WavData out;
    REQUIRE(readWav(outPath, out, error));
    const auto expected =
        prepareIr(makeSourceIr(ir.samples.data(), static_cast<int>(ir.frames()), 1, ir.sampleRate), 96000.0);
    REQUIRE(expected.size() < out.samples.size());
    double err = 0.0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        err = std::max(err, std::fabs(static_cast<double>(out.samples[i] - expected[i])));
    }
    CHECK(err < 1.0e-5);
    CHECK(render({"-i", inPath, "-o", outPath, "--ir1", tempPath("missing_ir.wav")}) != 0);
}

TEST_CASE("render tool: bad arguments fail with a non-zero exit code") {
    CHECK(render({}) != 0);
    CHECK(render({"--help"}) == 0);
    CHECK(render({"--list-params"}) == 0);
    CHECK(render({"-i", tempPath("missing.wav"), "-o", tempPath("x.wav")}) != 0);
    CHECK(render({"-i", "a.wav", "-o", "b.wav", "--set", "nope=1"}) != 0);
    CHECK(render({"-i", "a.wav", "-o", "b.wav", "--set", "input.level=abc"}) != 0);
    CHECK(render({"-i", "a.wav", "-o", "b.wav", "--block", "0"}) != 0);
    CHECK(render({"-i", "a.wav", "-o", "b.wav", "--format", "mp3"}) != 0);
    CHECK(render({"--frobnicate"}) != 0);
}
