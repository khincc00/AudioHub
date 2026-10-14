import SwiftUI

// MARK: - Gaya

// Palet dan ukuran dipusatkan di sini. Warna aksen sendiri, tidak memakai identitas merek lain.
enum Style {
    static let accent = Color(red: 0.30, green: 0.82, blue: 0.74)
    static let warning = Color(red: 0.98, green: 0.72, blue: 0.30)
    static let danger = Color(red: 0.95, green: 0.35, blue: 0.35)
    static let surface = Color.white.opacity(0.045)
    static let surfaceStrong = Color.white.opacity(0.08)
    static let hairline = Color.white.opacity(0.08)
    static let muted = Color.secondary
    static let channelColors: [Color] = [
        Color(red: 0.30, green: 0.82, blue: 0.74),
        Color(red: 0.36, green: 0.62, blue: 0.98),
        Color(red: 0.98, green: 0.72, blue: 0.30),
        Color(red: 0.90, green: 0.40, blue: 0.60),
        Color(red: 0.70, green: 0.55, blue: 0.95),
        Color(red: 0.60, green: 0.85, blue: 0.40),
    ]
}

// Judul bagian kecil dengan huruf kapital, dipakai di setiap panel.
struct SectionTitle: View {
    let text: String
    var trailing: String? = nil

    init(_ text: String, trailing: String? = nil) {
        self.text = text
        self.trailing = trailing
    }

    var body: some View {
        HStack {
            Text(text)
                .font(.system(size: 11, weight: .semibold))
                .tracking(0.6)
                .foregroundStyle(.secondary)
            Spacer()
            if let t = trailing {
                Text(t).font(.system(size: 11)).foregroundStyle(.tertiary)
            }
        }
    }
}

// ScrollView biasa di aplikasi. Mode snapshot (AUDIOHUB_SNAPSHOT) memakai VStack agar isinya ikut tergambar.
struct AdaptiveScroll<Content: View>: View {
    let axes: Axis.Set
    @ViewBuilder let content: () -> Content

    init(_ axes: Axis.Set = .vertical, @ViewBuilder content: @escaping () -> Content) {
        self.axes = axes
        self.content = content
    }

    var body: some View {
        if ProcessInfo.processInfo.environment["AUDIOHUB_SNAPSHOT"] != nil {
            content()
        } else {
            ScrollView(axes) { content() }
        }
    }
}

// Pengganti segmented Picker: tab sederhana yang tampil konsisten di semua kondisi render.
struct SegmentedTabs<T: Hashable>: View {
    let items: [(T, String)]
    @Binding var selection: T

    var body: some View {
        HStack(spacing: 2) {
            ForEach(0..<items.count, id: \.self) { k in
                let value = items[k].0
                let active = selection == value
                Button {
                    selection = value
                } label: {
                    Text(items[k].1)
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(active ? Color.primary : Color.secondary)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 4)
                        .background(RoundedRectangle(cornerRadius: 5).fill(active ? Style.surfaceStrong : Color.clear))
                }
                .buttonStyle(.plain)
            }
        }
        .padding(2)
        .background(RoundedRectangle(cornerRadius: 7).fill(Style.surface))
    }
}

// Tombol kecil berbentuk huruf (S, M), dengan warna aktif.
struct ToggleChip: View {
    let label: String
    let on: Bool
    let tint: Color
    let help: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 11, weight: .bold))
                .frame(width: 22, height: 20)
                .foregroundStyle(on ? Color.black : Color.secondary)
                .background(RoundedRectangle(cornerRadius: 5).fill(on ? tint : Style.surfaceStrong))
        }
        .buttonStyle(.plain)
        .help(help)
    }
}

// MARK: - App

// Aplikasi tetap berjalan saat jendela ditutup, sehingga engine tidak ikut berhenti.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
}

@main
struct AudioHubApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var engine = EngineController()

    // Mode snapshot: `AudioHub --snapshot /path/file.png` merender tampilan ke PNG lalu keluar.
    private var snapshotPath: String? {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count else { return nil }
        return args[i + 1]
    }

    var body: some Scene {
        WindowGroup("AudioHub") {
            ContentView()
                .environmentObject(engine)
                .environmentObject(engine.meters)
                .frame(minWidth: 1120, minHeight: 720)
                .preferredColorScheme(.dark)
                .onAppear {
                    if let path = snapshotPath {
                        engine.open()
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
                            writeSnapshot(engine: engine, path: path)
                            NSApp.terminate(nil)
                        }
                    }
                }
        }
    }
}

@MainActor
func writeSnapshot(engine: EngineController, path: String) {
    let view = ContentView()
        .environmentObject(engine)
        .environmentObject(engine.meters)
        .frame(width: 1240, height: 780)
        .preferredColorScheme(.dark)
    let renderer = ImageRenderer(content: view)
    renderer.scale = 2
    guard let cg = renderer.cgImage else { return }
    let rep = NSBitmapImageRep(cgImage: cg)
    if let data = rep.representation(using: .png, properties: [:]) {
        try? data.write(to: URL(fileURLWithPath: path))
    }
}

