/*
 * gigrig.h - public C interface of the GigRigDSP core.
 *
 * This is the only header the app bridge (and, later, an AUv3 wrapper)
 * includes. It is plain C so that Swift and Objective-C++ can call it directly.
 *
 * Threading contract:
 *   - gigrig_create, gigrig_destroy and gigrig_prepare run on a non-audio
 *     thread, never concurrently with gigrig_process on the same engine.
 *   - gigrig_process runs on the audio thread. It never allocates, locks,
 *     logs or makes system calls.
 *
 *   - Parameter functions (gigrig_set_param and friends) may be called from
 *     any thread at any time; values cross to the audio thread through one
 *     flat array of atomic floats and are read once per block.
 *   - gigrig_collect_garbage runs on one non-audio thread (the app's worker
 *     queue), never on the audio thread.
 *
 * Work package 4 state: the chain is Input level, the Combo 40 amp (on by
 * default, bypassed with "amp.on" = 0) and Output level. Meters and the rest
 * of the signal chain (docs/architecture.md, sections 3 and 4) arrive in later
 * packages.
 */
#ifndef GIGRIG_GIGRIG_H
#define GIGRIG_GIGRIG_H

#include <stdint.h>

#include "gigrig_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of this C interface. Bumped whenever a function is added or changed. */
#define GIGRIG_API_VERSION 3

/* Limits accepted by gigrig_prepare. */
#define GIGRIG_MIN_SAMPLE_RATE 8000.0
#define GIGRIG_MAX_SAMPLE_RATE 192000.0
#define GIGRIG_MAX_BLOCK_SIZE  4096

typedef enum GigRigStatus {
    GIGRIG_OK = 0,
    GIGRIG_ERROR_NULL_ENGINE = 1,
    GIGRIG_ERROR_INVALID_SAMPLE_RATE = 2,
    GIGRIG_ERROR_INVALID_BLOCK_SIZE = 3,
    GIGRIG_ERROR_OUT_OF_MEMORY = 4,
    GIGRIG_ERROR_INVALID_PARAM = 5
} GigRigStatus;

/* Opaque engine handle. */
typedef struct GigRigEngine GigRigEngine;

/* Returns GIGRIG_API_VERSION of the compiled library. */
int32_t gigrig_api_version(void);

/* Creates an engine. Returns NULL if allocation fails. Not real-time safe. */
GigRigEngine* gigrig_create(void);

/* Destroys an engine created by gigrig_create. Accepts NULL. Not real-time safe. */
void gigrig_destroy(GigRigEngine* engine);

/*
 * Prepares the engine for processing: allocates all memory and resets state.
 * sample_rate: GIGRIG_MIN_SAMPLE_RATE to GIGRIG_MAX_SAMPLE_RATE (Hz).
 * max_block_size: 1 to GIGRIG_MAX_BLOCK_SIZE samples; gigrig_process will
 *   never be called with more samples than this.
 * May be called again at any time to change rate or block size (for example
 * after a device rate change), as long as gigrig_process is not running.
 * On error the engine is left unprepared. Not real-time safe.
 */
GigRigStatus gigrig_prepare(GigRigEngine* engine, double sample_rate, int32_t max_block_size);

/* Clears all internal state (delay lines, filters) without reallocating. */
void gigrig_reset(GigRigEngine* engine);

/*
 * Processes one block of mono audio. Real-time safe.
 * input and output may point to the same buffer (in-place processing).
 * num_samples may be any value from 0 to the prepared max_block_size; a larger
 * count is processed internally in chunks of max_block_size.
 * Before a successful gigrig_prepare, or if input is NULL, the output is silence.
 * If engine or output is NULL the call does nothing.
 */
void gigrig_process(GigRigEngine* engine, const float* input, float* output, int32_t num_samples);

/* Latency the engine adds, in samples at the prepared rate. Always 0 by design. */
int32_t gigrig_latency_samples(const GigRigEngine* engine);

/* ---- Parameters (API version 2). Ids and keys are declared in gigrig_params.h. ----
 * API version 3: the amp parameters (ids 2 to 7) and the Combo 40 amp in gigrig_process. */

/* Number of parameters (GIGRIG_PARAM_COUNT). */
int32_t gigrig_param_count(void);

/* Fills *info for a parameter id. Returns GIGRIG_ERROR_INVALID_PARAM for an unknown id or NULL info. */
GigRigStatus gigrig_param_info(int32_t id, GigRigParamInfo* info);

/* Id of the parameter with this preset key, or -1 if there is none. */
int32_t gigrig_param_find(const char* key);

/*
 * Sets a parameter. Any thread, real-time safe, lock free. The value is
 * clamped to the parameter's range (and rounded for stepped parameters); NaN
 * is ignored. Unknown ids and a NULL engine are ignored. The audio thread
 * picks the value up at the start of its next block and smooths it.
 */
void gigrig_set_param(GigRigEngine* engine, int32_t id, float value);

/* Current value of a parameter (the last value set), or 0 for an unknown id or NULL engine. */
float gigrig_get_param(const GigRigEngine* engine, int32_t id);

/* Knob position (0 to 1) of a value, following the parameter's taper. 0 for an unknown id. */
float gigrig_param_to_normalized(int32_t id, float value);

/* Value at a knob position (0 to 1, clamped), following the taper. 0 for an unknown id. */
float gigrig_param_from_normalized(int32_t id, float normalized);

/* ---- Commands (API version 2) ---- */

/*
 * Frees objects the audio thread has retired (for example a cabinet IR that
 * was replaced). Call regularly, for example at 30 Hz alongside meter reads,
 * from one non-audio thread. Returns the number of objects freed. Not
 * real-time safe.
 */
int32_t gigrig_collect_garbage(GigRigEngine* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GIGRIG_GIGRIG_H */
