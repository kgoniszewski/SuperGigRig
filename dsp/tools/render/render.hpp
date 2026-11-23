// render.hpp - the gigrig_render command line, callable from tests.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace gigrig::tools {

// Runs the renderer with arguments as typed after the program name.
// Messages go to out and err. Returns the process exit code.
int runRender(const std::vector<std::string>& args, std::FILE* out, std::FILE* err);

}  // namespace gigrig::tools
