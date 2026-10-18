import AppKit
import Combine
import Foundation
import UniformTypeIdentifiers

// Satu slot FX pada satu track (input). Slot fisik 0..7; urutan proses diatur terpisah di StudioChains.order.
struct FxSlot {
    var name = ""                              // nama Audio Unit; kosong = slot kosong
    var enabled = true                         // false = bypass: dilewati, tetap ada di chain
    var loaded = false                         // AU berhasil dimuat di engine (runtime, tidak disimpan)
    var state: Data? = nil                     // state AU penuh, diambil saat proyek disimpan
    var paramValues: [UInt32: Float] = [:]     // nilai parameter yang diubah pengguna
    var isEmpty: Bool { name.isEmpty }
}

struct FxParamRow: Identifiable {
    let id: UInt32
    let name: String
    let minValue: Float
    let maxValue: Float
    var value: Float
}

// Berkas proyek AudioHub Studio (JSON). Routing dan gain ikut disimpan, rantai FX per track ada di chains.
struct StudioProjectFile: Codable {
    var schemaVersion = 1
    var deviceName: String
    var routes: [[Bool]]
    var sends: [[Float]]
    var gains: [Float]
    var mutes: [Bool]
    var outputGains: [Float]
    var outputMutes: [Bool]
    var chains: [ChainFile]
}

struct ChainFile: Codable {
    var input: Int
    var order: [Int]
    var slots: [SlotFile?]
}

struct SlotFile: Codable {
    var name: String
    var enabled: Bool
    var state: Data?
    var paramValues: [String: Float]
}

// Rantai FX per track. Model ini memegang status yang disimpan; engine (AudioHub) memegang perangkat dan routing.
final class StudioChains: ObservableObject {
    static let maxInputs = 8      // = kMaxCh di backend
    static let slotCount = 8      // = AH_FX_SLOTS di backend

    let engine: EngineController
    @Published private(set) var slots: [[FxSlot]]
    @Published private(set) var order: [[Int]]
    @Published var selectedInput = 0
    @Published private(set) var selectedSlot: Int? = nil
    @Published private(set) var params: [FxParamRow] = []
    // Sheet editor rantai yang sedang tampil.
    @Published var editorPresented = false

    private var cancellable: AnyCancellable?

    init(engine: EngineController) {
        self.engine = engine
        slots = Array(repeating: Array(repeating: FxSlot(), count: Self.slotCount), count: Self.maxInputs)
        order = Array(repeating: Array(0..<Self.slotCount), count: Self.maxInputs)
        // Setiap perangkat dibuka (termasuk sambung ulang) → muat ulang rantai ke handle baru.
        cancellable = engine.$ioGeneration.dropFirst().sink { [weak self] _ in self?.restoreChains() }
    }

    // Slot yang berisi FX, sesuai urutan proses.
    func chain(_ input: Int) -> [Int] {
        guard input >= 0, input < Self.maxInputs else { return [] }
        return order[input].filter { !slots[input][$0].isEmpty }
    }

    func openEditor(_ input: Int) {
        selectedInput = input
        selectedSlot = nil
        params = []
        editorPresented = true
    }

    // MARK: - Edit rantai

    func addEffect(_ name: String) {
        let i = selectedInput
        guard let free = (0..<Self.slotCount).first(where: { slots[i][$0].isEmpty }) else {
            engine.status = "Chain \(engine.inLabel(i)) penuh (\(Self.slotCount) FX)"
            return
        }
        let ok = whileStopped { h in ah_io_fx_load(h, Int32(i), Int32(free), name) == 0 }
        guard ok else {
            engine.status = "Gagal memuat \(name)"
            return
        }
        var slot = FxSlot()
        slot.name = name
        slot.loaded = true
        slots[i][free] = slot
        order[i] = order[i].filter { $0 != free } + [free]
        pushOrder(i)
        engine.status = "FX \(name) ditambahkan di \(engine.inLabel(i))"
    }

    func removeEffect(_ k: Int) {
        let i = selectedInput
        guard !slots[i][k].isEmpty else { return }
        let ok = whileStopped { h in ah_io_fx_unload(h, Int32(i), Int32(k)) == 0 }
        guard ok else {
            engine.status = "Gagal melepas FX"
            return
        }
        slots[i][k] = FxSlot()
        order[i] = order[i].filter { $0 != k } + [k]
        pushOrder(i)
        if selectedSlot == k {
            selectedSlot = nil
            params = []
        }
    }

