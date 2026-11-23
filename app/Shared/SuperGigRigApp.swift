import SwiftUI

@main
struct SuperGigRigApp: App {
    @State private var audio = AudioController()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(audio)
                .task { await audio.startOnLaunch() }
        }
        #if os(macOS)
        .defaultSize(width: 760, height: 560)
        #endif
    }
}