// MARK: - Jendela utama

struct ContentView: View {
    @EnvironmentObject var engine: EngineController
    @State private var showGraph = false
    @State private var rightTab = 0

    var body: some View {
        VStack(spacing: 0) {
            topBar
            Divider()
            HStack(spacing: 0) {
                inputColumn
                    .frame(width: 280)
                Divider()
                routingColumn
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                Divider()
                sideColumn
                    .frame(width: 280)
            }
            .frame(maxHeight: .infinity)
            Divider()
            outputBar
            Divider()
            statusBar
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    // MARK: Toolbar

    private var topBar: some View {
        HStack(spacing: 14) {
            HStack(spacing: 8) {
                Image(systemName: "waveform")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(Style.accent)
                Text("AudioHub").font(.system(size: 15, weight: .semibold))
            }

            Divider().frame(height: 22)

            Picker("Perangkat", selection: $engine.deviceName) {
                ForEach(engine.devices.filter { $0.inCh > 0 && $0.outCh > 0 }, id: \.uid) { d in
                    Text(d.name).tag(d.name)
                }
                if !engine.devices.contains(where: { $0.name == engine.deviceName }) {
                    Text("\(engine.deviceName) (tidak tersedia)").tag(engine.deviceName)
                }
            }
            .labelsHidden()
            .frame(width: 230)

            Menu {
                Toggle("AudioHub Virtual", isOn: $engine.useVirtual)
                Toggle("AudioHub Virtual 2", isOn: $engine.useVirtual2)
            } label: {
                Label(virtualSummary, systemImage: "rectangle.connected.to.line.below")
            }
            .fixedSize()

            Button {
                engine.open()
            } label: {
                Label("Buka", systemImage: "arrow.clockwise")
            }
            .help("Buka ulang perangkat dengan pengaturan di atas")

            Spacer()

            statusPill

            if engine.running {
                Button {
                    engine.stop()
                } label: {
                    Label("Hentikan", systemImage: "stop.fill")
                }
                .tint(Style.danger)
                .keyboardShortcut(.defaultAction)
            } else {
                Button {
                    engine.start()
                } label: {
                    Label("Mulai", systemImage: "play.fill")
                }
                .tint(Style.accent)
                .keyboardShortcut(.defaultAction)
            }

            Menu {
                Toggle("Sambung ulang otomatis", isOn: $engine.autoReconnect)
                Divider()
                Button("Perbarui daftar perangkat") { engine.refreshDevices() }
            } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 14))
                    .foregroundStyle(.secondary)
            }
            .fixedSize()
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    private var virtualSummary: String {
        switch (engine.useVirtual, engine.useVirtual2) {
        case (true, true): return "Virtual 1 + 2"
        case (true, false): return "Virtual 1"
        case (false, true): return "Virtual 2"
        default: return "Tanpa virtual"
        }
    }

    private var statusPill: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(engine.running ? Style.accent : Color.secondary)
                .frame(width: 7, height: 7)
            Text(engine.running ? "Berjalan" : "Berhenti")
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(engine.running ? .primary : .secondary)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(Capsule().fill(Style.surface))
    }

    // MARK: Input

