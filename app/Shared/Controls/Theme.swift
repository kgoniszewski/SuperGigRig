import SwiftUI

/// Placeholder look for version 1 (work package 5): a warm, dark stage with simple vector
/// panels. The final artwork comes in work package 16 and only needs to change this file and
/// the panel views, not the controls' behaviour.
enum Theme {
    static let stage = Color(red: 0.11, green: 0.09, blue: 0.08)
    static let stageLight = Color(red: 0.20, green: 0.16, blue: 0.13)
    static let bar = Color(red: 0.07, green: 0.06, blue: 0.05)
    static let barText = Color(red: 0.90, green: 0.86, blue: 0.80)
    static let dimText = Color(red: 0.62, green: 0.57, blue: 0.51)
    static let accent = Color(red: 0.95, green: 0.66, blue: 0.26)
    static let led = Color(red: 1.00, green: 0.25, blue: 0.18)
    static let ledOff = Color(red: 0.30, green: 0.10, blue: 0.08)

    /// The design size of the whole rig. The window scales this canvas as one piece.
    static let canvas = CGSize(width: 1200, height: 780)
}

/// Colours of one front panel and its knobs.
struct PanelStyle {
    var face: Color
    var faceText: Color
    var knob: Color
    var knobPointer: Color
    var frame: Color

    /// Cream panel with black knobs, for the combo amp.
    static let combo = PanelStyle(face: Color(red: 0.85, green: 0.82, blue: 0.74),
                                  faceText: Color(red: 0.12, green: 0.11, blue: 0.10),
                                  knob: Color(red: 0.10, green: 0.10, blue: 0.10),
                                  knobPointer: .white,
                                  frame: Color(red: 0.14, green: 0.13, blue: 0.12))
    /// Dark brushed panel with silver knobs, for the heads.
    static let head = PanelStyle(face: Color(red: 0.16, green: 0.16, blue: 0.17),
                                 faceText: Color(red: 0.88, green: 0.86, blue: 0.82),
                                 knob: Color(red: 0.72, green: 0.72, blue: 0.70),
                                 knobPointer: .black,
                                 frame: Color(red: 0.05, green: 0.05, blue: 0.05))
    /// Pedal and rack units in the generic section views.
    static let pedal = PanelStyle(face: Color(red: 0.26, green: 0.27, blue: 0.29),
                                  faceText: Color(red: 0.92, green: 0.90, blue: 0.86),
                                  knob: Color(red: 0.08, green: 0.08, blue: 0.08),
                                  knobPointer: Color(red: 0.95, green: 0.66, blue: 0.26),
                                  frame: Color(red: 0.10, green: 0.10, blue: 0.11))
}

private struct PanelStyleKey: EnvironmentKey {
    static let defaultValue = PanelStyle.pedal
}

extension EnvironmentValues {
    var panelStyle: PanelStyle {
        get { self[PanelStyleKey.self] }
        set { self[PanelStyleKey.self] = newValue }
    }
}
