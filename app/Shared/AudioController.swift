import AVFoundation
import Foundation
import Observation

/// SwiftUI's view of the audio engine. Owns the Objective-C++ bridge (GRAudioEngine),
/// keeps the audio settings in UserDefaults and mirrors the engine state for the views.
/// Main actor only, like the bridge itself.
@MainActor
@Observable
final class AudioController {
    enum Permission { case unknown, granted, denied }

    static let sampleRates: [Double] = [44_100, 48_000, 88_200, 96_000]
    static let bufferSizes: [Int] = [32, 64, 128, 256]

    /// First-launch sample rate: 96 kHz on both devices. The interface and the OS keep
    /// buffers they do not report, fixed in samples, so a higher rate shrinks them in time.
    /// Only 96 kHz / 32 met the 5 ms target on both (docs/latency.md).
    static let defaultSampleRate: Double = 96_000

    private let engine = GRAudioEngine()
    private let defaults = UserDefaults.standard

    private(set) var state: GREngineState = .stopped
    private(set) var statusMessage = "Stopped"
    private(set) var device: GRAudioDevice?
    private(set) var latency: GRLatencyReport?
    private(set) var devices: [GRAudioDevice] = []
    private(set) var permission: Permission = .unknown
    private(set) var loopbackPhase: GRLoopbackPhase = .idle
    /// Measured round trips in frames; -1 marks a click that never came back, -2 a too noisy input.
    private(set) var loopbackResults: [Int] = []
    private(set) var loopbackRate: Double = 0

    /// Empty string means automatic (the AXE I/O One when present). macOS only.
    var preferredDeviceUID: String {
        didSet {
            defaults.set(preferredDeviceUID, forKey: Keys.device)
            engine.preferredDeviceUID = preferredDeviceUID.isEmpty ? nil : preferredDeviceUID
        }
    }
    var sampleRate: Double {
        didSet {
            defaults.set(sampleRate, forKey: Keys.rate)
            engine.preferredSampleRate = sampleRate
        }
    }
    var bufferFrames: Int {
        didSet {
            defaults.set(bufferFrames, forKey: Keys.buffer)
            engine.preferredBufferFrames = bufferFrames
        }
    }
    /// Zero-based: 0 is input 1, the instrument input on the AXE I/O One.
    var inputChannel: Int {
        didSet {
            defaults.set(inputChannel, forKey: Keys.input)
            engine.inputChannel = inputChannel
        }
    }
    var outputToBoth: Bool {
        didSet {
            defaults.set(outputToBoth, forKey: Keys.outputBoth)
            engine.outputToBothChannels = outputToBoth
        }
    }

    private enum Keys {
        static let device = "audio.deviceUID"
        static let rate = "audio.sampleRate"
        static let buffer = "audio.bufferFrames"
        static let input = "audio.inputChannel"
        static let outputBoth = "audio.outputToBoth"
    }

    init() {
        let d = UserDefaults.standard
        preferredDeviceUID = d.string(forKey: Keys.device) ?? ""
        let rate = d.double(forKey: Keys.rate)
        sampleRate = Self.sampleRates.contains(rate) ? rate : Self.defaultSampleRate
        let buffer = d.integer(forKey: Keys.buffer)
        bufferFrames = Self.bufferSizes.contains(buffer) ? buffer : 32
        inputChannel = max(0, d.integer(forKey: Keys.input))
        outputToBoth = d.bool(forKey: Keys.outputBoth)

        // didSet does not run during init, so hand the settings to the engine here.
        engine.preferredDeviceUID = preferredDeviceUID.isEmpty ? nil : preferredDeviceUID
        engine.preferredSampleRate = sampleRate
        engine.preferredBufferFrames = bufferFrames
        engine.inputChannel = inputChannel
        engine.outputToBothChannels = outputToBoth
        engine.onChange = { [weak self] in self?.refresh() }
        refresh()
    }

    var isRunning: Bool { state == .running }

    /// Number of input channels to offer in Settings (at least two: the AXE I/O One has two).
    var inputChannelCount: Int { max(2, device?.inputChannels ?? 2) }

    func startOnLaunch() async {
        await requestPermission()
        if permission == .granted { engine.start() }
    }

    func start() {
        Task {
            await requestPermission()
            if permission == .granted { engine.start() }
        }
    }

    func stop() { engine.stop() }

    func refreshDevices() { devices = engine.availableDevices() }

    private func refresh() {
        state = engine.state
        statusMessage = engine.statusMessage
        device = engine.currentDevice
        latency = engine.latency
        devices = engine.availableDevices()
    }