    private var inputColumn: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionTitle("Input", trailing: engine.isOpen ? "\(engine.inputs) kanal" : nil)
                .padding(.horizontal, 14)
                .padding(.top, 12)
            if engine.isOpen {
                AdaptiveScroll {
                    VStack(spacing: 8) {
                        ForEach(0..<engine.inputs, id: \.self) { i in
                            InputRow(index: i)
                        }
                    }
                    .padding(.horizontal, 10)
                    .padding(.bottom, 12)
                }
            } else {
                emptyState("Belum ada perangkat terbuka", "Pilih perangkat di atas, lalu tekan Buka.")
            }
        }
    }

    // MARK: Routing

    private var routingColumn: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                SectionTitle("Routing")
                Spacer()
                SegmentedTabs(items: [(false, "Matriks"), (true, "Graph")], selection: $showGraph)
                    .frame(width: 180)
            }
            .padding(.horizontal, 16)
            .padding(.top, 12)

            if engine.isOpen && showGraph {
                RouteGraphView()
                    .padding(.horizontal, 16)
                    .padding(.bottom, 12)
            } else if engine.isOpen {
                AdaptiveScroll([.horizontal, .vertical]) {
                    RouteMatrix()
                        .padding(16)
                }
                legend
                    .padding(.horizontal, 16)
                    .padding(.bottom, 12)
            } else {
                emptyState("Routing belum tersedia", "Buka perangkat untuk mengatur koneksi.")
            }
        }
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private var legend: some View {
        HStack(spacing: 14) {
            legendItem(Style.accent, "Tersambung")
            legendItem(Style.surfaceStrong, "Terputus")
            legendItem(Color.black.opacity(0.35), "Tidak tersedia (virtual → virtual)")
            Spacer()
            Text("Klik kanan untuk send").font(.caption2).foregroundStyle(.tertiary)
        }
        .font(.caption2)
        .foregroundStyle(.secondary)
    }

    private func legendItem(_ color: Color, _ text: String) -> some View {
        HStack(spacing: 5) {
            RoundedRectangle(cornerRadius: 3).fill(color).frame(width: 12, height: 9)
            Text(text)
        }
    }

    // MARK: Samping kanan

    private var sideColumn: some View {
        VStack(alignment: .leading, spacing: 12) {
            SegmentedTabs(items: [(0, "Preset"), (1, "Perangkat"), (2, "Diagnostik")], selection: $rightTab)
                .padding(.horizontal, 14)
                .padding(.top, 12)

            AdaptiveScroll {
                VStack(alignment: .leading, spacing: 16) {
                    switch rightTab {
                    case 1: DevicePanel()
                    case 2: DiagnosticsPanel()
                    default: PresetPanel()
                    }
                }
                .padding(14)
            }
        }
        .frame(maxHeight: .infinity, alignment: .top)
    }

    // MARK: Output

    private var outputBar: some View {
        HStack(alignment: .bottom, spacing: 0) {
            if engine.isOpen {
                ForEach(0..<engine.outputs, id: \.self) { j in
                    OutputStrip(index: j)
                        .frame(maxWidth: .infinity)
                    if j < engine.outputs - 1 { Divider() }
                }
            } else {
                emptyState("Output", "Muncul setelah perangkat dibuka.")
                    .frame(maxWidth: .infinity)
            }
        }
        .frame(height: 226)
        .padding(.horizontal, 12)
        .padding(.top, 10)
        .padding(.bottom, 8)
    }

    // MARK: Status

    private var statusBar: some View {
        HStack(spacing: 14) {
            StatusMetrics()
            Spacer()
            Text(engine.status)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 6)
    }

    private func emptyState(_ title: String, _ subtitle: String) -> some View {
        VStack(spacing: 4) {
            Text(title).font(.callout.weight(.medium))
            Text(subtitle).font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(20)
    }
}

// MARK: - Baris input

struct InputRow: View {
    let index: Int
    @EnvironmentObject var engine: EngineController

    var body: some View {
        let pair = index / 2
        let isPairStart = index % 2 == 0 && index + 1 < engine.inputs
        let isLinkedSecond = index % 2 == 1 && pair < engine.linkedPairs.count && engine.linkedPairs[pair]
        let color = Style.channelColors[engine.fx[index].colorIndex % Style.channelColors.count]

        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                // Penanda warna; klik untuk berganti warna
                RoundedRectangle(cornerRadius: 2)
                    .fill(color)
                    .frame(width: 3, height: 16)
                    .onTapGesture {
                        engine.updateInputFx(index) { $0.colorIndex = ($0.colorIndex + 1) % Style.channelColors.count }
                    }
                    .help("Klik untuk mengganti warna")

                TextField("Nama", text: Binding(get: { engine.inLabel(index) },
                                                set: { engine.setInputName(index, $0) }))
                    .textFieldStyle(.plain)
                    .font(.system(size: 12, weight: .medium))
                    .lineLimit(1)

                Spacer(minLength: 4)

                if isPairStart {
                    Button {
                        engine.setLink(pair, !engine.linkedPairs[pair])
                    } label: {
                        Image(systemName: engine.linkedPairs[pair] ? "link" : "link.badge.plus")
                            .foregroundStyle(engine.linkedPairs[pair] ? Style.accent : .secondary)
                    }
                    .buttonStyle(.plain)
                    .help("Tautkan pasangan stereo")
                }
                ToggleChip(label: "S", on: engine.soloInputs.contains(index), tint: Style.warning,
                           help: "Solo: hanya memengaruhi monitor") { engine.toggleSolo(index) }
                ToggleChip(label: "M", on: engine.mutes[index], tint: Style.danger,
                           help: "Bisukan kanal") { engine.setMute(index, !engine.mutes[index]) }
                DynamicsButton(index: index)
            }

            HStack(spacing: 8) {
                InputMeter(index: index)
                ClipDot(index: index)
            }

