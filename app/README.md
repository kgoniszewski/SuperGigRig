# app

SwiftUI app for macOS 27 and iPadOS 27, starting in work package 1.

- `project.yml`: XcodeGen spec for both targets (generate with `xcodegen` in this folder; the
  generated project is not committed).
- `Shared/`: SwiftUI views, ParameterStore, presets.
- `Bridge/`: Objective-C++ audio engine (I/O audio unit, render callback) and the C bridge to `dsp/`.
- `macOS/`, `iPadOS/`: platform setup, Info.plist, entitlements.

See `CLAUDE.md` and `docs/architecture.md`, sections 6 and 7.
