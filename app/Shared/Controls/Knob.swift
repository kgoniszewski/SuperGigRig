import SwiftUI
#if os(macOS)
import AppKit
#endif

/// The one knob of the app (docs/architecture.md, section 7, "Controls"), bound to a
/// parameter by its preset key.
///
/// - Drag up or down to turn it; a full turn is 200 points of travel.
/// - Fine adjustment, a tenth of the speed: hold Option or Shift on the Mac, or move the
///   finger more than 80 points sideways while dragging on the iPad (a two-finger drag is
///   not reliable in SwiftUI, so this replaces it).
/// - Double click or double tap resets to the default.
/// - The value is shown under the knob while dragging.
struct Knob: View {
    @Environment(ParameterStore.self) private var store
    @Environment(\.panelStyle) private var style

    let key: String
    var label: String?
    var size: CGFloat = 56

    @State private var dragging = false
    @State private var lastY: CGFloat = 0
    @State private var position: Double = 0

    private static let travel: CGFloat = 200
    private static let fineSideways: CGFloat = 80

    var body: some View {
        if let info = store.info(key) {
            VStack(spacing: 4) {
                Text(label ?? info.name)
                    .font(.system(size: 11, weight: .semibold))
                    .textCase(.uppercase)
                    .foregroundStyle(style.faceText)
                    .lineLimit(1)
                    .fixedSize()
                dial(store.normalized(key))
                    .frame(width: size, height: size)
                    .contentShape(Circle())
                    .gesture(drag)
                    .onTapGesture(count: 2) { store.reset(key) }
                Text(dragging ? store.displayText(key) : " ")
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(style.faceText)
                    .frame(height: 12)
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(label ?? info.name)
            .accessibilityValue(store.displayText(key))
            .accessibilityAdjustableAction { direction in
                let step = info.taper == .stepped ? 1.0 / Double(max(1, info.stepCount - 1)) : 0.05
                let now = store.normalized(key)
                switch direction {
                case .increment: store.setNormalized(key, now + step)
                case .decrement: store.setNormalized(key, now - step)
                @unknown default: break
                }
            }
        }
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 1)
            .onChanged { g in
                if !dragging {
                    dragging = true
                    lastY = g.translation.height
                    position = store.normalized(key)
                }
                let dy = lastY - g.translation.height
                lastY = g.translation.height
                let speed: Double = fineMode(sideways: abs(g.translation.width)) ? 0.1 : 1
                position = min(1, max(0, position + Double(dy / Self.travel) * speed))
                store.setNormalized(key, position)
            }
            .onEnded { _ in dragging = false }
    }

    private func fineMode(sideways: CGFloat) -> Bool {
        #if os(macOS)
        let flags = NSEvent.modifierFlags
        return flags.contains(.option) || flags.contains(.shift)
        #else
        return sideways > Self.fineSideways
        #endif
    }

    /// A pointer knob over a scale of eleven ticks, 270 degrees from 7 to 5 o'clock.
    private func dial(_ normalized: Double) -> some View {
        let angle = Angle.degrees(-135 + 270 * normalized)
        return ZStack {
            ForEach(0..<11, id: \.self) { i in
                Capsule()
                    .fill(style.faceText.opacity(0.7))
                    .frame(width: size * 0.03, height: size * 0.08)
                    .offset(y: -size * 0.5)
                    .rotationEffect(.degrees(-135 + 27 * Double(i)))
            }
            Circle()
                .fill(style.knob)
                .shadow(color: .black.opacity(0.5), radius: size * 0.05, y: size * 0.04)
                .padding(size * 0.1)
            Circle()
                .stroke(style.knobPointer.opacity(0.15), lineWidth: 1)
                .padding(size * 0.16)
            Capsule()
                .fill(style.knobPointer)
                .frame(width: size * 0.06, height: size * 0.26)
                .offset(y: -size * 0.22)
                .rotationEffect(angle)
        }
    }
}