            HStack(spacing: 8) {
                Text("Gain").font(.system(size: 10)).foregroundStyle(.secondary).frame(width: 30, alignment: .leading)
                Slider(value: Binding(get: { engine.gains[index] }, set: { engine.setGain(index, $0) }), in: -60...12)
                    .controlSize(.small)
                    .tint(Style.accent)
                    .disabled(isLinkedSecond)
                Text(dbText(engine.gains[index]))
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 44, alignment: .trailing)
            }

            HStack(spacing: 8) {
                Text("Pan").font(.system(size: 10)).foregroundStyle(.secondary).frame(width: 30, alignment: .leading)
                Slider(value: Binding(get: { engine.fx[index].pan }, set: { engine.setPan(index, $0) }), in: -1...1)
                    .controlSize(.small)
                    .tint(Style.accent)
                Text(panText(engine.fx[index].pan))
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 44, alignment: .trailing)
            }
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8).fill(Style.surface))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(isLinkedSecond ? Style.accent.opacity(0.5) : Color.clear, lineWidth: 1)
        )
    }

    private func dbText(_ db: Float) -> String { String(format: "%+.1f", db) }

    private func panText(_ p: Float) -> String {
        if abs(p) < 0.01 { return "C" }
        return p < 0 ? String(format: "L%.0f", -p * 100) : String(format: "R%.0f", p * 100)
    }
}

// MARK: - Matriks

struct RouteMatrix: View {
    @EnvironmentObject var engine: EngineController

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Color.clear.frame(width: 130, height: 22)
                ForEach(0..<engine.outputs, id: \.self) { j in
                    Text(engine.outLabel(j))
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundStyle(j >= engine.physOut ? Color.secondary.opacity(0.8) : .secondary)
                        .lineLimit(1)
                        .frame(width: 84)
                        .padding(.leading, j == engine.physOut && engine.physOut > 0 ? 6 : 0)
                }
            }
            ForEach(0..<engine.inputs, id: \.self) { i in
                HStack(spacing: 6) {
                    Text(engine.inLabel(i))
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .frame(width: 130, alignment: .trailing)
                    ForEach(0..<engine.outputs, id: \.self) { j in
                        RouteCell(input: i, output: j)
                            .padding(.leading, j == engine.physOut && engine.physOut > 0 ? 6 : 0)
                    }
                }
                .padding(.top, i == engine.physIn && engine.physIn > 0 ? 6 : 0)
            }
        }
    }
}

struct RouteCell: View {
    let input: Int
    let output: Int
    @EnvironmentObject var engine: EngineController

    var body: some View {
        let blocked = input >= engine.physIn && output >= engine.physOut
        let on = engine.routes[input][output]
        let send = engine.sends[input][output]

        Button {
            engine.setRoute(input, output, !on)
        } label: {
            RoundedRectangle(cornerRadius: 6)
                .fill(blocked ? Color.black.opacity(0.35) : (on ? Style.accent : Style.surface))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(on ? Style.accent.opacity(0.9) : Style.hairline, lineWidth: 1)
                )
                .overlay(
                    Text(on && send != 0 ? String(format: "%.0f", send) : "")
                        .font(.system(size: 10, weight: .semibold).monospacedDigit())
                        .foregroundStyle(.black.opacity(0.75))
                )
                .frame(width: 84, height: 34)
        }
        .buttonStyle(.plain)
        .disabled(blocked)
        .contextMenu {
            Button("Send 0 dB") { engine.setSend(input, output, db: 0); engine.setRoute(input, output, true) }
            Button("Send −6 dB") { engine.setSend(input, output, db: -6); engine.setRoute(input, output, true) }
            Button("Send −12 dB") { engine.setSend(input, output, db: -12); engine.setRoute(input, output, true) }
            Divider()
            Button("Matikan koneksi") { engine.setRoute(input, output, false) }
        }
        .help(blocked ? "Virtual → virtual tidak bisa dihubungkan" : "\(engine.inLabel(input)) → \(engine.outLabel(output))")
    }
}

// MARK: - Panel samping

struct PresetPanel: View {
    @EnvironmentObject var engine: EngineController
    @State private var newName = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Preset tersimpan")
                Picker("", selection: Binding(get: { engine.currentPreset },
                                              set: { name in if !name.isEmpty { engine.loadPreset(name) } })) {
                    Text("Pilih preset").tag("")
                    ForEach(engine.presets, id: \.self) { Text($0).tag($0) }
                }
                .labelsHidden()
                .disabled(!engine.isOpen)
                HStack {
                    Button("Muat") { engine.loadPreset(engine.currentPreset) }
                        .disabled(engine.currentPreset.isEmpty)
                    Button("Hapus", role: .destructive) { engine.deletePreset(engine.currentPreset) }
                        .disabled(engine.currentPreset.isEmpty)
                    Spacer()
                }
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Simpan sebagai")
                HStack {
                    TextField("Nama preset", text: $newName)
                        .textFieldStyle(.roundedBorder)
                    Button("Simpan") {
                        engine.savePreset(named: newName)
                        newName = ""
                    }
                    .disabled(!engine.isOpen || newName.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Template")
                Text("Mengatur routing dan DSP mic sesuai skenario.")
                    .font(.caption).foregroundStyle(.secondary)
                Grid(horizontalSpacing: 8, verticalSpacing: 8) {
                    GridRow {
                        templateButton("Podcast")
                        templateButton("Streaming")
                    }
                    GridRow {
                        templateButton("Online Meeting")
                        templateButton("Recording")
                    }
                }
                Button("Reset ke default") { engine.resetDefaults() }
                    .disabled(!engine.isOpen)
            }
        }
    }

    private func templateButton(_ name: String) -> some View {
        Button(name) { engine.applyTemplate(name) }
            .frame(maxWidth: .infinity)
            .disabled(!engine.isOpen)
    }
}

