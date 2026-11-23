# SuperGigRig

A standalone guitar amp rig for macOS 27 and iPadOS 27: one SwiftUI app over one portable, real-time
C++ DSP core, built for an IK AXE I/O One (mono in, mono out) with a round-trip target under 5 ms.
Original circuit-style models of a three-amp clean-to-edge rig. Personal use only.

- Architecture plan: [docs/architecture.md](docs/architecture.md)
- Rules for contributors and coding agents: [CLAUDE.md](CLAUDE.md)

## Status

Work package 0 (repository bootstrap): the DSP core builds and runs as a passthrough with tests on
Linux CI. The apps arrive in work package 1.

## Quick start (DSP core)

```sh
cd dsp
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Licence

GPL-3.0, see [LICENSE](LICENSE). Bundled impulse responses carry their own licences, listed in
[irs/LICENSES.md](irs/LICENSES.md).
