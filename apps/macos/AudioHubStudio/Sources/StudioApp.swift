import AppKit
import SwiftUI
import UniformTypeIdentifiers

// AudioHub Studio: tampilan dan routing AudioHub, ditambah rantai FX per track gaya REAPER.
// Memakai ContentView dan Model.swift yang sama dengan AudioHub (dikompilasi dengan AUDIOHUB_STUDIO).
@main
struct AudioHubStudioApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var engine: EngineController
    @StateObject private var chains: StudioChains
    @AppStorage("appearance") private var appearance = AppearanceMode.system.rawValue

    init() {
        let engine = EngineController()
        _engine = StateObject(wrappedValue: engine)
        _chains = StateObject(wrappedValue: StudioChains(engine: engine))
    }

    var body: some Scene {
        WindowGroup("AudioHub Studio") {
            ContentView()
                .environmentObject(engine)
                .environmentObject(engine.meters)
                .frame(minWidth: 1120, minHeight: 720)
                .preferredColorScheme(AppearanceMode(rawValue: appearance)?.colorScheme)
                .environment(\.fxChainHost, FxChainHost(
                    open: { chains.openEditor($0) },
                    count: { chains.chain($0).count }
                ))
                .sheet(isPresented: $chains.editorPresented) {
                    StudioChainSheet(engine: engine, chains: chains)
                        .frame(width: 620, height: 560)
                }
        }
        .commands {
            CommandMenu("Proyek") {
                Button("Skema Live TikTok (VB1 → VB2)") {
                    engine.status = engine.applyLiveTikTokTemplate()
                }
                Divider()
                Button("Buka Proyek…") { openProject() }
                    .keyboardShortcut("o")
                Button("Simpan Proyek…") { saveProject() }
                    .keyboardShortcut("s")
            }
        }
    }

    private func openProject() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url { chains.loadProject(from: url) }
    }

    private func saveProject() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "proyek.audiohub.json"
        if panel.runModal() == .OK, let url = panel.url { chains.saveProject(to: url) }
    }
}