    // MARK: Permission

    private func requestPermission() async {
        #if os(macOS)
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            permission = .granted
        case .notDetermined:
            permission = await AVCaptureDevice.requestAccess(for: .audio) ? .granted : .denied
        default:
            permission = .denied
        }
        #else
        switch AVAudioApplication.shared.recordPermission {
        case .granted:
            permission = .granted
        case .undetermined:
            permission = await AVAudioApplication.requestRecordPermission() ? .granted : .denied
        default:
            permission = .denied
        }
        #endif
    }

    // MARK: Loopback test

    /// Plays clicks on the output and measures their return on the selected input.
    /// Needs a cable from output 1 to the selected input.
    func measureLoopback(pings: Int = 5) {
        guard isRunning, loopbackPhase != .measuring else { return }
        loopbackRate = latency?.sampleRate ?? sampleRate
        loopbackResults = []
        engine.startLoopbackMeasurement(withPings: pings)
        loopbackPhase = .measuring
        Task { [weak self] in
            // Poll at 20 Hz: the audio thread publishes results through atomics only.
            while let self, self.loopbackPhase == .measuring {
                try? await Task.sleep(nanoseconds: 50_000_000)
                self.loopbackResults = self.engine.loopbackResults().map { $0.intValue }
                let phase = self.engine.loopbackPhase
                if phase != .measuring || !self.isRunning {
                    self.loopbackPhase = phase == .measuring ? .idle : phase
                }
            }
        }
    }

    // MARK: Cabinet (work package 3; the real Cab screen comes in work package 5)

    /// File name of the IR last loaded into each mic slot, or "" for none.
    private(set) var cabIRNames: [String] = ["", ""]
    /// Length of the IR playing in each slot, in samples at the running rate (0 = empty).
    private(set) var cabIRLengths: [Int] = [0, 0]
    private(set) var cabMessage: String?

    /// The factory IRs bundled from irs/ (generated, see irs/LICENSES.md).
    static let factoryIRs: [URL] = (Bundle.main.urls(forResourcesWithExtension: "wav", subdirectory: nil) ?? [])
        .sorted { $0.lastPathComponent < $1.lastPathComponent }

    var cabOn: Bool {
        get { _ = cabTick; return engine.cabinetOn }
        set { engine.cabinetOn = newValue; cabTick += 1 }
    }
    var mic2On: Bool {
        get { _ = cabTick; return engine.cabinetMic2On }
        set { engine.cabinetMic2On = newValue; cabTick += 1 }
    }
    /// Bumped when a cabinet setting changes, so views reading the computed properties refresh.
    private var cabTick = 0

    func loadCabIR(_ url: URL, slot: Int) {
        cabMessage = nil
        engine.loadCabinetIR(from: url, slot: slot) { [weak self] error in
            guard let self else { return }
            if let error {
                self.cabMessage = error
            } else {
                self.cabIRNames[slot] = url.deletingPathExtension().lastPathComponent
                if slot == 1 { self.mic2On = true }
            }
            self.refreshCabLengths(after: 0.1)
        }
    }

    func clearCabIR(slot: Int) {
        engine.clearCabinetIR(inSlot: slot)
        cabIRNames[slot] = ""
        refreshCabLengths(after: 0.1)
    }

    /// The length changes on the audio thread once the 20 ms crossfade is done.
    private func refreshCabLengths(after seconds: Double) {
        Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
            guard let self else { return }
            self.cabIRLengths = (0..<2).map { self.engine.cabinetIRLength(inSlot: $0) }
        }
    }

    /// Result code of a ping whose input was too noisy (LoopbackProbe::kTooNoisy).
    static let loopbackTooNoisy = -2

    /// Largest minus smallest successful ping, in frames; 0 with fewer than two.
    var loopbackSpreadFrames: Int {
        let ok = loopbackResults.filter { $0 >= 0 }
        guard let lo = ok.min(), let hi = ok.max() else { return 0 }
        return hi - lo
    }

    /// Median of the successful pings, in frames.
    var loopbackMedianFrames: Int? {
        let ok = loopbackResults.filter { $0 >= 0 }.sorted()
        guard !ok.isEmpty else { return nil }
        return ok[ok.count / 2]
    }
}

/// Formats a frame count as milliseconds at `rate`.
func formatMs(_ frames: Int, rate: Double) -> String {
    guard rate > 0 else { return "-" }
    return String(format: "%.2f ms", 1000.0 * Double(frames) / rate)
}