struct DevicePanel: View {
    @EnvironmentObject var engine: EngineController
    @EnvironmentObject var meters: Meters

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Peran perangkat")
                ForEach(engine.currentBindings(), id: \.role) { b in
                    let ok = b.deviceUID.isEmpty || engine.devices.contains(where: { $0.uid == b.deviceUID })
                    HStack(spacing: 8) {
                        Circle().fill(ok ? Style.accent : Style.danger).frame(width: 6, height: 6)
                        Text(b.role).font(.caption)
                        Spacer()
                        Text(ok ? "terhubung" : "hilang")
                            .font(.caption2)
                            .foregroundStyle(ok ? Color.secondary : Style.danger)
                    }
                }
                if !engine.missingRoles.isEmpty {
                    Text("Tidak tersedia: \(engine.missingRoles.joined(separator: ", ")). Scene tidak diganti otomatis.")
                        .font(.caption2)
                        .foregroundStyle(Style.danger)
                }
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Parameter audio")
                row("Sample rate", meters.sampleRate > 0 ? String(format: "%.0f Hz", meters.sampleRate) : "—")
                row("Buffer", meters.bufferFrames > 0 ? "\(meters.bufferFrames) frame" : "—")
                row("Buffer", meters.bufferFrames > 0 && meters.sampleRate > 0
                    ? String(format: "%.1f ms", Double(meters.bufferFrames) / meters.sampleRate * 1000) : "—")
                row("Dropout", "\(meters.dropouts)")
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Latensi loopback terukur")
                row("AudioHub Virtual, buffer 192", "16 ms")
                row("PS22, buffer 512", "29 ms")
                Text("Hasil tes loopback sebelumnya, bukan pengukuran sesi ini.")
                    .font(.caption2).foregroundStyle(.secondary)
            }
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Spacer()
            Text(value).font(.caption.monospacedDigit())
        }
    }
}

struct DiagnosticsPanel: View {
    @EnvironmentObject var engine: EngineController
    @EnvironmentObject var meters: Meters

    var body: some View {
        let clips = meters.inClip.filter { $0 }.count + meters.outClip.filter { $0 }.count
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Ringkasan")
                row("Engine", engine.running ? "Berjalan" : "Berhenti")
                row("Perangkat", engine.isOpen ? "Terbuka" : "—")
                row("Scene aktif", engine.currentPreset.isEmpty ? "tanpa preset" : engine.currentPreset)
                row("Clip", "\(clips)")
                row("Dropout", "\(meters.dropouts)")
                row("Solo", engine.soloInputs.isEmpty ? "—" : "\(engine.soloInputs.count) kanal")
                row("Rekaman", engine.recordingOutput == nil ? "—" : "berjalan")
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                SectionTitle("Ekspor")
                Toggle("Sembunyikan nama dan UID perangkat", isOn: $engine.redactDiagnostics)
                    .font(.caption)
                    .toggleStyle(.checkbox)
                Button("Simpan laporan diagnostik") { engine.exportDiagnostics() }
                    .disabled(!engine.isOpen)
                Text("Laporan berisi routing, gain, dan status. Aman dibagikan bila nama perangkat disembunyikan.")
                    .font(.caption2).foregroundStyle(.secondary)
            }
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Spacer()
            Text(value).font(.caption.monospacedDigit()).lineLimit(1)
        }
    }
}

// MARK: - Output

struct OutputStrip: View {
    let index: Int
    @EnvironmentObject var engine: EngineController

