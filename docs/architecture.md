> **Exported copy.** The living version of this plan is the Claude Doc "SuperGigRig architecture plan". This file was exported from it on 2026-11-23 for work package 0. If the two differ, ask Krzysztof which one is current.
>
> **Decisions made after this plan was written (Krzysztof, 2026-11-23).** These answer the open points in section 10 and override the plan where they differ:
>
> - **Three-in-One amp blend is not in version 1.** Work package 8 is deferred; the amp selector in version 1 shows three amps.
> - **Harmonic Tremolo, Spring Reverb (the Verb and Trem section) and the Compressor are not in version 1.** Work package 10 is deferred, and work package 11 delivers the EQ only.
> - **No paid macOS CI.** CI runs the DSP core on Linux only. Krzysztof builds the apps in Xcode on his Mac, and that build is the app compile check.
> - Also deferred, as already listed in section 10: transpose, doubler, metronome and MIDI.
> - **The default is 96 kHz with a 32 sample buffer on both devices, and CPU budgets are sized for 96 kHz.** Work package 1 measured that 48 kHz misses the 5 ms target on both devices (see section 2 and `docs/latency.md`). Sections 1, 2, 5, 6, 8 and 10 are updated to match.

# SuperGigRig architecture plan

2026-11-15 · @Krzysztof

## 1. Summary

SuperGigRig is a standalone guitar amp rig for macOS 27 and iPadOS 27, built as one SwiftUI app over one portable C++ DSP core, with original circuit-style models of a three-amp rig in the style of the reference plugin. The architect (Fable 5.2) owns this plan; the coder (Opus 5.5) implements it one work package at a time from section 9.

Decisions confirmed by Krzysztof on 2026-11-15:

