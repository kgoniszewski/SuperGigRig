import SwiftUI
import UniformTypeIdentifiers

/// Work package 3: a plain panel to try cabinet IRs on the hardware. It only loads IRs and
/// switches the cabinet and mic 2; the real Cab screen with all its controls comes in
/// work package 5 and replaces this view.
struct CabinetTestView: View {
    @Environment(AudioController.self) private var audio
    @State private var importSlot = 0
    @State private var importing = false

    var body: some View {
        @Bindable var audio = audio
        VStack(alignment: .leading, spacing: 10) {
            Text("Cabinet").font(.title2.bold())
            Toggle("Cab on", isOn: $audio.cabOn)
            ForEach(0..<2, id: \.self) { slot in
                HStack(spacing: 12) {
                    Text("Mic \(slot + 1)").frame(width: 50, alignment: .leading)
                    Menu(audio.cabIRNames[slot].isEmpty ? "No IR" : audio.cabIRNames[slot]) {
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
                    }
                    .frame(maxWidth: 260)
                    Text(lengthText(audio.cabIRLengths[slot]))
                        .font(.footnote.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
            Toggle("Mic 2 on", isOn: $audio.mic2On)
            if let message = audio.cabMessage {
                Text(message).foregroundStyle(.red).font(.footnote)
            }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio]) { result in
            if case .success(let url) = result {
                audio.loadCabIR(url, slot: importSlot)
            }
        }
    }

    private func lengthText(_ samples: Int) -> String {
        guard samples > 0, let rate = audio.latency?.sampleRate, rate > 0 else { return "" }
        return String(format: "%.1f ms", 1000.0 * Double(samples) / rate)
    }
}