    var body: some View {
        VStack(spacing: 6) {
            Text(engine.outLabel(index))
                .font(.system(size: 12, weight: .semibold))
                .lineLimit(1)
            Text("\(engine.sourceCount(index)) sumber")
                .font(.caption2)
                .foregroundStyle(.secondary)

            HStack(alignment: .center, spacing: 10) {
                VerticalFader(value: Binding(get: { engine.outputGains[index] },
                                             set: { engine.setOutputGain(index, $0) }))
                VStack(spacing: 4) {
                    ClipDot(output: index)
                    OutputMeter(index: index)
                }
            }
            .frame(height: 150)

            Text(String(format: "%+.1f dB", engine.outputGains[index]))
                .font(.system(size: 10).monospacedDigit())
                .foregroundStyle(.secondary)

            HStack(spacing: 6) {
                ToggleChip(label: "M", on: engine.outputMutes[index], tint: Style.danger,
                           help: "Bisukan output") { engine.setOutputMute(index, !engine.outputMutes[index]) }
                Button("0 dB") { engine.setOutputGain(index, 0) }
                    .font(.system(size: 10, weight: .medium))
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                LimiterButton(index: index)
                RecordButton(index: index)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }
}

// MARK: - Kontrol DSP per input

struct DynamicsButton: View {
    let index: Int
    @EnvironmentObject var engine: EngineController
    @State private var show = false

    var body: some View {
        Button {
            show = true
        } label: {
            Image(systemName: "slider.horizontal.3")
                .foregroundStyle(hasActiveDSP ? Style.accent : .secondary)
        }
        .buttonStyle(.plain)
        .help("Dinamika, EQ, delay, dan plugin")
        .popover(isPresented: $show, arrowEdge: .trailing) {
            InputDynamicsView(index: index)
                .environmentObject(engine)
                .frame(width: 380, height: 420)
        }
    }

    private var hasActiveDSP: Bool {
        let f = engine.fx[index]
        return f.gate.enabled || f.eq.enabled || f.comp.enabled || f.delay.enabled || !f.insert.isEmpty
    }
}

struct InputDynamicsView: View {
    let index: Int
    @EnvironmentObject var engine: EngineController

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(engine.inLabel(index)).font(.headline).padding(.horizontal, 16).padding(.top, 14)
            TabView {
                gateTab.tabItem { Text("Gate") }
                eqTab.tabItem { Text("EQ") }
                compTab.tabItem { Text("Kompresor") }
                delayTab.tabItem { Text("Delay") }
                pluginTab.tabItem { Text("Plugin") }
            }
            .padding(.horizontal, 12)
            .padding(.bottom, 12)
        }
    }

    private var f: ChannelFx { engine.fx[index] }

    private var gateTab: some View {
        Form {
            Toggle("Aktifkan gate", isOn: Binding(get: { f.gate.enabled },
                set: { v in engine.updateInputFx(index) { $0.gate.enabled = v } }))
            param("Threshold", f.gate.thresholdDb, -80...0, "dB") { v in engine.updateInputFx(index) { $0.gate.thresholdDb = v } }
            param("Release", f.gate.releaseMs, 5...1000, "ms") { v in engine.updateInputFx(index) { $0.gate.releaseMs = v } }
            param("Hold", f.gate.holdMs, 0...500, "ms") { v in engine.updateInputFx(index) { $0.gate.holdMs = v } }
        }
        .formStyle(.grouped)
    }

    private var eqTab: some View {
        Form {
            Toggle("Aktifkan EQ", isOn: Binding(get: { f.eq.enabled },
                set: { v in engine.updateInputFx(index) { $0.eq.enabled = v } }))
            param("Low shelf", f.eq.lowDb, -15...15, "dB") { v in engine.updateInputFx(index) { $0.eq.lowDb = v } }
            param("Mid peak", f.eq.midDb, -15...15, "dB") { v in engine.updateInputFx(index) { $0.eq.midDb = v } }
            param("High shelf", f.eq.highDb, -15...15, "dB") { v in engine.updateInputFx(index) { $0.eq.highDb = v } }
        }
        .formStyle(.grouped)
    }

    private var compTab: some View {
        Form {
            Toggle("Aktifkan kompresor", isOn: Binding(get: { f.comp.enabled },
                set: { v in engine.updateInputFx(index) { $0.comp.enabled = v } }))
            param("Threshold", f.comp.thresholdDb, -60...0, "dB") { v in engine.updateInputFx(index) { $0.comp.thresholdDb = v } }
            param("Rasio", f.comp.ratio, 1...20, ":1") { v in engine.updateInputFx(index) { $0.comp.ratio = v } }
            param("Knee", f.comp.kneeDb, 0...12, "dB") { v in engine.updateInputFx(index) { $0.comp.kneeDb = v } }
            param("Makeup", f.comp.makeupDb, 0...24, "dB") { v in engine.updateInputFx(index) { $0.comp.makeupDb = v } }
            Picker("Sidechain", selection: Binding(get: { f.comp.sidechainInput },
                set: { v in engine.updateInputFx(index) { $0.comp.sidechainInput = v } })) {
                Text("Tanpa sidechain").tag(-1)
                ForEach(0..<engine.inputs, id: \.self) { k in
                    if k != index { Text(engine.inLabel(k)).tag(k) }
                }
            }
        }
        .formStyle(.grouped)
    }

    private var delayTab: some View {
        Form {
            Toggle("Aktifkan delay", isOn: Binding(get: { f.delay.enabled },
                set: { v in engine.updateInputFx(index) { $0.delay.enabled = v } }))
            param("Waktu", f.delay.delayMs, 0...1000, "ms") { v in engine.updateInputFx(index) { $0.delay.delayMs = v } }
            Text("Delay menambah latensi sebesar waktunya.").font(.caption).foregroundStyle(.secondary)
        }
        .formStyle(.grouped)
    }