| Topic | Decision |
| --- | --- |
| Repository | [kgoniszewski/SuperGigRig](https://github.com/kgoniszewski/SuperGigRig), currently holding only a LICENSE file |
| Build and signing | Built in Xcode 27 on the Mac mini M4, signed with a paid Apple Developer account |
| Stack | SwiftUI interface, shared real-time DSP core in C++, Core Audio I/O, one codebase |
| Format | Standalone app first; DSP core shaped so an AUv3 plugin can be added later |
| Amp modelling | Hand-written circuit-style models of three amp types, tuned by measurement and by ear |
| Closeness check | A/B by ear; Krzysztof does not own the reference plugin |
| Cabinets | IR loader, free properly licensed factory IRs, user IRs; no Neural DSP IRs |
| Latency | 96 kHz, 32 sample buffer, under 5 ms round trip on the AXE I/O One (changed from 48 kHz on 2026-11-23 after the work package 1 measurements) |
| Look and naming | Same layout and workflow as the reference; original artwork and generic names; personal use only |

What "as close as possible" means here: the same signal chain, the same blocks, the same controls on each block, and the same sound character. It does not mean a null-test match, because the models are written from public circuit knowledge and tuned by ear, not captured from the reference.

The reference feature inventory in this plan comes from the public [user manual](https://neuraldsp.com/manual/archetype-john-mayer-x) and the [launch announcement](https://neuraldsp.com/news/introducing-archetype-john-mayer-x). Three things in it go beyond the version 1 scope that was confirmed and need a yes or no from Krzysztof; they are listed in section 10.

## 2. Constraints and targets

The hard target is a round trip under 5 ms at 96 kHz with a 32 sample buffer on both devices, measured by loopback on the AXE I/O One, with zero added latency from the DSP itself.

| Item | Target |
| --- | --- |
| Mac | Mac mini M4, 16 GB, macOS 27 |
| iPad | iPad Pro 11 (M2), iPadOS 27, AXE I/O One over USB-C |
| Interface | IK Multimedia AXE I/O One, class compliant USB audio |
| Input | Mono, one selectable input channel (default input 1, the instrument input) |
| Output | Mono signal, sent to output 1 or duplicated to outputs 1 and 2 (setting) |
| Sample rate | 96 kHz default on both devices; core must run correctly at 44.1, 48, 88.2 and 96 kHz |
| Buffer | 32 samples preferred; 64 is the fallback on the Mac (it misses the target on the iPad); user selectable up to 256 |
| DSP latency | 0 samples: minimum-phase oversampling filters and zero-latency convolution |
| CPU | Full chain under 25% of one performance core on the M2 at 96 kHz, 32 samples (about twice the work of 48 kHz); every work package sizes its CPU cost at 96 kHz |
| Dropouts | None in a 30 minute soak test at the chosen buffer size |

Latency budget at 96 kHz and 32 samples: 0.33 ms input buffer, 0.33 ms output buffer, and the converter and USB safety offsets that the interface and the OS decide, much of which the OS does not report. Work package 1 measured the round trip by loopback (full table in `docs/latency.md`):

| Device | 96 kHz, 32 | 96 kHz, 64 | 48 kHz, 32 |
| --- | --- | --- | --- |
| Mac mini M4 | **2.58 ms** | 3.25 ms | 5.58 ms |
| iPad Pro 11 M2 | **4.53 ms** | 5.20 ms | 7.90 ms |

The unreported part (about 3 ms on the Mac and 4 ms on the iPad at 48 kHz) is roughly fixed in samples, so doubling the rate almost halves it in time. That is why 96 kHz with 32 samples is the only setting that meets 5 ms on both devices. The iPad keeps under 0.5 ms of margin, so the DSP must add nothing.

Legal boundaries, for personal use:

- All DSP code is original. No decompiling, no captured or neural-trained models of the reference plugin.
- No Neural DSP impulse responses, presets, images or fonts.
- Block names are generic. No Neural DSP, Fender, Dumble, Two-Rock, Klon, Ibanez, Marshall or John Mayer names or logos in the app.
- Every bundled IR has its licence recorded in `irs/LICENSES.md` before it is committed.

API caution for the coder: this plan relies only on Core Audio and SwiftUI APIs that have been stable for several OS releases. Anything specific to the version 27 SDKs must be checked against the Xcode 27 documentation, not assumed.

## 3. System architecture

The app has three layers with one rule between them: nothing above the DSP core runs on the audio thread, and nothing on the audio thread allocates, locks, logs or calls Swift.

_(diagram: see the original plan document)_

The worker queue hands finished objects, such as a prepared cabinet IR, to the core through the command queue, so the audio thread only ever swaps a pointer.

| Layer | Language | Responsibility |
| --- | --- | --- |
| App | Swift, SwiftUI | Screens, knobs, preset browser, settings, tuner display, file import |
| Engine bridge | Objective-C++ and a C header | Owns the I/O audio unit, hosts the render callback, exposes parameters, meters and commands to Swift |
| DSP core (`GigRigDSP`) | C++20, no dependencies | The whole signal chain; builds on Linux, macOS and iPadOS |

The DSP core exposes a plain C interface in `gigrig.h`: create, prepare (sample rate, maximum block), process (mono in, mono out, any block size), set parameter, push command, read meter. A C interface keeps Swift interop simple and is exactly what an AUv3 wrapper will need later.

Threading:

1. **Audio thread** (Core Audio I/O thread). Runs the render callback in C++ only. Reads parameters, processes one block, writes meters.
2. **Main thread** (SwiftUI). Writes parameter values, reads meters at 30 Hz, sends commands.
3. **Worker queue** (utility priority). Loads and resamples IRs, builds new convolver objects, parses presets, runs tuner pitch detection.

Crossing between threads uses three mechanisms and no others:

- **Parameters**: one flat array of atomic floats indexed by parameter id. The audio thread reads each value once per block and smooths it per sample where needed.
- **Commands**: a single-producer single-consumer lock-free queue for structural changes, such as swapping in a prepared IR. Objects are built on the worker queue, handed over by pointer, and the replaced object is sent back on a second queue to be freed off the audio thread.
- **Meters and tuner audio**: atomics for levels and gain reduction, and a lock-free ring buffer carrying input audio to the tuner.

Real-time rules the coder must hold in every DSP file: all memory is allocated in `prepare`; no exceptions, no virtual dispatch per sample, no `std::function`, no system calls in `process`; flush-to-zero is enabled on the audio thread to avoid denormal slowdowns; every block accepts any block size from 1 to the prepared maximum.

The render path is single threaded. One performance core on either device has ample headroom for this chain, and a single thread avoids all cross-thread scheduling jitter at a 32 sample buffer.

## 4. Signal chain

The chain follows the reference section by section: utility input, Pre FX, Verb and Trem, Amp, Cab, EQ and Comp, Post FX, utility output. Every block has a bypass. Names in the "Our block" column are working names that Krzysztof can change.

| # | Section | Our block | Role in the reference | Controls |
| --- | --- | --- | --- | --- |
| 0 | Utility | Input, Gate | Input level and noise gate | Input (dB), Gate threshold (dB), Gate on |
| 1 | Pre FX | Boost | Always-on clean boost at the start of the chain | Volume, Mode (clean or overdrive-style), On |
| 2 | Pre FX | Env Filter | Envelope filter that follows pick attack | Gain (sensitivity), Peak, Up or Down, Fast or Slow, Hi or Lo, On |
| 3 | Pre FX | Gold OD | Mid-gain transparent overdrive | Gain, Level, Treble, On |
| 4 | Pre FX | Twin OD | Dual-mode overdrive | Gain, Mode (mid-focused or amp-like), Level, Tone, On |
| 5 | Pre FX | Bucket Delay | Analog bucket-brigade delay that can self-oscillate | Time, Feedback, Blend, On |
| 6 | Verb and Trem | Harmonic Tremolo | Tube harmonic tremolo | Speed, Intensity, Sync, On |
| 7 | Verb and Trem | Spring Reverb | 1960s style spring tank | Mix, Tone, Decay, On |
| 8 | Amp | Combo 40 | 40 W 1964 American combo with a 15 inch speaker | Bright, Volume, Bass, Treble, Output |
| 8 | Amp | Steel 100 | Rare 100 W clean head with high headroom | Input (FET or normal), Bright, Mid boost, Gain, Bass, Middle, Treble, Master, Output |
| 8 | Amp | Proto 50 | 50 W boutique prototype head | Bright, EQ 1 or 2, Gain, Bass, Mid, Treble, Presence, Master, Output |
| 8 | Amp | Three-in-One | All three amps blended with fixed cabs | Gain, three amp levels, Output, Room send, Room size |
| 9 | Cab | Cabinet | One matched cabinet per amp, two mic slots, user IRs | Per slot: IR select, Level, Phase, Room send; Room size (small, medium, large) |
| 10 | EQ and Comp | EQ | Four semi-parametric bands with high and low pass | HPF, LPF, four bands of Frequency and Gain (plus or minus 12 dB) |
| 11 | EQ and Comp | Compressor | Studio compressor with fixed timing | Input, Output, gain reduction meter |
| 12 | Post FX | Digital Delay | 1980s rack-style delay | Time, Feedback, Mix, Hardness, Sync (free, tempo, tap) |
| 13 | Post FX | Studio Reverb | Hall and plate | Mode, Mix, Pre-delay, Decay, Low cut, High cut |
| 14 | Utility | Output | Output level and meter | Output (dB) |

Only one of the four amp choices runs at a time. In Three-in-One mode the three amp models run in parallel from one shared input gain, each into its own fixed cabinet IR, and the Cab section is hidden, as in the reference.

Because the output is mono, the stereo-only features of the reference are left out: ping-pong on the delay, per-mic pan, and the doubler. The reverbs and room are computed in mono.

## 5. DSP design

Every nonlinear block is a grey-box model: linear filters derived from the circuit, a static nonlinearity for each gain stage, and a small amount of state for bias shift and supply sag, all run oversampled to about 192 kHz. This is far cheaper than full circuit simulation and keeps the knobs behaving like the real controls.

### Shared building blocks

| Block | Design |
| --- | --- |
| Oversampler | 2 times and 4 times, polyphase IIR half-band filters (minimum phase, zero lookahead). An 8 times mode is a build option for the Mac. Guidance for the blocks that use it: pick the factor from the session rate in `prepare`, 2 times at 88.2 and 96 kHz (the default) and 4 times at 44.1 and 48 kHz, so the inner rate stays near 192 kHz. A block keeps 4 times at 96 kHz only if its aliasing test needs it and the CPU budget allows. The work package 2 oversampler already supports both factors; nothing in it changes. |
| Waveshaper | Tabulated or closed-form curve with first-order antiderivative anti-aliasing |
| Filters | Biquads in transposed direct form II, a state-variable filter for swept filters, one-pole smoothers |
| Delay line | Fractional delay with cubic interpolation, power-of-two circular buffer |
| Envelope follower | Peak and RMS detectors with separate attack and release |
| LFO | Sine and triangle, free-running or tempo locked |
| Parameter smoother | Linear ramp per block for gains, one-pole per sample for filter cutoffs |

### Amp models

All three amps share one structure, with different component values, stage counts and voicing:

1. **Input stage**: input impedance loading filter, optional bright cap as a level-dependent high shelf.
2. **Preamp triode stages** (two to four): each stage is a coupling high-pass, a 12AX7-style asymmetric transfer curve with grid conduction, a cathode bypass shelf and a Miller-effect low-pass. A slow bias-shift state gives the bloom and compression of a pushed stage.
3. **Tone stack**: the passive treble, middle, bass network solved analytically and discretised, so the knobs interact as they do on the amp. One coefficient routine per topology.
4. **Phase inverter**: long-tail pair as a soft symmetric clipper with its own headroom.
5. **Power amp**: push-pull 6L6-style pair with crossover region, negative feedback loop that sets presence and damping, and supply sag driven by an envelope of the output current.
6. **Output transformer**: low-frequency saturation and a band-limit.

| Amp | Voicing notes for the coder |
| --- | --- |
| Combo 40 | AB763-family vibrato channel without tremolo and reverb: two triode stages, treble and bass stack with fixed mid, about 40 W power stage, early soft breakup, tube rectifier sag |
| Steel 100 | Very high headroom clean: FET or normal input, three-band stack with mid boost, fixed deep voicing, stiff solid-state-rectifier supply, strong negative feedback, about 100 W |
| Proto 50 | Steel-family preamp with more gain: two EQ voicings, presence control in the feedback loop, about 50 W, earlier power-amp compression |

The public AB763 schematic is the starting point for Combo 40. The other two start from the well-documented general topology of that amp family and are voiced by ear, since exact values are not public.

### Pedals and effects

| Block | Design |
| --- | --- |
| Gate | Detector on the raw input with hysteresis and hold; gain applied at the input |
| Boost | Clean mode is a high-headroom gain stage; overdrive mode adds a soft MOSFET-style clipper |
| Env Filter | State-variable filter swept by an envelope follower; direction, speed and range switches |
| Gold OD | Clean and clipped paths summed, hard diode clipping in the dirty path, treble shelf |
| Twin OD | Mid-focused mode clips in the feedback loop with a mid hump and bass cut; amp-like mode is flatter and softer |
| Bucket Delay | Delay line with clock-dependent low-pass, compander, gentle saturation in the feedback path, self-oscillation allowed and limited |
| Harmonic Tremolo | Crossover splits lows and highs; one LFO modulates the two bands in opposite phase |
| Spring Reverb | Chain of dispersive all-pass filters with delay feedback per spring, two or three springs, tone filter |
| EQ | Second-order high and low pass, two shelves and two peaking bands, frequency and gain per band |
| Compressor | Feed-forward FET character, fixed attack, release and ratio, automatic make-up gain |
| Digital Delay | Clean delay up to 2 s, low-pass in the feedback loop for Hardness, tempo sync and tap |
| Studio Reverb | Hall as a 16-line feedback delay network; plate as a Dattorro-style tank; shared pre-delay and cut filters |
| Room | Short 8-line feedback delay network fed from the cab sends, three sizes |

### Cabinet

The cabinet is a zero-latency convolver: the first 64 samples run as a direct FIR, the rest as non-uniformly partitioned FFT convolution with the FFT work spread evenly across blocks. IRs are loaded on the worker queue, converted to mono, resampled to the session rate, trimmed to 200 ms and normalised, then swapped in with a short crossfade. Two slots can run in parallel, each with level and phase. At 96 kHz a 200 ms IR is 19,200 taps, twice the convolution work of 48 kHz, and the convolver is the largest single item in the 96 kHz CPU budget.

The reference has continuous mic position and distance knobs. Version 1 replaces them with a list of discrete IRs per cabinet; morphing between positions is deferred.

### Tuner

A ring buffer carries decimated input audio to the worker queue, where a YIN-family pitch detector runs about 20 times a second. Reference pitch is adjustable from 420 to 460 Hz, with an option to mute the output while tuning.

## 6. Audio I/O

Both platforms use the system I/O audio unit directly, in full duplex on one device, because it gives the shortest path between the interface and the DSP core. `AVAudioEngine` is not used for the live path.

|  | macOS | iPadOS |
| --- | --- | --- |
| I/O unit | `AUAudioUnit` of subtype HAL output, input and output enabled on the same device | `AUAudioUnit` of subtype Remote I/O, input and output enabled |
| Device choice | Core Audio device list; AXE I/O One preferred automatically when present | `AVAudioSession` route; the USB interface becomes the route when plugged in |
| Sample rate | Set the device nominal rate to 96 kHz | `setPreferredSampleRate(96000)` |
| Buffer | Set the device buffer frame size to 32 or 64 | `setPreferredIOBufferDuration` for 32 or 64 frames; use what the system grants |
| Session | Not applicable | Category play and record, mode measurement, so no system input processing is applied |
| Permission | Microphone usage description and audio input entitlement | Microphone usage description |
| Background | Keeps running when not frontmost | Audio background mode so playing continues behind other apps |

Rules for the bridge:

- The render block is written in Objective-C++ and captures only C++ pointers. It pulls input from the unit, calls `process` on the core, and writes the result to the selected output channels.
- Stream format is 32-bit float, non-interleaved, at the device rate. The app never inserts a sample rate converter; if the device rate changes, the engine stops, calls `prepare` again and restarts.
- The app reports the real numbers in Settings: granted buffer size, device input and output latency, safety offsets, and their sum in milliseconds.
- A debug-only loopback test plays a click on the output and measures its arrival on the input, so round trip latency is measured with a cable, not estimated.
- Device unplug, route change and interruption are handled by stopping cleanly, showing the state in the UI, and restarting when the device returns.

On iPadOS the system decides the smallest buffer it will grant. Work package 1 found that it grants 32 frames at 96 kHz on the AXE I/O One (4.53 ms round trip). The only fallback is 64 frames at 96 kHz (5.20 ms, just over target); 48 kHz misses the target at any buffer size.

## 7. App layer

One SwiftUI app target per platform shares all screens; only window setup, device selection and file import differ.

### Screen structure

The layout mirrors the reference: a utility bar on top, a row of section icons, the selected section filling the centre, and a status bar at the bottom.

| Area | Contents |
| --- | --- |
| Utility bar | Input knob and meter, Gate knob and switch, preset name with previous, next, save, save as and browser, Output knob and meter |
| Section icons | Pre FX, Verb and Trem, Amp, Cab, EQ and Comp, Post FX; each icon shows whether anything in it is active |
| Section view | The pedals, amp panel, cabinet, EQ or rack units of the selected section, with their knobs and switches |
| Amp selector | Four thumbnails under the amp view: three amps and Three-in-One |
| Status bar | Tuner, tap tempo and BPM, CPU load, Settings |
| Settings | Audio device, sample rate, buffer size, input channel, output channels, measured latency, tuner reference |

The Mac window is resizable with a fixed aspect ratio and scales as one canvas. The iPad runs in landscape, full screen.

### Parameter system

Every parameter is declared once, in a C++ table in the DSP core: stable numeric id, string key, display name, unit, minimum, maximum, default, taper and smoothing type. The bridge exposes this table to Swift, so the UI, the presets and the DSP can never disagree about a range or a default. Ids are never reused or renumbered once a preset format ships.

A single observable `ParameterStore` in Swift holds the current values. A knob writes to the store, the store writes the atomic value in the core, and the DSP smooths it. There is no other path from UI to sound.

### Controls

One custom `Knob` view serves the whole app: vertical drag, fine adjustment with a modifier key or a two-finger drag, double tap or double click to reset, and a value readout while dragging. Switches, stomp buttons and selectors are similar small reusable views. All of them support pointer and touch.

### Presets

A preset is a versioned JSON file: format version, name, tags, and a map from parameter key to value, plus references to user IR files by bookmark. Factory presets ship read-only in the app bundle; user presets live in the app's Application Support folder on the Mac and its Documents folder on the iPad. Input, Gate and Output can be locked so that changing preset does not change them, as in the reference.

### Artwork

All artwork is original. Version 1 draws amps, pedals and racks as vector graphics in SwiftUI, in a warm stage setting like the reference but without copying any image. Front panels show only our working names.

## 8. Repository, build and testing

The coder works in a Linux cloud environment that cannot run Xcode, so the repository is arranged so that the DSP core is fully buildable and testable on Linux and the Xcode project is generated from a text file.

### Layout

```
SuperGigRig/
  CLAUDE.md                 rules for coders: real-time rules, layout, how to test
  docs/architecture.md      this plan, exported
  dsp/
    CMakeLists.txt
    include/gigrig/         public C header and parameter table
    src/                    blocks, amps, effects, convolver, engine
    tests/                  unit and measurement tests
    tools/render/           command-line renderer: WAV in, preset in, WAV out
  app/
    project.yml             XcodeGen spec for the macOS and iPadOS targets
    Shared/                 SwiftUI views, ParameterStore, presets
    Bridge/                 Objective-C++ audio engine and C bridge
    macOS/  iPadOS/         platform setup, Info.plist, entitlements
  presets/factory/          JSON presets
  irs/                      factory IRs and LICENSES.md
  .github/workflows/        CI
```

### Build

- **DSP on Linux and CI**: CMake with Clang, warnings as errors, address and undefined-behaviour sanitisers in the test build.
- **App on the Mac**: Krzysztof installs XcodeGen once with Homebrew, runs `xcodegen` in `app/`, opens the generated project in Xcode 27 and builds either target. The generated project is not committed.
- **App compile check in CI**: a GitHub Actions macOS job builds both targets unsigned, so the coder gets compiler feedback on Swift and Objective-C++ without a Mac. This uses paid macOS runner minutes on a private repository and depends on GitHub offering an Xcode 27 image; if either is a problem, Krzysztof's local build is the check.

### Tests

| Kind | What it proves |
| --- | --- |
| Unit tests | Each block against a known answer: filter responses, delay times, tone stack curves against the analytic network |
| Real-time safety | A test build that aborts on any allocation inside `process`, run over every block and the full chain |
| Block-size independence | Same output for block sizes 1, 32, 64 and 512 |
| Sample-rate sweep | Every block stable and correctly tuned at 44.1, 48, 88.2 and 96 kHz |
| Aliasing | A swept sine through each nonlinear block; aliased energy below a stated threshold |
| Latency | An impulse through the full chain arrives with zero samples of delay when time effects are off |
| Golden renders | A few DI guitar clips rendered through factory presets; changes in output must be intentional |
| Benchmark | Full-chain CPU cost per block at 96 kHz and 32 samples, tracked over time |

### Judging closeness

Closeness is judged by ear by Krzysztof, in a tuning pass at the end, against public recordings and demos of the reference. To make that pass efficient, the renderer produces a clip for every amp and preset from the same DI recordings, and the app has an instant A and B preset compare.

The reference plugin has a 14 day free trial. Installing it on the Mac mini at the start of the tuning work package, not earlier, would give a direct side-by-side for listening during those two weeks. This is optional.

## 9. Work packages

Seventeen work packages take the project from an empty repository to a tuned app, each sized for one coder thread and one pull request. Packages are done in order unless the "Needs" column says they can run in parallel. Every package must leave CI green and must not break the real-time rules in section 3.

| WP | Title | Delivers | Needs | Done when |
| --- | --- | --- | --- | --- |
| 0 | Repository bootstrap | Layout from section 8, `CLAUDE.md`, CMake, test framework, CI, empty `gigrig.h` with create, prepare, process as passthrough | none | DSP builds and one test passes on Linux CI |
| 1 | Audio I/O and passthrough app | XcodeGen project, both app targets, bridge with I/O unit, Settings screen, loopback latency test | 0 | Krzysztof hears guitar through both devices and reports measured round trip at 32 and 64 samples |
| 2 | DSP foundations | Oversampler, waveshaper, filters, delay line, envelope follower, LFO, smoothers, parameter table, command queues, allocation guard, render tool | 0 | All unit, aliasing and real-time safety tests pass |
| 3 | Cabinet and room | Zero-latency convolver, IR loader and resampler, two slots, room reverb, first licensed factory IRs | 2 | Convolver matches direct convolution to within rounding and adds zero latency |
| 4 | Combo 40 amp | Full amp model with controls from section 4 | 2 | Tone stack matches the analytic curve; renders posted for listening |
| 5 | UI shell and parameter binding | Utility bar, section navigation, Knob and switch views, ParameterStore, amp and cab views with placeholder art | 1, 2 | Krzysztof can play Combo 40 through a cab and turn every knob on both devices |
| 6 | Steel 100 amp | Full amp model | 4 | Same as package 4 |
| 7 | Proto 50 amp | Full amp model | 4 | Same as package 4 |
| 8 | Three-in-One | Three amps in parallel with fixed cabs, blend controls, room send | 3, 6, 7 | Blend runs inside the CPU target on the iPad |
| 9 | Pre FX | Boost, Env Filter, Gold OD, Twin OD, Bucket Delay | 2 | Unit and aliasing tests pass for each pedal |
| 10 | Verb and Trem | Harmonic Tremolo, Spring Reverb | 2 | Tests pass; tremolo locks to tempo |
| 11 | EQ and Comp | Four-band EQ with filters, compressor with meter | 2 | EQ curves match design; gain reduction meter reads correctly |
| 12 | Post FX | Digital Delay with sync and tap, Studio Reverb with hall and plate | 2 | Tests pass; no zipper noise when Time is moved |
| 13 | Gate, tuner and meters | Noise gate, tuner engine and view, input and output meters, CPU meter | 2, 5 | Tuner reads within 1 cent on test tones from 70 Hz to 1.3 kHz |
| 14 | Section views | Pre FX, Verb and Trem, EQ and Comp, Post FX and Three-in-One screens bound to parameters | 5, 8 to 12 | Every parameter in the table has a control on screen |
| 15 | Presets | JSON format, browser, save, save as, favourites, locks, IR import on both platforms, factory preset set | 14 | Presets round-trip without loss and load without clicks |
| 16 | Artwork, tuning and performance | Final vector artwork, voicing pass on all amps and effects with Krzysztof, CPU optimisation, 30 minute soak test | 15 | Krzysztof signs off the sound and both devices pass the soak test |

Packages 6, 7 and 9 to 12 depend only on the foundations and can run as parallel coder threads once package 4 has proven the amp structure.

Milestones:

1. **Playable passthrough** after package 1: latency is known on real hardware before any DSP is written.
2. **First tone** after package 5: one amp and one cab, playable on both devices.
3. **Full chain** after package 13: every block exists and is tested.
4. **Complete app** after package 15: all screens and presets.
5. **Tuned release** after package 16.

What each coder thread receives: the work package row, the sections of this plan it touches, and `CLAUDE.md`. What it returns: one pull request, test results, and for sound-producing packages a set of rendered clips for Krzysztof to listen to.

## 10. Risks, open points and deferred items

Three points need a yes or no from Krzysztof before the plan is final, because the reference manual shows more than the scope that was confirmed.

- [ ] **Three-in-One blend in version 1?** It is the view in the screenshot. Recommended: yes, as package 8.
- [ ] **Verb and Trem section and the compressor in version 1?** They sit inside the reference chain but were not in the confirmed list. Recommended: yes, as packages 10 and 11.
- [ ] **Paid macOS CI minutes for the app compile check?** Recommended: yes, limited to pull requests that touch `app/`.

| Risk | Effect | Mitigation |
| --- | --- | --- |
| iPadOS refuses a 32 sample buffer on the AXE I/O One | Round trip above 5 ms on the iPad | Measured in package 1: 32 samples at 96 kHz is granted (4.53 ms); 64 samples at 96 kHz (5.20 ms) is the fallback |
| Interface safety offsets are larger than assumed | Target missed on both devices regardless of code | Happened in package 1 at 48 kHz (5.58 ms Mac, 7.90 ms iPad); the default moved to 96 kHz, which meets the target on both |
| Twice the DSP work at 96 kHz | Full chain over the CPU target on the iPad | 2 times oversampling at 96 kHz, convolver cost measured in package 3, per-block benchmark at 96 kHz from package 3 onward |
| Sound is not close enough without the reference to compare | Long tuning phase | Renders after every amp package, optional 14 day trial during package 16 |
| No suitable free IRs for a 15 inch combo or the two cabinet types | Factory cab sound is generic | User IR loading works from package 3; licences checked per file |
| Coder cannot run the app | Swift errors found late | CI compile check, and Krzysztof builds at each milestone |
| Version 27 SDK differences | Build breaks on details | Plan uses long-stable APIs; coder checks current documentation |

Deferred past version 1, with the design leaving room for each:

- AUv3 plugin wrapper around the same core.
- Transpose, doubler, stereo input mode and stereo output.
- Metronome and MIDI control with MIDI learn.
- Continuous mic position and distance on the cabinet.
- iCloud sync of presets between the Mac and the iPad.

Sources: [Archetype John Mayer X user manual](https://neuraldsp.com/manual/archetype-john-mayer-x) and [launch announcement](https://neuraldsp.com/news/introducing-archetype-john-mayer-x), both read on 2026-11-15, plus the two screenshots Krzysztof provided.
