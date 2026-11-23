import SwiftUI

/// The main screen (docs/architecture.md, section 7, "Screen structure"): utility bar on top,
/// the row of section icons, the selected section in the centre and the status bar at the
/// bottom. It is laid out once at the canvas size and scaled as one piece to fit the window
/// on the Mac and the screen on the iPad.
struct RigView: View {
    @Environment(AudioController.self) private var audio
    @AppStorage("ui.section") private var section: RigSection = .amp
    @State private var showSettings = false

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / Theme.canvas.width, geo.size.height / Theme.canvas.height)
            canvas
                .frame(width: Theme.canvas.width, height: Theme.canvas.height)
                .scaleEffect(scale)
                .frame(width: geo.size.width, height: geo.size.height)
        }
        .background(Theme.bar.ignoresSafeArea())
        .environment(audio.parameters)
        .preferredColorScheme(.dark)
        .sheet(isPresented: $showSettings) {
            SettingsView()
                .environment(audio)
        }
    }

    private var canvas: some View {
        VStack(spacing: 0) {
            UtilityBar()
            SectionBar(selection: $section)
            sectionView
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(
                    RadialGradient(colors: [Theme.stageLight, Theme.stage],
                                   center: .init(x: 0.5, y: 0.35), startRadius: 50, endRadius: 700)
                )
            StatusBar(showSettings: $showSettings)
        }
    }

    @ViewBuilder
    private var sectionView: some View {
        switch section {
        case .amp: AmpSectionView()
        case .cab: CabSectionView()
        default: GenericSectionView(section: section)
        }
    }
}

/// Input and output level. Presets (work package 15), the gate (13) and the meters (13)
/// join this bar later.
struct UtilityBar: View {
    var body: some View {
        HStack(spacing: 24) {
            Knob(key: "input.level", label: "Input", size: 40)
            Spacer()
            VStack(spacing: 2) {
                Text("SuperGigRig")
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundStyle(Theme.barText)
                Text("Presets arrive with a later update")
                    .font(.system(size: 11))
                    .foregroundStyle(Theme.dimText)
            }
            .padding(.horizontal, 40)
            .padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.6)))
            Spacer()
            Knob(key: "output.level", label: "Output", size: 40)
        }
        .padding(.horizontal, 24)
        .frame(height: 96)
        .background(Theme.bar)
        .environment(\.panelStyle, .head)
    }
}

/// One icon per section. The LED shows whether anything in the section is switched on.
struct SectionBar: View {
    @Environment(ParameterStore.self) private var store
    @Binding var selection: RigSection

    var body: some View {
        HStack(spacing: 10) {
            ForEach(visibleSections) { s in
                Button {
                    selection = s
                } label: {
                    HStack(spacing: 8) {
                        Image(systemName: s.systemImage)
                        Text(s.title).font(.system(size: 14, weight: .semibold))
                        Led(on: s.onKeys.contains { store.isOn($0) }, size: 7)
                    }
                    .foregroundStyle(s == selection ? Color.black : Theme.barText)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 9)
                    .background(RoundedRectangle(cornerRadius: 8)
                        .fill(s == selection ? Theme.accent : Color.white.opacity(0.06)))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: 52)
        .background(Theme.bar.opacity(0.9))
        .onAppear {
            if !visibleSections.contains(selection) { selection = .amp }
        }
    }

    /// Sections whose blocks exist in the core; the others appear as their packages land.
    private var visibleSections: [RigSection] {
        RigSection.allCases.filter { s in
            s.blocks.contains { !store.keys(withPrefix: $0.prefix).isEmpty }
        }
    }
}

/// Audio state, the running rate and buffer, the measured round trip and Settings. The
/// tuner, tap tempo and CPU meter join it in work packages 12 and 13.
struct StatusBar: View {
    @Environment(AudioController.self) private var audio
    @Binding var showSettings: Bool

    var body: some View {
        HStack(spacing: 16) {
            Circle().fill(statusColor).frame(width: 10, height: 10)
            Text(audio.permission == .denied ? permissionHelp : audio.statusMessage)
                .lineLimit(1)
            if let l = audio.latency {
                Text(String(format: "%.1f kHz, ", l.sampleRate / 1000) + "\(l.bufferFrames) samples")
                    .foregroundStyle(Theme.dimText)
            }
            if let median = audio.loopbackMedianFrames {
                Text("Round trip " + formatMs(median, rate: audio.loopbackRate))
                    .foregroundStyle(Theme.dimText)
            }
            Spacer()
            if audio.state == .stopped {
                Button("Start audio") { audio.start() }
            } else {
                Button("Stop audio") { audio.stop() }
            }
            Button {
                showSettings = true
            } label: {
                Label("Settings", systemImage: "gearshape")
            }
        }
        .font(.system(size: 13).monospacedDigit())
        .foregroundStyle(Theme.barText)
        .buttonStyle(.bordered)
        .padding(.horizontal, 20)
        .frame(height: 44)
        .background(Theme.bar)
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
        return "Microphone access is off. Turn it on in System Settings, Privacy & Security, Microphone."
        #else
        return "Microphone access is off. Turn it on in Settings, Privacy & Security, Microphone."
        #endif
    }
}
