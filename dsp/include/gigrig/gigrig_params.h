/*
 * gigrig_params.h - the parameter table of the GigRigDSP core.
 *
 * Every parameter is declared exactly once, in GIGRIG_PARAMETER_LIST below.
 * The C++ core, the C interface, the app's ParameterStore and the presets all
 * read from this one list, so a range or default can never disagree between
 * them (docs/architecture.md, section 7, "Parameter system").
 *
 * Rules (see CLAUDE.md):
 *   - The numeric id is stable. Never reuse or renumber an id once presets
 *     exist; only append new entries at the end with the next number.
 *   - The key is what presets store. Never rename a key either.
 *   - Ids are dense from 0, so the core can keep values in one flat array.
 *
 * Columns of each X(...) entry:
 *   enum name, id, key, display name, unit, minimum, maximum, default, taper, smoothing
 *
 * Plain C (C99) only: this header is included from Swift through gigrig.h.
 */
#ifndef GIGRIG_GIGRIG_PARAMS_H
#define GIGRIG_GIGRIG_PARAMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a knob position (0 to 1) maps to a value. */
typedef enum GigRigTaper {
    GIGRIG_TAPER_LINEAR = 0,  /* value moves linearly with the knob */
    GIGRIG_TAPER_LOG = 1,     /* equal ratios per knob distance (frequencies, times); min > 0 */
    GIGRIG_TAPER_STEPPED = 2  /* whole numbers from min to max (switches, selectors) */
} GigRigTaper;

/* How the audio thread smooths changes of the value. */
typedef enum GigRigSmoothing {
    GIGRIG_SMOOTHING_NONE = 0,      /* applied at the next block (switches) */
    GIGRIG_SMOOTHING_LINEAR = 1,    /* linear ramp over a fixed time (gains) */
    GIGRIG_SMOOTHING_ONE_POLE = 2   /* exponential glide (filter cutoffs) */
} GigRigSmoothing;

/* clang-format off */
#define GIGRIG_PARAMETER_LIST(X)                                                                          \
    /* Utility: input and output level (section 4, rows 0 and 14). */                                     \
    X(GIGRIG_PARAM_INPUT_LEVEL,  0, "input.level",  "Input",  "dB", -24.0f, 24.0f, 0.0f,                  \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_OUTPUT_LEVEL, 1, "output.level", "Output", "dB", -60.0f, 12.0f, 0.0f,                  \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    /* Pre FX: Boost (section 4, row 1). mode: 0 clean, 1 overdrive. */                                   \
    X(GIGRIG_PARAM_BOOST_VOLUME, 2, "boost.volume", "Volume", "dB", 0.0f, 20.0f, 6.0f,                    \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_BOOST_MODE, 3, "boost.mode", "Mode", "", 0.0f, 1.0f, 0.0f,                             \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_BOOST_ON, 4, "boost.on", "On", "", 0.0f, 1.0f, 0.0f,                                   \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    /* Pre FX: Env Filter (row 2). direction: 0 up, 1 down; speed: 0 fast, 1 slow; range: 0 lo, 1 hi. */  \
    X(GIGRIG_PARAM_ENVFILTER_GAIN, 5, "envfilter.gain", "Gain", "", 0.0f, 10.0f, 5.0f,                    \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_ENVFILTER_PEAK, 6, "envfilter.peak", "Peak", "", 0.0f, 10.0f, 5.0f,                    \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_ONE_POLE)                                                     \
    X(GIGRIG_PARAM_ENVFILTER_DIRECTION, 7, "envfilter.direction", "Direction", "", 0.0f, 1.0f, 0.0f,      \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_ENVFILTER_SPEED, 8, "envfilter.speed", "Speed", "", 0.0f, 1.0f, 0.0f,                  \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_ENVFILTER_RANGE, 9, "envfilter.range", "Range", "", 0.0f, 1.0f, 0.0f,                  \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_ENVFILTER_ON, 10, "envfilter.on", "On", "", 0.0f, 1.0f, 0.0f,                          \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    /* Pre FX: Gold OD (row 3). */                                                                        \
    X(GIGRIG_PARAM_GOLDOD_GAIN, 11, "goldod.gain", "Gain", "", 0.0f, 10.0f, 3.0f,                         \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_GOLDOD_LEVEL, 12, "goldod.level", "Level", "dB", -24.0f, 12.0f, 0.0f,                  \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_GOLDOD_TREBLE, 13, "goldod.treble", "Treble", "", 0.0f, 10.0f, 5.0f,                   \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_ONE_POLE)                                                     \
    X(GIGRIG_PARAM_GOLDOD_ON, 14, "goldod.on", "On", "", 0.0f, 1.0f, 0.0f,                                \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    /* Pre FX: Twin OD (row 4). mode: 0 mid-focused, 1 amp-like. */                                       \
    X(GIGRIG_PARAM_TWINOD_GAIN, 15, "twinod.gain", "Gain", "", 0.0f, 10.0f, 4.0f,                         \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_TWINOD_MODE, 16, "twinod.mode", "Mode", "", 0.0f, 1.0f, 0.0f,                          \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_TWINOD_LEVEL, 17, "twinod.level", "Level", "dB", -24.0f, 12.0f, 0.0f,                  \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_TWINOD_TONE, 18, "twinod.tone", "Tone", "", 0.0f, 10.0f, 5.0f,                         \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_ONE_POLE)                                                     \
    X(GIGRIG_PARAM_TWINOD_ON, 19, "twinod.on", "On", "", 0.0f, 1.0f, 0.0f,                                \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    /* Pre FX: Bucket Delay (row 5). Feedback above 100% self-oscillates. */                              \
    X(GIGRIG_PARAM_BUCKETDELAY_TIME, 20, "bucketdelay.time", "Time", "ms", 20.0f, 800.0f, 350.0f,         \
      GIGRIG_TAPER_LOG, GIGRIG_SMOOTHING_ONE_POLE)                                                        \
    X(GIGRIG_PARAM_BUCKETDELAY_FEEDBACK, 21, "bucketdelay.feedback", "Feedback", "%",                     \
      0.0f, 110.0f, 35.0f, GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                  \
    X(GIGRIG_PARAM_BUCKETDELAY_BLEND, 22, "bucketdelay.blend", "Blend", "%", 0.0f, 100.0f, 35.0f,         \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_BUCKETDELAY_ON, 23, "bucketdelay.on", "On", "", 0.0f, 1.0f, 0.0f,                      \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)
/* clang-format on */

typedef enum GigRigParamId {
#define GIGRIG_PARAM_ENUM_ENTRY(name, id, key, display, unit, lo, hi, def, taper, smoothing) name = id,
    GIGRIG_PARAMETER_LIST(GIGRIG_PARAM_ENUM_ENTRY)
#undef GIGRIG_PARAM_ENUM_ENTRY
    /* Number of parameters; ids run from 0 to GIGRIG_PARAM_COUNT - 1. Not a parameter. */
    GIGRIG_PARAM_COUNT
} GigRigParamId;

/* Description of one parameter. Strings are static and never freed. */
typedef struct GigRigParamInfo {
    int32_t id;
    const char* key;          /* stable preset key, for example "input.level" */
    const char* name;         /* display name */
    const char* unit;         /* "dB", "Hz", "ms", "%" or "" */
    float min_value;
    float max_value;
    float default_value;
    GigRigTaper taper;
    GigRigSmoothing smoothing;
} GigRigParamInfo;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GIGRIG_GIGRIG_PARAMS_H */