    private var pluginTab: some View {
        Form {
            Picker("Audio Unit", selection: Binding(get: { f.insert },
                set: { engine.setInsert(index, $0) })) {
                Text("Tanpa plugin").tag("")
                ForEach(engine.effectNames, id: \.self) { Text($0).tag($0) }
            }
            Text("Pemasangan plugin menghentikan audio sebentar. Parameter plugin belum bisa diatur dari aplikasi.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .formStyle(.grouped)
    }

    private func param(_ label: String, _ value: Float, _ range: ClosedRange<Float>, _ unit: String,
                       _ set: @escaping (Float) -> Void) -> some View {
        LabeledContent {
            HStack(spacing: 8) {
                Slider(value: Binding(get: { value }, set: set), in: range)
                    .frame(width: 180)
                Text(String(format: "%.1f %@", value, unit))
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 70, alignment: .trailing)
            }
        } label: {
            Text(label)
        }
    }
}

// MARK: - Limiter dan rekam output

struct LimiterButton: View {
    let index: Int
    @EnvironmentObject var engine: EngineController
    @State private var show = false

    var body: some View {
        let on = engine.outFx.indices.contains(index) && engine.outFx[index].enabled
        Button("Limiter") { show = true }
            .font(.system(size: 10, weight: .medium))
            .buttonStyle(.bordered)
            .controlSize(.small)
            .tint(on ? Style.accent : nil)
            .popover(isPresented: $show, arrowEdge: .top) {
                let f = engine.outFx[index]
                Form {
                    Toggle("Aktifkan limiter", isOn: Binding(get: { f.enabled },
                        set: { v in engine.updateOutputFx(index) { $0.enabled = v } }))
                    LabeledContent("Ceiling") {
                        HStack {
                            Slider(value: Binding(get: { f.ceilingDb }, set: { v in engine.updateOutputFx(index) { $0.ceilingDb = v } }),
                                   in: -12...0).frame(width: 160)
                            Text(String(format: "%.1f dB", f.ceilingDb)).font(.caption.monospacedDigit())
                                .frame(width: 60, alignment: .trailing)
                        }
                    }
                    LabeledContent("Lookahead") {
                        HStack {
                            Slider(value: Binding(get: { f.lookaheadMs }, set: { v in engine.updateOutputFx(index) { $0.lookaheadMs = v } }),
                                   in: 0...20).frame(width: 160)
                            Text(String(format: "%.1f ms", f.lookaheadMs)).font(.caption.monospacedDigit())
                                .frame(width: 60, alignment: .trailing)
                        }
                    }
                    Text("Lookahead menunda sinyal supaya batas tidak terlewati. Latensi bertambah sebesar nilainya.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                .formStyle(.grouped)
                .frame(width: 380, height: 260)
            }
    }
}

struct RecordButton: View {
    let index: Int
    @EnvironmentObject var engine: EngineController

    var body: some View {
        let on = engine.recordingOutput == index
        Button {
            if on { engine.stopRecording() } else { engine.startRecording(output: index) }
        } label: {
            Image(systemName: on ? "stop.circle.fill" : "record.circle")
                .foregroundStyle(on ? Style.danger : .secondary)
        }
        .buttonStyle(.plain)
        .help(on ? "Hentikan rekaman" : "Rekam output ini ke berkas WAV")
        .disabled(engine.recordingOutput != nil && !on)
    }
}

// MARK: - Graph

struct RouteGraphView: View {
    @EnvironmentObject var engine: EngineController

    var body: some View {
        GeometryReader { geo in
            let n = CGFloat(max(engine.inputs, 1))
            let m = CGFloat(max(engine.outputs, 1))
            let leftX: CGFloat = 150
            let rightX = geo.size.width - 150
            ZStack {
                Canvas { ctx, size in
                    for i in 0..<engine.inputs {
                        for j in 0..<engine.outputs where engine.routes[i][j] {
                            let p1 = CGPoint(x: leftX, y: (CGFloat(i) + 0.5) * size.height / n)
                            let p2 = CGPoint(x: rightX, y: (CGFloat(j) + 0.5) * size.height / m)
                            var path = Path()
                            path.move(to: p1)
                            let mid = (p1.x + p2.x) / 2
                            path.addCurve(to: p2, control1: CGPoint(x: mid, y: p1.y), control2: CGPoint(x: mid, y: p2.y))
                            ctx.stroke(path, with: .color(Style.accent), lineWidth: 2)
                        }
                    }
                }
                ForEach(0..<engine.inputs, id: \.self) { i in
                    Text(engine.inLabel(i))
                        .font(.caption.weight(.medium))
                        .frame(width: 140, alignment: .trailing)
                        .position(x: 80, y: (CGFloat(i) + 0.5) * geo.size.height / n)
                }
                ForEach(0..<engine.outputs, id: \.self) { j in
                    Text(engine.outLabel(j))
                        .font(.caption.weight(.medium))
                        .frame(width: 140, alignment: .leading)
                        .position(x: geo.size.width - 80, y: (CGFloat(j) + 0.5) * geo.size.height / m)
                }
            }
            .overlay(alignment: .bottom) {
                Text("Graph hanya menampilkan. Ubah koneksi di tampilan Matriks.")
                    .font(.caption2).foregroundStyle(.secondary).padding(6)
            }
        }
        .frame(minHeight: 300)
    }
}

// MARK: - Meter dan indikator

struct InputMeter: View {
    let index: Int
    @EnvironmentObject var meters: Meters

    var body: some View {
        HorizontalMeter(db: value(meters.inDB), hold: value(meters.inHold))
    }

    private func value(_ a: [Float]) -> Float { index < a.count ? a[index] : -120 }
}

struct OutputMeter: View {
    let index: Int
    @EnvironmentObject var meters: Meters

    var body: some View {
        VerticalMeter(db: value(meters.outDB), hold: value(meters.outHold))
    }

    private func value(_ a: [Float]) -> Float { index < a.count ? a[index] : -120 }
}

// Lampu clip. Menyala saat level mencapai batas; klik untuk menghapus.
struct ClipDot: View {
    var index: Int? = nil
    var output: Int? = nil
    @EnvironmentObject var meters: Meters

    init(index: Int) { self.index = index }
    init(output: Int) { self.output = output }

    var body: some View {
        let on = isOn
        Circle()
            .fill(on ? Style.danger : Color.white.opacity(0.10))
            .frame(width: 7, height: 7)
            .onTapGesture {
                meters.clearClip(input: index, output: output)
            }
            .help("Clip. Klik untuk menghapus")
    }

    private var isOn: Bool {
        if let i = index { return i < meters.inClip.count && meters.inClip[i] }
        if let j = output { return j < meters.outClip.count && meters.outClip[j] }
        return false
    }
}

struct HorizontalMeter: View {
    let db: Float
    var hold: Float = -120

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 2).fill(Color.black.opacity(0.35))
                RoundedRectangle(cornerRadius: 2)
                    .fill(color)
                    .frame(width: geo.size.width * level(db))
                Rectangle()
                    .fill(Color.white.opacity(0.85))
                    .frame(width: 1.5)
                    .offset(x: geo.size.width * level(hold))
                    .opacity(hold > -60 ? 1 : 0)
            }
        }
        .frame(height: 6)
    }

    private func level(_ v: Float) -> CGFloat { CGFloat(min(max((v + 60) / 60, 0), 1)) }
    private var color: Color { db > -1 ? Style.danger : (db > -12 ? Style.warning : Style.accent) }
}

