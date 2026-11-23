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
    /* Cab: two mic slots and the room (section 4, row 9). The IR in each slot is not a */                \
    /* parameter: it is loaded with gigrig_cab_load_ir. Smoothing is done by the cabinet. */              \
    X(GIGRIG_PARAM_CAB_ON,          2, "cab.on",          "Cab",          "",   0.0f,   1.0f, 1.0f,       \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_CAB_MIC1_ON,     3, "cab.mic1.on",     "Mic 1",        "",   0.0f,   1.0f, 1.0f,       \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_CAB_MIC1_LEVEL,  4, "cab.mic1.level",  "Mic 1 Level",  "dB", -36.0f, 12.0f, 0.0f,      \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_CAB_MIC1_PHASE,  5, "cab.mic1.phase",  "Mic 1 Phase",  "",   0.0f,   1.0f, 0.0f,       \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_LINEAR)                                                      \
    X(GIGRIG_PARAM_CAB_MIC1_ROOM,   6, "cab.mic1.room",   "Mic 1 Room",   "%",  0.0f, 100.0f, 0.0f,       \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_CAB_MIC2_ON,     7, "cab.mic2.on",     "Mic 2",        "",   0.0f,   1.0f, 0.0f,       \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_NONE)                                                        \
    X(GIGRIG_PARAM_CAB_MIC2_LEVEL,  8, "cab.mic2.level",  "Mic 2 Level",  "dB", -36.0f, 12.0f, 0.0f,      \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_CAB_MIC2_PHASE,  9, "cab.mic2.phase",  "Mic 2 Phase",  "",   0.0f,   1.0f, 0.0f,       \
      GIGRIG_TAPER_STEPPED, GIGRIG_SMOOTHING_LINEAR)                                                      \
    X(GIGRIG_PARAM_CAB_MIC2_ROOM,  10, "cab.mic2.room",   "Mic 2 Room",   "%",  0.0f, 100.0f, 0.0f,       \
      GIGRIG_TAPER_LINEAR, GIGRIG_SMOOTHING_LINEAR)                                                       \
    X(GIGRIG_PARAM_CAB_ROOM_SIZE,  11, "cab.room.size",   "Room Size",    "",   0.0f,   2.0f, 1.0f,       \
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
