// gigrig.cpp - C interface wrapper around gigrig::Engine.
#include <gigrig/gigrig.h>

#include "denormals.hpp"
#include "engine.hpp"

#include <algorithm>
#include <new>

struct GigRigEngine {
    gigrig::Engine engine;
};

extern "C" {

int32_t gigrig_api_version(void) {
    return GIGRIG_API_VERSION;
}

GigRigEngine* gigrig_create(void) {
    return new (std::nothrow) GigRigEngine{};
}

void gigrig_destroy(GigRigEngine* engine) {
    delete engine;
}

GigRigStatus gigrig_prepare(GigRigEngine* engine, double sample_rate, int32_t max_block_size) {
    if (engine == nullptr) {
        return GIGRIG_ERROR_NULL_ENGINE;
    }
    if (!(sample_rate >= GIGRIG_MIN_SAMPLE_RATE && sample_rate <= GIGRIG_MAX_SAMPLE_RATE)) {
        engine->engine.prepare(0.0, 0);  // leaves it unprepared, as documented
        return GIGRIG_ERROR_INVALID_SAMPLE_RATE;
    }
    if (max_block_size < 1 || max_block_size > GIGRIG_MAX_BLOCK_SIZE) {
        engine->engine.prepare(0.0, 0);
        return GIGRIG_ERROR_INVALID_BLOCK_SIZE;
    }
    try {
        return engine->engine.prepare(sample_rate, max_block_size) ? GIGRIG_OK
                                                                   : GIGRIG_ERROR_INVALID_BLOCK_SIZE;
    } catch (const std::bad_alloc&) {
        engine->engine.prepare(0.0, 0);
        return GIGRIG_ERROR_OUT_OF_MEMORY;
    }
}

void gigrig_reset(GigRigEngine* engine) {
    if (engine != nullptr) {
        engine->engine.reset();
    }
}

void gigrig_process(GigRigEngine* engine, const float* input, float* output, int32_t num_samples) {
    if (engine == nullptr || output == nullptr || num_samples <= 0) {
        return;
    }
    gigrig::ScopedNoDenormals noDenormals;
    gigrig::Engine& e = engine->engine;
    // Before prepare, max block is 0: process everything in one silent call.
    const int32_t chunk = e.isPrepared() ? e.maxBlockSize() : num_samples;
    int32_t done = 0;
    while (done < num_samples) {
        const int32_t n = std::min(chunk, num_samples - done);
        e.processBlock(input != nullptr ? input + done : nullptr, output + done, n);
        done += n;
    }
}

int32_t gigrig_latency_samples(const GigRigEngine* engine) {
    return engine != nullptr ? engine->engine.latencySamples() : 0;
}

}  // extern "C"
