import SwiftUI

@main
struct SuperGigRigApp: App {
    @State private var audio = AudioController()

    var body: some Scene {
        WindowGroup {
            RigView()
                .environment(audio)
                .task { await audio.startOnLaunch() }
        }
        #if os(macOS)
        .defaultSize(width: 1200, height: 780)
        #endif
    }
}
