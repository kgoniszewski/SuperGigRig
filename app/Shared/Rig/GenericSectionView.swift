import SwiftUI

/// Pre FX, EQ and Post FX until their designed screens arrive in work package 14: one plain
/// unit per block, with every parameter of the block as a knob, switch or selector and the
/// block's on switch as a footswitch. Built from the parameter table, so blocks appear here
/// as soon as their package adds them to the core.
struct GenericSectionView: View {
    @Environment(ParameterStore.self) private var store
    let section: RigSection

    var body: some View {
        VStack {
            Spacer(minLength: 0)
            HStack(alignment: .top, spacing: 16) {
                ForEach(blocks) { block in
                    BlockUnit(block: block, columns: columns)
                }
            }
            .environment(\.panelStyle, .pedal)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 24)
    }

    private var blocks: [RigBlock] {
        section.blocks.filter { !store.keys(withPrefix: $0.prefix).isEmpty }
    }

    /// Five pedals share the width in Pre FX; the EQ is one wide unit.
    private var columns: Int {
        switch section {
        case .preFX: return 2
        case .eq: return 5
        default: return 3
        }
    }
}

struct BlockUnit: View {
    @Environment(ParameterStore.self) private var store
    let block: RigBlock
    let columns: Int

    var body: some View {
        let onKey = block.prefix + ".on"
        let keys = store.keys(withPrefix: block.prefix).filter { $0 != onKey }
        VStack(spacing: 14) {
            Text(block.title)
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(PanelStyle.pedal.faceText)
            LazyVGrid(columns: Array(repeating: GridItem(.fixed(78), spacing: 6), count: min(columns, max(1, keys.count))),
                      spacing: 10) {
                ForEach(keys, id: \.self) { key in
                    ParameterControl(key: key, knobSize: 48)
                }
            }
            Spacer(minLength: 0)
            Footswitch(key: onKey)
        }
        .padding(16)
        .frame(minHeight: 400)
        .background(RoundedRectangle(cornerRadius: 12).fill(PanelStyle.pedal.face))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(PanelStyle.pedal.frame, lineWidth: 2))
        .opacity(!store.has(onKey) || store.isOn(onKey) ? 1 : 0.75)
    }
}
