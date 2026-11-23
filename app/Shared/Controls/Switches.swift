import SwiftUI

/// A two-position panel switch with an LED (bright, on, mic on, phase). Tap or click toggles.
struct PanelSwitch: View {
    @Environment(ParameterStore.self) private var store
    @Environment(\.panelStyle) private var style

    let key: String
    var label: String?
    /// Text for the off and on positions; nil shows only the LED.
    var positions: (String, String)?

    var body: some View {
        if let info = store.info(key) {
            let on = store.isOn(key)
            Button {
                store.toggle(key)
            } label: {
                VStack(spacing: 5) {
                    Text(label ?? info.name)
                        .font(.system(size: 11, weight: .semibold))
                        .textCase(.uppercase)
                        .foregroundStyle(style.faceText)
                        .lineLimit(1)
                        .fixedSize()
                    ZStack(alignment: on ? .top : .bottom) {
                        RoundedRectangle(cornerRadius: 4)
                            .fill(Color.black.opacity(0.55))
                            .frame(width: 22, height: 36)
                        RoundedRectangle(cornerRadius: 3)
                            .fill(style.knob)
                            .overlay(RoundedRectangle(cornerRadius: 3).stroke(style.knobPointer.opacity(0.4)))
                            .frame(width: 18, height: 17)
                            .padding(2)
                    }
                    if let positions {
                        Text(on ? positions.1 : positions.0)
                            .font(.system(size: 10))
                            .foregroundStyle(style.faceText.opacity(0.85))
                            .fixedSize()
                    } else {
                        Led(on: on)
                    }
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(label ?? info.name)
            .accessibilityValue(positions.map { on ? $0.1 : $0.0 } ?? (on ? "On" : "Off"))
        }
    }
}

/// A block's on switch: a round footswitch with an LED above it, as on a pedal.
struct Footswitch: View {
    @Environment(ParameterStore.self) private var store
    @Environment(\.panelStyle) private var style

    let key: String

    var body: some View {
        if store.has(key) {
            let on = store.isOn(key)
            Button {
                store.toggle(key)
            } label: {
                VStack(spacing: 8) {
                    Led(on: on, size: 10)
                    Circle()
                        .fill(LinearGradient(colors: [Color(white: 0.85), Color(white: 0.45)],
                                             startPoint: .top, endPoint: .bottom))
                        .overlay(Circle().stroke(Color.black.opacity(0.6), lineWidth: 2))
                        .frame(width: 34, height: 34)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("On")
            .accessibilityValue(on ? "On" : "Off")
        }
    }
}

/// A selector for stepped parameters with more than two positions (room size, divisions).
struct StepSelector: View {
    @Environment(ParameterStore.self) private var store
    @Environment(\.panelStyle) private var style

    let key: String
    var label: String?
    /// One name per position; missing names fall back to the number.
    var names: [String] = []

    var body: some View {
        if let info = store.info(key) {
            let current = Int(store.value(key).rounded())
            let first = Int(info.minValue.rounded())
            VStack(spacing: 5) {
                Text(label ?? info.name)
                    .font(.system(size: 11, weight: .semibold))
                    .textCase(.uppercase)
                    .foregroundStyle(style.faceText)
                    .fixedSize()
                HStack(spacing: 2) {
                    ForEach(0..<info.stepCount, id: \.self) { i in
                        let v = first + i
                        Button {
                            store.set(key, Float(v))
                        } label: {
                            Text(i < names.count ? names[i] : String(v))
                                .font(.system(size: 11, weight: v == current ? .bold : .regular))
                                .foregroundStyle(v == current ? Color.black : style.faceText)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 5)
                                .background(RoundedRectangle(cornerRadius: 4)
                                    .fill(v == current ? Theme.accent : Color.black.opacity(0.35)))
                                .fixedSize()
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel(label ?? info.name)
        }
    }
}

struct Led: View {
    var on: Bool
    var size: CGFloat = 8

    var body: some View {
        Circle()
            .fill(on ? Theme.led : Theme.ledOff)
            .shadow(color: on ? Theme.led.opacity(0.8) : .clear, radius: on ? size * 0.6 : 0)
            .frame(width: size, height: size)
    }
}