    // Menggeser slot satu posisi di chain. delta = -1 naik, +1 turun.
    func move(_ k: Int, by delta: Int) {
        let i = selectedInput
        let vis = chain(i)
        guard let p = vis.firstIndex(of: k), vis.indices.contains(p + delta) else { return }
        let other = vis[p + delta]
        guard let a = order[i].firstIndex(of: k), let b = order[i].firstIndex(of: other) else { return }
        order[i].swapAt(a, b)
        pushOrder(i)
    }

    // Enabled boleh diubah saat IO berjalan.
    func setEnabled(_ k: Int, _ on: Bool) {
        let i = selectedInput
        guard !slots[i][k].isEmpty else { return }
        slots[i][k].enabled = on
        if let h = engine.io { _ = ah_io_fx_set_enabled(h, Int32(i), Int32(k), on ? 1 : 0) }
    }

    // MARK: - Parameter

    func select(_ k: Int?) {
        selectedSlot = k
        refreshParams()
    }

    func setParam(_ id: UInt32, _ value: Float) {
        let i = selectedInput
        guard let k = selectedSlot, !slots[i][k].isEmpty, let h = engine.io else { return }
        _ = ah_io_fx_param_set(h, Int32(i), Int32(k), id, value)
        slots[i][k].paramValues[id] = value
        if let row = params.firstIndex(where: { $0.id == id }) { params[row].value = value }
    }

    private func refreshParams() {
        let i = selectedInput
        guard let k = selectedSlot, !slots[i][k].isEmpty, let h = engine.io else {
            params = []
            return
        }
        let count = Int(max(ah_io_fx_param_count(h, Int32(i), Int32(k)), 0))
        var rows: [FxParamRow] = []
        for idx in 0..<count {
            var p = AHFxParam()
            guard ah_io_fx_param_get(h, Int32(i), Int32(k), Int32(idx), &p) == 0 else { continue }
            rows.append(FxParamRow(id: p.id, name: cString(p.name), minValue: p.minValue, maxValue: p.maxValue, value: p.value))
        }
        params = rows
    }

    // MARK: - Proyek

