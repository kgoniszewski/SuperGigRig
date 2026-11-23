// engine_handle.hpp - definition of the opaque GigRigEngine handle from gigrig.h.
// Internal: lets the C wrapper and the tests reach the C++ engine behind a handle.
#pragma once

#include <gigrig/gigrig.h>

#include "engine.hpp"

struct GigRigEngine {
    gigrig::Engine engine;
};
