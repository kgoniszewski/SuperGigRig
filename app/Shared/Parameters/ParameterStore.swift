import Foundation
import Observation

/// One entry of the DSP core's parameter table (dsp/include/gigrig/gigrig_params.h).
struct ParameterInfo: Identifiable {
    enum Taper { case linear, log, stepped }

    let id: Int
    let key: String
    let name: String
    let unit: String
    let minValue: Float
    let maxValue: Float
    let defaultValue: Float
    let taper: Taper

    /// Whole-number positions for stepped parameters (switches and selectors), else 0.
    var stepCount: Int { taper == .stepped ? Int((maxValue - minValue).rounded()) + 1 : 0 }
}

/// The current value of every parameter of the rig (docs/architecture.md, section 7,
/// "Parameter system"). A control writes here, this store writes the core's atomic value,
/// and the core smooths it on the audio thread. There is no other path from UI to sound.
///
/// The table itself is read from the core, so ranges and defaults can never disagree with
/// the DSP. Values are remembered between launches until presets arrive (work package 15).
@MainActor
@Observable
final class ParameterStore {
    let infos: [ParameterInfo]
    private(set) var values: [Float]

    private let engine: GRAudioEngine
    private let idsByKey: [String: Int]
    @ObservationIgnored private var saveTask: Task<Void, Never>?
    private static let savedValuesKey = "parameters.lastValues"

    init(engine: GRAudioEngine) {
        self.engine = engine
        var infos: [ParameterInfo] = []
        let count = Int(gigrig_param_count())
        for id in 0..<count {
            var raw = GigRigParamInfo()
            guard gigrig_param_info(Int32(id), &raw) == GIGRIG_OK else { continue }
            let taper: ParameterInfo.Taper
            switch Int(raw.taper.rawValue) {
            case Int(GIGRIG_TAPER_LOG.rawValue): taper = .log
            case Int(GIGRIG_TAPER_STEPPED.rawValue): taper = .stepped
            default: taper = .linear
            }
            infos.append(ParameterInfo(id: id,
                                       key: String(cString: raw.key),
                                       name: String(cString: raw.name),
                                       unit: String(cString: raw.unit),
                                       minValue: raw.min_value,
                                       maxValue: raw.max_value,
                                       defaultValue: raw.default_value,
                                       taper: taper))
        }
        self.infos = infos
        var byKey: [String: Int] = [:]
        for info in infos { byKey[info.key] = info.id }
        idsByKey = byKey

        // Restore the last session. Keys that no longer exist are dropped; new ones keep
        // their defaults. The core clamps anything out of range.
        let saved = UserDefaults.standard.dictionary(forKey: Self.savedValuesKey) as? [String: Double] ?? [:]
        for (key, value) in saved {
            if let id = byKey[key] { engine.setParameter(id, value: Float(value)) }
        }
        values = infos.map { engine.parameterValue($0.id) }
    }

    // MARK: Lookup

    func info(_ key: String) -> ParameterInfo? {
        guard let id = idsByKey[key] else { return nil }
        return infos[id]
    }

    func has(_ key: String) -> Bool { idsByKey[key] != nil }

    /// Keys of all parameters whose key starts with "<prefix>.", in table order.
    func keys(withPrefix prefix: String) -> [String] {
        infos.filter { $0.key.hasPrefix(prefix + ".") }.map(\.key)
    }

    // MARK: Values

    func value(_ key: String) -> Float {
        guard let id = idsByKey[key] else { return 0 }
        return values[id]
    }

    func isOn(_ key: String) -> Bool { value(key) >= 0.5 }

    func set(_ key: String, _ value: Float) {
        guard let id = idsByKey[key] else { return }
        engine.setParameter(id, value: value)
        // Read back what the core kept (clamped, rounded for switches), so the UI shows it.
        let kept = engine.parameterValue(id)
        if values[id] != kept { values[id] = kept }
        scheduleSave()
    }

    func toggle(_ key: String) { set(key, isOn(key) ? 0 : 1) }

    func reset(_ key: String) {
        guard let info = info(key) else { return }
        set(key, info.defaultValue)
    }

    /// Knob position, 0 to 1, following the parameter's taper.
    func normalized(_ key: String) -> Double {
        guard let id = idsByKey[key] else { return 0 }
        return Double(gigrig_param_to_normalized(Int32(id), values[id]))
    }

    func setNormalized(_ key: String, _ position: Double) {
        guard let id = idsByKey[key] else { return }
        let clamped = Float(min(1, max(0, position)))
        set(key, gigrig_param_from_normalized(Int32(id), clamped))
    }

    // MARK: Display

    /// The value as the knob readout shows it, for example "+3.0 dB", "6.5", "350 ms".
    func displayText(_ key: String) -> String {
        guard let info = info(key) else { return "" }
        return Self.format(value(key), info: info)
    }

    static func format(_ v: Float, info: ParameterInfo) -> String {
        switch info.unit {
        case "dB":
            return String(format: "%+.1f dB", v)
        case "Hz":
            return v >= 1000 ? String(format: "%.2f kHz", v / 1000) : String(format: "%.0f Hz", v)
        case "ms":
            return String(format: "%.0f ms", v)
        case "s":
            return String(format: "%.1f s", v)
        case "%":
            return String(format: "%.0f %%", v)
        case "":
            return info.taper == .stepped ? String(Int(v.rounded())) : String(format: "%.1f", v)
        default:
            return String(format: "%.1f %@", v, info.unit)
        }
    }

    // MARK: Persistence

    /// Writes all values half a second after the last change, so a knob drag saves once.
    private func scheduleSave() {
        saveTask?.cancel()
        saveTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 500_000_000)
            guard let self, !Task.isCancelled else { return }
            var dict: [String: Double] = [:]
            for info in self.infos { dict[info.key] = Double(self.values[info.id]) }
            UserDefaults.standard.set(dict, forKey: Self.savedValuesKey)
        }
    }
}