    func saveProject(to url: URL) {
        captureStates()
        let file = StudioProjectFile(
            deviceName: engine.deviceName,
            routes: engine.routes,
            sends: engine.sends,
            gains: engine.gains,
            mutes: engine.mutes,
            outputGains: engine.outputGains,
            outputMutes: engine.outputMutes,
            chains: (0..<Self.maxInputs).map(chainFile)
        )
        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(file).write(to: url, options: .atomic)
            engine.status = "Proyek disimpan: \(url.lastPathComponent)"
        } catch {
            engine.status = "Gagal menyimpan proyek"
        }
    }

    func loadProject(from url: URL) {
        let file: StudioProjectFile
        do {
            file = try JSONDecoder().decode(StudioProjectFile.self, from: Data(contentsOf: url))
        } catch {
            engine.status = "Berkas proyek tidak bisa dibaca"
            return
        }
        guard file.schemaVersion == 1 else {
            engine.status = "Versi proyek \(file.schemaVersion) tidak didukung"
            return
        }
        if engine.io == nil { engine.open() }
        guard engine.io != nil else { return }

        applyRouting(file)

        var newSlots = Array(repeating: Array(repeating: FxSlot(), count: Self.slotCount), count: Self.maxInputs)
        var newOrder = Array(repeating: Array(0..<Self.slotCount), count: Self.maxInputs)
        for chain in file.chains where chain.input >= 0 && chain.input < Self.maxInputs && chain.slots.count == Self.slotCount {
            if isPermutation(chain.order) { newOrder[chain.input] = chain.order }
            for k in 0..<Self.slotCount {
                guard let sf = chain.slots[k], !sf.name.isEmpty else { continue }
                var slot = FxSlot()
                slot.name = sf.name
                slot.enabled = sf.enabled
                slot.state = sf.state
                for (key, value) in sf.paramValues {
                    if let id = UInt32(key) { slot.paramValues[id] = value }
                }
                newSlots[chain.input][k] = slot
            }
        }
        slots = newSlots
        order = newOrder
        selectedSlot = nil
        params = []
        // Muat ulang seluruh rantai; ini berjalan dengan IO berhenti.
        _ = whileStopped { _ in
            restoreChains()
            return true
        }
        engine.status = "Proyek dibuka: \(url.lastPathComponent)"
    }

    // MARK: - Internal

    // Memuat ulang semua slot yang berisi ke handle IO aktif. Dipanggil saat IO baru dibuka, dengan IO berhenti.
    private func restoreChains() {
        guard let h = engine.io else { return }
        var s = slots
        for i in 0..<Self.maxInputs {
            for k in 0..<Self.slotCount {
                // Mulai dari slot kosong: membersihkan sisa dari handle sebelumnya (mis. insert lama).
                _ = ah_io_fx_unload(h, Int32(i), Int32(k))
                s[i][k].loaded = false
            }
            for k in 0..<Self.slotCount where !s[i][k].isEmpty {
                let ok = ah_io_fx_load(h, Int32(i), Int32(k), s[i][k].name) == 0
                s[i][k].loaded = ok
                guard ok else { continue }
                if let st = s[i][k].state { applyState(h, i, k, st) }
                for (id, value) in s[i][k].paramValues {
                    _ = ah_io_fx_param_set(h, Int32(i), Int32(k), id, value)
                }
                _ = ah_io_fx_set_enabled(h, Int32(i), Int32(k), s[i][k].enabled ? 1 : 0)
            }
            pushOrder(i, on: h)
        }
        slots = s
        refreshParams()
    }

    // Mengambil state penuh setiap slot yang dimuat. Aman dipanggil saat IO berjalan.
    private func captureStates() {
        guard let h = engine.io else { return }
        for i in 0..<Self.maxInputs {
            for k in 0..<Self.slotCount where !slots[i][k].isEmpty {
                let n = Int(ah_io_fx_state_get(h, Int32(i), Int32(k), nil, 0))
                guard n > 0 else { continue }
                var buf = [UInt8](repeating: 0, count: n)
                if ah_io_fx_state_get(h, Int32(i), Int32(k), &buf, Int32(n)) == Int32(n) {
                    slots[i][k].state = Data(buf)
                }
            }
        }
    }

    private func chainFile(_ i: Int) -> ChainFile {
        ChainFile(
            input: i,
            order: order[i],
            slots: (0..<Self.slotCount).map { k -> SlotFile? in
                let s = slots[i][k]
                if s.isEmpty { return nil }
                var values: [String: Float] = [:]
                for (id, v) in s.paramValues { values[String(id)] = v }
                return SlotFile(name: s.name, enabled: s.enabled, state: s.state, paramValues: values)
            }
        )
    }

    private func applyRouting(_ f: StudioProjectFile) {
        for i in 0..<min(f.routes.count, engine.inputs) {
            for j in 0..<min(f.routes[i].count, engine.outputs) {
                engine.setRoute(i, j, f.routes[i][j])
                if i < f.sends.count, j < f.sends[i].count { engine.setSend(i, j, db: f.sends[i][j]) }
            }
        }
        for i in 0..<min(f.gains.count, engine.inputs) { engine.setGain(i, f.gains[i]) }
        for i in 0..<min(f.mutes.count, engine.inputs) { engine.setMute(i, f.mutes[i]) }
        for j in 0..<min(f.outputGains.count, engine.outputs) { engine.setOutputGain(j, f.outputGains[j]) }
        for j in 0..<min(f.outputMutes.count, engine.outputs) { engine.setOutputMute(j, f.outputMutes[j]) }
    }

    private func pushOrder(_ i: Int) {
        guard let h = engine.io else { return }
        pushOrder(i, on: h)
    }

    private func pushOrder(_ i: Int, on h: OpaquePointer) {
        var o = order[i].map { Int32($0) }
        _ = ah_io_fx_set_order(h, Int32(i), &o, Int32(Self.slotCount))
    }

    private func applyState(_ h: OpaquePointer, _ i: Int, _ k: Int, _ data: Data) {
        data.withUnsafeBytes { raw in
            _ = ah_io_fx_state_set(h, Int32(i), Int32(k), raw.bindMemory(to: UInt8.self).baseAddress, Int32(data.count))
        }
    }

    // Menjalankan perubahan yang butuh IO berhenti. Engine dihentikan lalu dijalankan lagi bila sedang berjalan.
    private func whileStopped(_ body: (OpaquePointer) -> Bool) -> Bool {
        if engine.io == nil { engine.open() }
        guard let h = engine.io else { return false }
        let wasRunning = engine.running
        if wasRunning { engine.stop() }
        let ok = body(h)
        if wasRunning { engine.start() }
        return ok
    }

    private func isPermutation(_ values: [Int]) -> Bool {
        values.count == Self.slotCount && Set(values) == Set(0..<Self.slotCount)
    }

    // Membaca string C dari tuple karakter hasil impor Swift.
    private func cString<T>(_ tuple: T) -> String {
        withUnsafePointer(to: tuple) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: MemoryLayout<T>.size) { String(cString: $0) }
        }
    }
}
