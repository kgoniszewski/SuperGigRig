# app

SwiftUI app for macOS 27 and iPadOS 27. Work package 1 delivers the audio I/O: a
passthrough from the AXE I/O One input to its output through the GigRigDSP core, a
Settings screen with the reported latency, and a debug loopback test that measures the
real round trip.

## Layout

- `project.yml`: XcodeGen spec for both targets. The generated project is not committed.
- `Config/Signing.xcconfig`: signing team and bundle id prefix, overridden by your own
  `Config/Signing.local.xcconfig` (ignored by git).
- `Shared/`: SwiftUI app, views and `AudioController` (settings, state, loopback test).
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
