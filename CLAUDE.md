# CLAUDE.md

Rules for anyone (human or Claude) writing code in this repository. Read this file and the
sections of `docs/architecture.md` that your work package touches before you start.

## What this is

SuperGigRig is a standalone guitar amp rig for macOS 27 (Mac mini M4) and iPadOS 27 (iPad Pro 11 M2):
one SwiftUI app over one portable, real-time C++ DSP core, with an IK AXE I/O One interface, mono in and
mono out. The target is a round trip under 5 ms at **96 kHz with a 32 sample buffer** (the default on both
devices; measured in `docs/latency.md`), and **zero added latency from the DSP**. Size CPU cost at 96 kHz:
about twice the work of 48 kHz, so prefer 2x oversampling at 96 kHz. Personal use only.

## How work is organised

- The architecture plan is `docs/architecture.md` (exported from the architect's living Claude Doc).
  The decisions block at its top overrides the plan body where they differ.
- Work is split into work packages WP0 to WP16 (plan section 9). **One work package per thread and per
  pull request.** Stay inside your package's scope; leave hooks for later packages, not half features.
- If the plan is imprecise or two rules conflict, ask Krzysztof before writing the code.
- Every PR must leave CI green and must not break the real-time rules below.

### Not in version 1 (do not build these)

Three-in-One amp blend (WP8), Harmonic Tremolo, Spring Reverb and the Compressor (WP10 and the
compressor half of WP11), transpose, doubler, metronome, MIDI, AUv3 wrapper, stereo I/O, continuous mic
position, iCloud preset sync. Design so they can be added later, nothing more.

## Naming and legal rules

- All DSP code is original. No decompiling, no captured or neural-trained models of any commercial product.
- No third-party trademarks in code identifiers, UI text, preset names, artwork or file names: no
  Neural DSP, Archetype, Fender, Dumble, Two-Rock, Klon, Ibanez, Marshall or John Mayer. Use the generic
  working names from plan section 4 (Combo 40, Steel 100, Proto 50, Gold OD, Twin OD, and so on).
  Circuit family names in code comments (for example "AB763-family tone stack") are fine.
- No Neural DSP impulse responses, presets, images or fonts.
- Every bundled IR has its licence recorded in `irs/LICENSES.md` **before** it is committed.

## Repository layout

```
CLAUDE.md                 this file
docs/architecture.md      the architecture plan
dsp/                      GigRigDSP, the C++20 core (no dependencies, builds on Linux and Apple)
  CMakeLists.txt          library gigrig_dsp (alias GigRig::dsp) and tests
  CMakePresets.json       dev (Clang, Debug, ASan+UBSan) and release presets
  include/gigrig/         public C header gigrig.h and the parameter table gigrig_params.h
  src/                    engine, parameters, command queues; amps, effects (later WPs)
  src/cab/                Cab section: zero-latency convolver, IR preparation, two mic slots, room (WP3)
  src/blocks/             shared building blocks: filters, smoothers, delay line, envelope,
                          LFO, waveshaper (ADAA), oversampler
  tests/                  unit and measurement tests (minimal harness in test_framework.hpp)
  tools/render/           gigrig_render: WAV in, parameters and IRs in, WAV out
  tools/irgen/            gigrig_irgen: generates the factory cab IRs in irs/ (WP3)
app/                      SwiftUI app for both platforms (WP1 onward)
  project.yml             XcodeGen spec (WP1)
  Shared/  Bridge/  macOS/  iPadOS/
presets/factory/          factory presets as JSON (WP15)
irs/                      factory IRs and LICENSES.md (WP3)
.github/workflows/        CI: DSP core on Linux only
```

## Building and testing the DSP core

Requires CMake 3.24+, Ninja and Clang (GCC also works).

```sh
cd dsp
cmake --preset dev && cmake --build --preset dev && ctest --preset dev    # Debug + sanitisers
cmake --preset release && cmake --build --preset release && ctest --preset release
./build/dev/tests/gigrig_tests passthrough                                 # run tests whose name contains "passthrough"
```

On Ubuntu the sanitiser runtime for Clang comes from `libclang-rt-18-dev`.

Warnings are errors (`GIGRIG_WARNINGS_AS_ERRORS`, on by default). Fix warnings; do not silence them
with pragmas or by turning the option off.

### Writing tests

- Tests use the tiny harness in `dsp/tests/test_framework.hpp`: `TEST_CASE("name")`, `CHECK`,
  `REQUIRE`, `CHECK_NEAR`. Add new test files to `dsp/tests/CMakeLists.txt`.
- Each DSP block gets the tests from plan section 8: known-answer unit tests, block-size independence
  (1, 32, 64, 512), sample-rate sweep (44.1, 48, 88.2, 96 kHz), aliasing for nonlinear blocks, zero
  latency with time effects off. From WP2, every block also runs under the allocation guard.
- `dsp/tests/dsp_test_utils.hpp` has the helpers: `checkBlockSizeIndependence` (runs every block
  size under the allocation guard), `kTestRates`, test signals, `toneAmplitude` and an FFT. The
  aliasing measurement to copy for new nonlinear blocks is `aliasingDb` in `test_nonlinear.cpp`.
- New parameters are appended to `GIGRIG_PARAMETER_LIST` in `gigrig_params.h` with the next id, and
  the pinned-ids test in `test_engine.cpp` gets the new entry.

## Building the app

The cloud coding environment is Linux and cannot run Xcode. Krzysztof builds on his Mac:
`brew install xcodegen` once, then `cd app && xcodegen` and open the generated project in Xcode 27.
The generated `.xcodeproj` is not committed. There is no macOS CI, so Swift and Objective-C++ changes
are checked by Krzysztof's build: keep them small and simple, use APIs that have been stable for several
OS releases, and check anything specific to the version 27 SDKs against current Apple documentation.
The app should compile the DSP sources directly (or link `gigrig_dsp`); keep `dsp/` free of anything
that needs CMake-generated headers.

## Real-time rules (every file under dsp/src)

Nothing on the audio thread allocates, locks, logs, throws or calls Swift.

- All memory is allocated in `prepare`. `process` never calls `new`, `malloc`, `std::vector::push_back`
  or anything else that may allocate.
- No exceptions, no `std::function`, no virtual dispatch per sample, no system calls, no I/O, no
  mutexes in `process`. Mark audio-path functions `noexcept`.
- Every block accepts any block size from 1 to the prepared maximum and produces the same output
  whatever the block size.
- Flush-to-zero is on during processing (`ScopedNoDenormals` in `gigrig_process`); do not rely on
  denormals, and do not undo it.
- Zero added latency: minimum-phase oversampling filters, zero-latency convolution, no lookahead.
- The core must run correctly at 44.1, 48, 88.2 and 96 kHz.
- Threads cross only through: atomic float parameters (one flat array indexed by parameter id), the
  lock-free SPSC command queue plus its return queue for freeing objects off the audio thread, and
  atomics or a lock-free ring buffer for meters and tuner audio. No other mechanism.

## C interface rules (dsp/include/gigrig/gigrig.h)

- Plain C only: no C++ types, no exceptions crossing the boundary. Swift and the future AUv3 wrapper use it.
- Bump `GIGRIG_API_VERSION` whenever a function is added or its behaviour changes.
- Parameter ids (from WP2) are stable: never reuse or renumber an id once presets exist; only append.

## Style

- C++20, `.clang-format` at the repository root (4-space indent, 110 columns).
- Namespace `gigrig`. Headers `.hpp` inside `dsp/src`, the public header is C (`.h`).
- Comments explain why (circuit reasoning, real-time constraints), not what the code says.