struct VerticalMeter: View {
    let db: Float
    var hold: Float = -120

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 2).fill(Color.black.opacity(0.35))
                RoundedRectangle(cornerRadius: 2)
                    .fill(color)
                    .frame(height: geo.size.height * level(db))
                Rectangle()
                    .fill(Color.white.opacity(0.85))
                    .frame(height: 1.5)
                    .offset(y: -(geo.size.height * level(hold)))
                    .opacity(hold > -60 ? 1 : 0)
            }
        }
        .frame(width: 8, height: 150)
    }

    private func level(_ v: Float) -> CGFloat { CGFloat(min(max((v + 60) / 60, 0), 1)) }
    private var color: Color { db > -1 ? Style.danger : (db > -12 ? Style.warning : Style.accent) }
}

struct VerticalFader: View {
    @Binding var value: Float

    var body: some View {
        Slider(value: $value, in: -60...12)
            .controlSize(.small)
            .tint(Style.accent)
            .frame(width: 150)
            .rotationEffect(.degrees(-90))
            .frame(width: 22, height: 150)
    }
}

// Bilah status: buffer, callback, dan dropout.
struct StatusMetrics: View {
    @EnvironmentObject var meters: Meters

    var body: some View {
        HStack(spacing: 12) {
            metric("Buffer", meters.bufferFrames > 0 ? "\(meters.bufferFrames)" : "—")
            metric("Callback", "\(meters.callbacks)")
            HStack(spacing: 4) {
                Text("Dropout").font(.caption2).foregroundStyle(.secondary)
                Text("\(meters.dropouts)")
                    .font(.caption.monospacedDigit().weight(.semibold))
                    .foregroundStyle(meters.dropouts > 0 ? Style.danger : .primary)
            }
        }
    }

    private func metric(_ label: String, _ value: String) -> some View {
        HStack(spacing: 4) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text(value).font(.caption.monospacedDigit())
        }
    }
}
