import SwiftUI

/// The Amp section: the selected amp's front panel and the amp selector under it.
/// Placeholder vector art; the final artwork comes in work package 16.
struct AmpSectionView: View {
    @Environment(ParameterStore.self) private var store

    var body: some View {
        VStack(spacing: 28) {
            Spacer(minLength: 0)
            AmpPanel(amp: selectedAmp)
            AmpSelector(amps: availableAmps, selected: selectedAmp)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 40)
    }

    /// Amps the core knows, in selector order.
    private var availableAmps: [RigAmp] {
        RigAmp.all.filter { !store.keys(withPrefix: $0.prefix).isEmpty }
    }

    /// Before "amp.model" exists (work package 6) the only amp is Combo 40.
    private var selectedAmp: RigAmp {
        let model = store.has("amp.model") ? Int(store.value("amp.model").rounded()) : 0
        return availableAmps.first { $0.model == model } ?? RigAmp.all[0]
    }
}

/// One amp: a tolex body with the control panel across the top. The controls are the amp's
/// parameters in table order, so a new amp model needs no new view code.
struct AmpPanel: View {
    @Environment(ParameterStore.self) private var store
    let amp: RigAmp

    private var isCombo: Bool { amp.prefix == "combo40" }
    private var style: PanelStyle { isCombo ? .combo : .head }

    var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .top, spacing: 26) {
                VStack(spacing: 6) {
                    PanelSwitch(key: "amp.on", label: "Power")
                }
                ForEach(store.keys(withPrefix: amp.prefix), id: \.self) { key in
                    ParameterControl(key: key, knobSize: 62)
                }
            }
            .padding(.vertical, 18)
            .padding(.horizontal, 30)
            .frame(maxWidth: .infinity)
            .background(RoundedRectangle(cornerRadius: 6).fill(style.face))
            .environment(\.panelStyle, style)
            .padding(14)

            if isCombo {
                grille
            } else {
                Text(amp.name)
                    .font(.system(size: 26, weight: .heavy, design: .serif).italic())
                    .foregroundStyle(Color(white: 0.85))
                    .padding(.bottom, 18)
            }
        }
        .frame(width: 900)
        .background(RoundedRectangle(cornerRadius: 14).fill(tolex))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Color.black.opacity(0.7), lineWidth: 3))
        .shadow(color: .black.opacity(0.6), radius: 20, y: 12)
        .opacity(store.isOn("amp.on") ? 1 : 0.7)
    }

    private var tolex: Color {
        isCombo ? Color(red: 0.08, green: 0.08, blue: 0.08) : Color(red: 0.13, green: 0.11, blue: 0.10)
    }

    /// Wheat-coloured grille cloth with the amp's name, for the combo.
    private var grille: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(red: 0.62, green: 0.55, blue: 0.43))
            Canvas { ctx, size in
                var path = Path()
                var x: CGFloat = -size.height
                while x < size.width {
                    path.move(to: CGPoint(x: x, y: size.height))
                    path.addLine(to: CGPoint(x: x + size.height, y: 0))
                    x += 6
                }
                ctx.stroke(path, with: .color(Color.black.opacity(0.12)), lineWidth: 1)
            }
            .clipShape(RoundedRectangle(cornerRadius: 6))
            Text(amp.name)
                .font(.system(size: 30, weight: .heavy, design: .serif).italic())
                .foregroundStyle(Color(white: 0.95))
                .shadow(color: .black.opacity(0.4), radius: 2, y: 1)
                .offset(x: -300, y: -70)
        }
        .frame(height: 220)
        .padding([.horizontal, .bottom], 14)
    }
}

/// Thumbnails under the amp, one per amp model in the core.
struct AmpSelector: View {
    @Environment(ParameterStore.self) private var store
    let amps: [RigAmp]
    let selected: RigAmp

    var body: some View {
        HStack(spacing: 16) {
            ForEach(amps) { amp in
                Button {
                    if store.has("amp.model") { store.set("amp.model", Float(amp.model)) }
                } label: {
                    VStack(spacing: 6) {
                        RoundedRectangle(cornerRadius: 5)
                            .fill(amp.prefix == "combo40" ? Color(red: 0.85, green: 0.82, blue: 0.74)
                                                          : Color(red: 0.25, green: 0.25, blue: 0.26))
                            .frame(width: 96, height: 30)
                            .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.black, lineWidth: 2))
                        Text(amp.name).font(.system(size: 13, weight: .semibold))
                    }
                    .foregroundStyle(amp.id == selected.id ? Theme.accent : Theme.barText)
                    .padding(10)
                    .background(RoundedRectangle(cornerRadius: 8)
                        .stroke(amp.id == selected.id ? Theme.accent : Color.clear, lineWidth: 2))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
    }
}

/// The right control for a parameter: a switch for two positions, a selector for three, a
/// knob for continuous values and for longer stepped ranges (a row of many buttons would not
/// fit a pedal).
struct ParameterControl: View {
    @Environment(ParameterStore.self) private var store
    let key: String
    var knobSize: CGFloat = 52

    var body: some View {
        if let info = store.info(key) {
            let names = PositionNames.byKey[key] ?? []
            if info.taper == .stepped && info.stepCount == 2 {
                PanelSwitch(key: key, positions: names.count == 2 ? (names[0], names[1]) : nil)
            } else if info.taper == .stepped && info.stepCount == 3 {
                StepSelector(key: key, names: names)
            } else {
                Knob(key: key, size: knobSize)
            }
        }
    }
}
