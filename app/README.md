# app

SwiftUI app for macOS 27 and iPadOS 27. Work package 1 delivered the audio I/O: a
passthrough from the AXE I/O One input to its output through the GigRigDSP core, a
Settings screen with the reported latency, and a debug loopback test that measures the
real round trip. Work package 5 added the rig screen and the parameter binding.

## Layout

- `project.yml`: XcodeGen spec for both targets. The generated project is not committed.
- `Config/Signing.xcconfig`: signing team and bundle id prefix, overridden by your own
  `Config/Signing.local.xcconfig` (ignored by git).
- `Shared/`: SwiftUI app, views and `AudioController` (settings, state, loopback test,
  cabinet IR loading).
  - `Parameters/ParameterStore.swift`: the value of every parameter, read from the core's
    table (`gigrig_params.h`) through the C interface. Controls write here and nowhere else.
    Values are remembered between launches until presets arrive (work package 15).
  - `Controls/`: `Knob` (drag up and down; Option or Shift on the Mac, or a sideways drag on
    the iPad, for fine steps; double click or tap resets), `PanelSwitch`, `Footswitch`,
    `StepSelector`, and the placeholder `Theme`.
  - `Rig/`: the main screen (utility bar, section icons, section view, status bar), scaled as
    one canvas. Amp and Cab have their own views; Pre FX, EQ and Post FX use a generic view
    built from the parameter table until work package 14. A section or amp appears once the
    core has its parameters.
- `Bridge/`: the audio engine.
  - `GRAudioEngine.h/.mm`: Objective-C++ owner of the system I/O audio unit (HAL output on
    macOS, Remote I/O on iPadOS), device choice, sample rate and buffer setup, latency
    report, and restarts on unplug, rate change and interruption.
  - `RealtimeIO.hpp`, `LoopbackProbe.hpp`: the render path in plain C++ (input channel
    selection, DSP call, output routing, loopback click). Tested on Linux in `Bridge/tests`.
- `macOS/`, `iPadOS/`: Info.plist and entitlements.

The DSP core is compiled straight from `../dsp/src`; nothing here needs CMake.

## Build on the Mac

```sh
brew install xcodegen                 # once
cd app
cat > Config/Signing.local.xcconfig <<'EOF'
DEVELOPMENT_TEAM = YOURTEAMID
GIGRIG_BUNDLE_PREFIX = com.yourname
EOF
xcodegen
open SuperGigRig.xcodeproj
```

Run `xcodegen` again whenever files are added or `project.yml` changes. Pick the
`SuperGigRig-macOS` scheme for the Mac mini, or `SuperGigRig-iPadOS` with the iPad as the
run destination. Use the Debug configuration for the loopback test, which is only in debug
builds.

## Test the bridge on Linux

```sh
cmake -S app/Bridge/tests -B build/bridge -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DGIGRIG_SANITIZE=ON
cmake --build build/bridge && ctest --test-dir build/bridge --output-on-failure
```

See `CLAUDE.md` and `docs/architecture.md`, sections 6 and 7.
