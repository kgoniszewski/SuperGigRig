// gigrig_irgen - writes the factory cabinet IRs (see cab_model.hpp) as WAV files.
//   gigrig_irgen OUTPUT_DIR
#include <cstdio>
#include <string>

#include "cab_model.hpp"
#include "wav.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gigrig_irgen OUTPUT_DIR\n");
        return 2;
    }
    const std::string dir = argv[1];
    for (const auto& cab : gigrig::tools::factoryCabinets()) {
        for (const auto& mic : gigrig::tools::factoryMics()) {
            gigrig::tools::WavData wav;
            wav.sampleRate = gigrig::tools::kIrSampleRate;
            wav.channels = 1;
            wav.samples = gigrig::tools::generateCabIr(cab, mic);
            const std::string path = dir + "/" + cab.name + "-" + mic.name + ".wav";
            std::string error;
            if (!gigrig::tools::writeWav(path, wav, gigrig::tools::WavFormat::Float32, error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("wrote %s\n", path.c_str());
        }
    }
    return 0;
}
