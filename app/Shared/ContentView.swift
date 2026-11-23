import SwiftUI

/// Work package 1 main screen: audio status and the numbers needed to judge latency.
/// The real rig layout (utility bar, sections, amp view) replaces this in work package 5.
struct ContentView: View {
    @Environment(AudioController.self) private var audio
    @State private var showSettings = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("SuperGigRig").font(.largeTitle.bold())
                    Text("Audio passthrough").foregroundStyle(.secondary)
                }
                Spacer()
                Button {
                    showSettings = true
                } label: {
                    Label("Settings", systemImage: "gearshape")
                }
            }

            HStack(spacing: 10) {
                Circle().fill(statusColor).frame(width: 12, height: 12)
                Text(audio.statusMessage).font(.title3)
            }

            if audio.permission == .denied {
                Text(permissionHelp)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Grid(alignment: .leading, horizontalSpacing: 24, verticalSpacing: 8) {
                row("Device", audio.device?.name ?? "-")
                if let l = audio.latency {
                    row("Sample rate", String(format: "%.1f kHz", l.sampleRate / 1000))
                    row("Buffer", "\(l.bufferFrames) samples (\(formatMs(l.bufferFrames, rate: l.sampleRate)))")
                    row("Round trip, reported", formatMs(l.estimatedRoundTripFrames, rate: l.sampleRate))
                }
                if let median = audio.loopbackMedianFrames {
                    row("Round trip, measured", formatMs(median, rate: audio.loopbackRate))
                }
                row("Input", "Input \(audio.inputChannel + 1)")
                row("Output", audio.outputToBoth ? "Outputs 1 and 2" : "Output 1")
            }
            .font(.body.monospacedDigit())

            HStack {
                if audio.state == .stopped {
                    Button("Start audio") { audio.start() }
                        .buttonStyle(.borderedProminent)
                } else {
                    Button("Stop audio") { audio.stop() }
                        .buttonStyle(.bordered)
                }
            }

            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .sheet(isPresented: $showSettings) {
            SettingsView()
                .environment(audio)
        }
    }

    private var statusColor: Color {
        switch audio.state {
        case .running: return .green
        case .waitingForDevice, .interrupted: return .orange
        case .failed: return .red
        default: return .gray
        }
    }

    private var permissionHelp: String {
        #if os(macOS)
        return "Microphone access is off, so the app hears nothing. Turn it on in System Settings, "
            + "Privacy & Security, Microphone, then press Start audio."
        #else
        return "Microphone access is off, so the app hears nothing. Turn it on in Settings, "
            + "Privacy & Security, Microphone, then press Start audio."
        #endif
    }

    @ViewBuilder
    private func row(_ title: String, _ value: String) -> some View {
        GridRow {
            Text(title).foregroundStyle(.secondary)
            Text(value)
        }
    }
}
