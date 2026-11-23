import SwiftUI
import UniformTypeIdentifiers

/// The Cab section: the cabinet switch, two mic slots (IR, on, level, phase, room send) and
/// the room size (docs/architecture.md, section 4, row 9). Replaces the work package 3 test
/// panel. The IR in each slot is not a parameter: it is loaded through AudioController.
struct CabSectionView: View {
    @Environment(AudioController.self) private var audio
    @Environment(ParameterStore.self) private var store
    @State private var importSlot = 0
    @State private var importing = false

    var body: some View {
        VStack(spacing: 26) {
            Spacer(minLength: 0)
            HStack(alignment: .top, spacing: 30) {
                VStack(spacing: 20) {
                    PanelSwitch(key: "cab.on", label: "Cab")
                    StepSelector(key: "cab.room.size", label: "Room", names: PositionNames.byKey["cab.room.size"] ?? [])
                }
                .padding(20)
                .background(RoundedRectangle(cornerRadius: 10).fill(PanelStyle.pedal.face))
                ForEach(0..<2, id: \.self) { slot in
                    micSlot(slot)
                }
            }
            .environment(\.panelStyle, .pedal)
            .opacity(store.isOn("cab.on") ? 1 : 0.6)
            if let message = audio.cabMessage {
                Text(message).foregroundStyle(.red).font(.system(size: 13))
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 40)
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio]) { result in
            if case .success(let url) = result {
                audio.loadCabIR(url, slot: importSlot)
            }
        }
    }

    private func micSlot(_ slot: Int) -> some View {
        let p = "cab.mic\(slot + 1)."
        return VStack(spacing: 16) {
            HStack {
                Text("Mic \(slot + 1)")
                    .font(.system(size: 16, weight: .bold))
                    .foregroundStyle(PanelStyle.pedal.faceText)
                Spacer()
                Text(lengthText(audio.cabIRLengths[slot]))
                    .font(.system(size: 11).monospacedDigit())
                    .foregroundStyle(Theme.dimText)
            }
            Menu {
                ForEach(AudioController.factoryIRs, id: \.self) { url in
                    Button(url.deletingPathExtension().lastPathComponent) {
                        audio.loadCabIR(url, slot: slot)
                    }
                }
                Divider()
                Button("Load your own IR file...") {
                    importSlot = slot
                    importing = true
                }
                Button("Clear") { audio.clearCabIR(slot: slot) }
            } label: {
                HStack {
                    Image(systemName: "waveform")
                    Text(audio.cabIRNames[slot].isEmpty ? "No IR" : audio.cabIRNames[slot])
                        .lineLimit(1)
                    Spacer()
                    Image(systemName: "chevron.down")
                }
                .font(.system(size: 13))
                .foregroundStyle(Theme.barText)
                .padding(.horizontal, 10)
                .padding(.vertical, 7)
                .background(RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.45)))
            }
            .menuStyle(.button)
            .buttonStyle(.plain)
            .menuIndicator(.hidden)
            HStack(alignment: .top, spacing: 22) {
                PanelSwitch(key: p + "on", label: "On")
                Knob(key: p + "level", label: "Level", size: 56)
                PanelSwitch(key: p + "phase", label: "Phase", positions: ("Normal", "Invert"))
                Knob(key: p + "room", label: "Room", size: 56)
            }
        }
        .padding(20)
        .frame(width: 400)
        .background(RoundedRectangle(cornerRadius: 10).fill(PanelStyle.pedal.face))
        .opacity(store.isOn(p + "on") ? 1 : 0.7)
    }

    private func lengthText(_ samples: Int) -> String {
        guard samples > 0, let rate = audio.latency?.sampleRate, rate > 0 else { return "" }
        return String(format: "%.1f ms", 1000.0 * Double(samples) / rate)
    }
}
