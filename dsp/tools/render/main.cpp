// gigrig_render - renders a WAV file through the GigRigDSP core. See README.md.
#include <cstdio>
#include <string>
#include <vector>

#include "render.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return gigrig::tools::runRender(args, stdout, stderr);
}
