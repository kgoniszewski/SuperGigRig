import Foundation

/// The sections of the rig, in signal-chain order (docs/architecture.md, sections 4 and 7).
/// Verb and Trem is left out because it is not in version 1; adding it later is one more
/// case here and one block list.
enum RigSection: String, CaseIterable, Identifiable {
    case preFX, amp, cab, eq, postFX

    var id: String { rawValue }

    var title: String {
        switch self {
        case .preFX: return "Pre FX"
        case .amp: return "Amp"
        case .cab: return "Cab"
        case .eq: return "EQ"
        case .postFX: return "Post FX"
        }
    }

    var systemImage: String {
        switch self {
        case .preFX: return "rectangle.portrait.on.rectangle.portrait"
        case .amp: return "hifispeaker"
        case .cab: return "square.grid.3x3.square"
        case .eq: return "slider.vertical.3"
        case .postFX: return "rectangle.stack"
        }
    }

    /// The blocks of the section, in chain order. A block appears only once the core has its
    /// parameters, so sections fill in as work packages land.
    var blocks: [RigBlock] {
        switch self {
        case .preFX:
            return [RigBlock(prefix: "boost", title: "Boost"),
                    RigBlock(prefix: "envfilter", title: "Env Filter"),
                    RigBlock(prefix: "goldod", title: "Gold OD"),
                    RigBlock(prefix: "twinod", title: "Twin OD"),
                    RigBlock(prefix: "bucketdelay", title: "Bucket Delay")]
        case .amp:
            return RigAmp.all.map { RigBlock(prefix: $0.prefix, title: $0.name) }
        case .cab:
            return [RigBlock(prefix: "cab", title: "Cabinet")]
        case .eq:
            return [RigBlock(prefix: "eq", title: "EQ")]
        case .postFX:
            return [RigBlock(prefix: "delay", title: "Digital Delay"),
                    RigBlock(prefix: "reverb", title: "Studio Reverb")]
        }
    }

    /// The switch that lights the section's icon: the section's own on switch where it has
    /// one, otherwise any of its blocks being on.
    var onKeys: [String] {
        switch self {
        case .amp: return ["amp.on"]
        case .cab: return ["cab.on"]
        default: return blocks.map { $0.prefix + ".on" }
        }
    }
}

struct RigBlock: Identifiable {
    let prefix: String
    let title: String
    var id: String { prefix }
}

/// The amp models, in the order of the amp selector. Each one shows in the selector once the
/// core has its parameters. Work packages 6 and 7 add Steel 100 and Proto 50 to the core and
/// the parameter "amp.model" that chooses between them.
struct RigAmp: Identifiable {
    let model: Int       // value of amp.model
    let prefix: String   // parameter key prefix of its front panel
    let name: String
    var id: Int { model }

    static let all = [RigAmp(model: 0, prefix: "combo40", name: "Combo 40"),
                      RigAmp(model: 1, prefix: "steel100", name: "Steel 100"),
                      RigAmp(model: 2, prefix: "proto50", name: "Proto 50")]
}

/// Names of switch and selector positions, by parameter key, for the generic block views.
/// Positions follow the comments in gigrig_params.h.
enum PositionNames {
    static let byKey: [String: [String]] = [
        "boost.mode": ["Clean", "Drive"],
        "envfilter.direction": ["Up", "Down"],
        "envfilter.speed": ["Fast", "Slow"],
        "envfilter.range": ["Lo", "Hi"],
        "twinod.mode": ["Mid", "Amp"],
        "cab.room.size": ["Small", "Medium", "Large"],
        "cab.mic1.phase": ["Normal", "Invert"],
        "cab.mic2.phase": ["Normal", "Invert"],
        "delay.sync": ["Free", "Tempo"],
        "reverb.mode": ["Hall", "Plate"],
        "steel100.input": ["Normal", "FET"],
    ]
}
