import SwiftUI

/// Audio settings and the latency report (docs/architecture.md, sections 6 and 7).
struct SettingsView: View {
    @Environment(AudioController.self) private var audio
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        @Bindable var audio = audio
        NavigationStack {
            Form {
                Section("Audio device") {
                    #if os(macOS)
                    Picker("Device", selection: $audio.preferredDeviceUID) {
                        Text("Automatic (AXE I/O One when connected)").tag("")
                        ForEach(audio.devices, id: \.uid) { d in
                            Text("\(d.name) (\(d.inputChannels) in, \(d.outputChannels) out)").tag(d.uid)
                        }
                    }
                    #else
                    LabeledContent("Route", value: audio.device?.name ?? "-")
                    Text("iPadOS picks the USB interface automatically when it is plugged in.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    #endif
                    Picker("Sample rate", selection: $audio.sampleRate) {
                        ForEach(AudioController.sampleRates, id: \.self) { r in
                            Text(String(format: "%.1f kHz", r / 1000)).tag(r)
                        }
                    }
                    Picker("Buffer size", selection: $audio.bufferFrames) {
                        ForEach(AudioController.bufferSizes, id: \.self) { n in
                            Text("\(n) samples").tag(n)
                        }
                    }
                    Picker("Input", selection: $audio.inputChannel) {
                        ForEach(0..<audio.inputChannelCount, id: \.self) { c in
                            Text(c == 0 ? "Input 1 (instrument)" : "Input \(c + 1)").tag(c)
                        }
                    }
                    Picker("Output", selection: $audio.outputToBoth) {
                        Text("Output 1").tag(false)
                        Text("Outputs 1 and 2").tag(true)
                    }
                }

                Section("Latency") {
                    if let l = audio.latency {
                        latencyRow("Sample rate", String(format: "%.0f Hz", l.sampleRate))
                        latencyRow("Buffer granted", frames: l.bufferFrames, rate: l.sampleRate)
                        if l.bufferFrames != audio.bufferFrames {
                            Text("The system granted \(l.bufferFrames) samples instead of the \(audio.bufferFrames) asked for.")
                                .font(.footnote)
                                .foregroundStyle(.orange)
                        }
                        latencyRow("Input device and stream", frames: l.inputDeviceLatencyFrames, rate: l.sampleRate)
                        #if os(macOS)
                        latencyRow("Input safety offset", frames: l.inputSafetyOffsetFrames, rate: l.sampleRate)
                        #endif
                        latencyRow("Output device and stream", frames: l.outputDeviceLatencyFrames, rate: l.sampleRate)
                        #if os(macOS)
                        latencyRow("Output safety offset", frames: l.outputSafetyOffsetFrames, rate: l.sampleRate)
                        #endif
                        latencyRow("DSP", frames: l.dspLatencyFrames, rate: l.sampleRate)
                        latencyRow("Round trip, reported", frames: l.estimatedRoundTripFrames, rate: l.sampleRate)
                            .bold()
                    } else {
                        Text("Start audio to see the latency numbers.").foregroundStyle(.secondary)
                    }
                }

                #if DEBUG
                loopbackSection
                #endif
            }
            .formStyle(.grouped)
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .onAppear { audio.refreshDevices() }
        }
        #if os(macOS)
        .frame(minWidth: 520, minHeight: 600)
        #endif
    }

    #if DEBUG
    private var loopbackSection: some View {
        Section("Loopback test (debug builds)") {
            Text("Unplug the guitar and turn monitors and headphones down. Connect a short cable from "
                 + "output 1 to the selected input, set the input gain to about half, then press Measure. "
                 + "Do not play during the test: it plays five clicks and times when each comes back.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Button(audio.loopbackPhase == .measuring ? "Measuring..." : "Measure round trip") {
                audio.measureLoopback()
            }
            .disabled(!audio.isRunning || audio.loopbackPhase == .measuring)
            ForEach(Array(audio.loopbackResults.enumerated()), id: \.offset) { i, frames in
                if frames >= 0 {
                    latencyRow("Click \(i + 1)", frames: frames, rate: audio.loopbackRate)
                } else if frames == AudioController.loopbackTooNoisy {
                    latencyRow("Click \(i + 1)", "input too noisy")
                } else {
                    latencyRow("Click \(i + 1)", "not detected")
                }
            }
            if let median = audio.loopbackMedianFrames {
                latencyRow("Round trip, measured (median)", frames: median, rate: audio.loopbackRate)
                    .bold()
            } else if audio.loopbackPhase == .done {
                Text(audio.loopbackResults.contains(AudioController.loopbackTooNoisy)
                     ? "The input was too noisy to hear the clicks. Unplug the guitar and use a short cable."
                     : "No click came back. Check the cable, the input selection and the input gain.")
                    .foregroundStyle(.red)
            }
            if audio.loopbackSpreadFrames > Int(audio.loopbackRate / 1000) {
                Text("The clicks disagree by more than 1 ms, so the result is not reliable. "
                     + "Check that nothing else is plugged into the input and measure again.")
                    .foregroundStyle(.orange)
            }
        }
    }
    #endif

    private func latencyRow(_ title: String, frames: Int, rate: Double) -> some View {
        latencyRow(title, "\(frames) samples, \(formatMs(frames, rate: rate))")
    }

    private func latencyRow(_ title: String, _ value: String) -> some View {
        LabeledContent(title) {
            Text(value).monospacedDigit()
        }
    }
}
