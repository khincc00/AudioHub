import AppKit
import CoreAudio
import Foundation
import SwiftUI

// MARK: - Parameter DSP

struct GateFx: Codable {
    var enabled = false
    var thresholdDb: Float = -50
    var attackMs: Float = 2
    var releaseMs: Float = 100
    var holdMs: Float = 50
}

struct EqFx: Codable {
    var enabled = false
    var lowDb: Float = 0
    var lowHz: Float = 120
    var midDb: Float = 0
    var midHz: Float = 1000
    var midQ: Float = 1
    var highDb: Float = 0
    var highHz: Float = 8000
}

struct CompFx: Codable {
    var enabled = false
    var thresholdDb: Float = -20
    var ratio: Float = 4
    var attackMs: Float = 10
    var releaseMs: Float = 150
    var makeupDb: Float = 0
    var kneeDb: Float = 0
    var sidechainInput = -1      // indeks input sebagai sumber detektor, -1 = tanpa sidechain

    init() {}
    init(enabled: Bool, thresholdDb: Float, ratio: Float, attackMs: Float, releaseMs: Float,
         makeupDb: Float, kneeDb: Float, sidechainInput: Int = -1) {
        self.enabled = enabled
        self.thresholdDb = thresholdDb
        self.ratio = ratio
        self.attackMs = attackMs
        self.releaseMs = releaseMs
        self.makeupDb = makeupDb
        self.kneeDb = kneeDb
        self.sidechainInput = sidechainInput
    }
    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        enabled = try c.decodeIfPresent(Bool.self, forKey: .enabled) ?? false
        thresholdDb = try c.decodeIfPresent(Float.self, forKey: .thresholdDb) ?? -20
        ratio = try c.decodeIfPresent(Float.self, forKey: .ratio) ?? 4
        attackMs = try c.decodeIfPresent(Float.self, forKey: .attackMs) ?? 10
        releaseMs = try c.decodeIfPresent(Float.self, forKey: .releaseMs) ?? 150
        makeupDb = try c.decodeIfPresent(Float.self, forKey: .makeupDb) ?? 0
        kneeDb = try c.decodeIfPresent(Float.self, forKey: .kneeDb) ?? 0
        sidechainInput = try c.decodeIfPresent(Int.self, forKey: .sidechainInput) ?? -1
    }
}

struct DelayFx: Codable {
    var enabled = false
    var delayMs: Float = 0
}

struct ChannelFx: Codable {
    var name = ""             // kosong = nama default (Fisik In 1, Virtual In 1, ...)
    var colorIndex = 0        // indeks palet Theme.channelColors
    var gate = GateFx()
    var eq = EqFx()
    var comp = CompFx()
    var delay = DelayFx()
    var insert = ""           // nama effect Audio Unit, kosong = tanpa plugin
    var pan: Float = 0        // −1 kiri … +1 kanan, untuk pasangan output

    init() {}
    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        name = try c.decodeIfPresent(String.self, forKey: .name) ?? ""
        colorIndex = try c.decodeIfPresent(Int.self, forKey: .colorIndex) ?? 0
        gate = try c.decodeIfPresent(GateFx.self, forKey: .gate) ?? GateFx()
        eq = try c.decodeIfPresent(EqFx.self, forKey: .eq) ?? EqFx()
        comp = try c.decodeIfPresent(CompFx.self, forKey: .comp) ?? CompFx()
        delay = try c.decodeIfPresent(DelayFx.self, forKey: .delay) ?? DelayFx()
        insert = try c.decodeIfPresent(String.self, forKey: .insert) ?? ""
        pan = try c.decodeIfPresent(Float.self, forKey: .pan) ?? 0
    }
}

struct LimiterFx: Codable {
    var enabled = false
    var ceilingDb: Float = -1
    var releaseMs: Float = 50
    var lookaheadMs: Float = 0

    init() {}
    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        enabled = try c.decodeIfPresent(Bool.self, forKey: .enabled) ?? false
        ceilingDb = try c.decodeIfPresent(Float.self, forKey: .ceilingDb) ?? -1
        releaseMs = try c.decodeIfPresent(Float.self, forKey: .releaseMs) ?? 50
        lookaheadMs = try c.decodeIfPresent(Float.self, forKey: .lookaheadMs) ?? 0
    }
}

// MARK: - Skema scene v1 (sesuai contracts/scene-schema/scene.v1.schema.json)

struct SceneBinding: Codable {
    var role: String
    var kind: String          // "physical" atau "virtual"
    var deviceUID: String
    var deviceName: String
    var channel: Int          // 1-based, dalam perangkatnya
}

struct SceneInput: Codable {
    var id: String            // "in:0", "in:1", ...
    var gainDb: Float
    var mute: Bool
    var fx: ChannelFx
}

struct SceneOutput: Codable {
    var id: String            // "out:0", ...
    var name: String
    var gainDb: Float
    var mute: Bool
    var limiter: LimiterFx
}

struct SceneRoute: Codable {
    var from: String
    var to: String
    var sendDb: Float
    var enabled: Bool
}

struct SceneFile: Codable {
    var schemaVersion: Int = 1
    var name: String
    var deviceName: String
    var useVirtual: Bool
    var useVirtual2: Bool
    var bindings: [SceneBinding]
    var inputs: [SceneInput]
    var outputs: [SceneOutput]
    var routes: [SceneRoute]
    var linkedPairs: [Bool]
    var buses: [String] = []  // dicadangkan untuk bus, belum dipakai
}

// Format lama (sebelum skema terpadu). Hanya dipakai untuk migrasi preset lama.
struct LegacySceneFile: Codable {
    var schemaVersion: Int
    var deviceName: String
    var routes: [[Bool]]
    var gains: [Float]
    var mutes: [Bool]
    var outputGains: [Float]?
    var outputMutes: [Bool]?
    var sends: [[Float]]?
    var linkedPairs: [Bool]?
}

// MARK: - Perangkat sistem

struct DeviceInfo: Identifiable, Hashable {
    let uid: String
    let name: String
    let inCh: Int
    let outCh: Int
    let isVirtual: Bool   // transport virtual dari Core Audio (driver apa pun, bukan vendor tertentu)
    var id: String { uid }
}

// UID agregat yang dibuat AudioHub sendiri. Disembunyikan dari daftar perangkat supaya tidak
// bisa dipilih sebagai perangkat fisik atau virtual. Harus sama dengan kAggregateUID di audiohub_io.cpp.
private let audiohubAggregateUIDPrefix = "com.audiohub.app.aggregate"

func systemDevices() -> [DeviceInfo] {
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                          mScope: kAudioObjectPropertyScopeGlobal,
                                          mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr else { return [] }
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else { return [] }
    return ids.compactMap { id in
        guard let name = cfStringProperty(id, kAudioObjectPropertyName),
              let uid = cfStringProperty(id, kAudioDevicePropertyDeviceUID) else { return nil }
        if uid.hasPrefix(audiohubAggregateUIDPrefix) { return nil }
        return DeviceInfo(uid: uid, name: name,
                          inCh: channelCount(id, kAudioObjectPropertyScopeInput),
                          outCh: channelCount(id, kAudioObjectPropertyScopeOutput),
                          isVirtual: transportType(id) == kAudioDeviceTransportTypeVirtual)
    }
}

private func transportType(_ id: AudioObjectID) -> UInt32 {
    var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyTransportType, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var t: UInt32 = 0
    var size = UInt32(MemoryLayout<UInt32>.size)
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &t) == noErr else { return 0 }
    return t
}

private func cfStringProperty(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector) -> String? {
    var addr = AudioObjectPropertyAddress(mSelector: sel, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var s: CFString = "" as CFString
    var size = UInt32(MemoryLayout<CFString>.size)
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &s) == noErr else { return nil }
    return s as String
}

private func channelCount(_ id: AudioObjectID, _ scope: AudioObjectPropertyScope) -> Int {
    var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
    let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { raw.deallocate() }
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
    let list = raw.assumingMemoryBound(to: AudioBufferList.self)
    return UnsafeMutableAudioBufferListPointer(list).reduce(0) { $0 + Int($1.mNumberChannels) }
}

// MARK: - Meter

// Nilai yang berubah tiap 0,1 detik. Dipisah dari EngineController supaya
// hanya bagian yang menampilkan meter yang dihitung ulang, bukan seluruh jendela.
final class Meters: ObservableObject {
    @Published var inDB: [Float] = []
    @Published var outDB: [Float] = []
    @Published var inHold: [Float] = []
    @Published var outHold: [Float] = []
    @Published var inClip: [Bool] = []
    @Published var outClip: [Bool] = []
    @Published var callbacks: UInt64 = 0
    @Published var dropouts: UInt64 = 0
    @Published var bufferFrames: UInt32 = 0
    @Published var sampleRate: Double = 0
    @Published var inputLatencyFrames: UInt32 = 0
    @Published var outputLatencyFrames: UInt32 = 0

    func clearClip(input: Int?, output: Int?) {
        if let i = input, i < inClip.count { inClip[i] = false }
        if let j = output, j < outClip.count { outClip[j] = false }
    }
}

// MARK: - Controller

final class EngineController: ObservableObject {
    let meters = Meters()
    @Published var deviceName = "Maono ProStudio 2x2"
    @Published var autoReconnect: Bool = UserDefaults.standard.bool(forKey: "autoReconnect") {
        didSet { UserDefaults.standard.set(autoReconnect, forKey: "autoReconnect") }
    }
    @Published var redactDiagnostics = true
    // Sample rate dan buffer yang dipilih pengguna. Dipasang ke perangkat saat IO dibuka ulang.
    // Rate dan buffer hanya dipilih dari nilai yang didukung perangkat (lihat loadCapabilities).
    @Published var supportedRates: [Double] = []
    @Published var bufferChoices: [UInt32] = []
    @Published var prefSampleRate: Double = {
        let stored = UserDefaults.standard.double(forKey: "prefSampleRate")
        return stored > 0 ? stored : 48000
    }() {
        didSet {
            UserDefaults.standard.set(prefSampleRate, forKey: "prefSampleRate")
            reopenWithPreferences()
        }
    }
    @Published var prefBuffer: UInt32 = {
        let stored = UserDefaults.standard.integer(forKey: "prefBuffer")
        return stored > 0 ? UInt32(stored) : 512
    }() {
        didSet {
            UserDefaults.standard.set(Int(prefBuffer), forKey: "prefBuffer")
            reopenWithPreferences()
        }
    }
    @Published var devices: [DeviceInfo] = []
    @Published var missingRoles: [String] = []
    @Published var physIn = 0
    @Published var physOut = 0
    @Published var running = false
    @Published var status = "Belum dibuka"
    @Published var inputs = 0
    @Published var outputs = 0
    @Published var routes: [[Bool]] = []
    @Published var sends: [[Float]] = []
    @Published var gains: [Float] = []
    @Published var mutes: [Bool] = []
    @Published var linkedPairs: [Bool] = []
    @Published var outputGains: [Float] = []
    @Published var outputMutes: [Bool] = []
    @Published var presets: [String] = []
    @Published var currentPreset: String = ""
    @Published var fx: [ChannelFx] = []
    @Published var outFx: [LimiterFx] = []
    @Published var outNames: [String] = []
    @Published var effectNames: [String] = []
    @Published var soloInputs: Set<Int> = []        // solo hanya memengaruhi monitor (output 1)
    @Published var recordingOutput: Int? = nil
    @Published var recordedFile = ""

    // Virtual 1 dan 2 dipilih dari perangkat virtual yang terpasang, apa pun vendornya.
    // Kosong = tidak dipakai. Disimpan supaya tetap terpilih setelah aplikasi dibuka ulang.
    @Published var virtualUID1: String = UserDefaults.standard.string(forKey: "virtualUID1") ?? "" {
        didSet { UserDefaults.standard.set(virtualUID1, forKey: "virtualUID1") }
    }
    @Published var virtualUID2: String = UserDefaults.standard.string(forKey: "virtualUID2") ?? "" {
        didSet { UserDefaults.standard.set(virtualUID2, forKey: "virtualUID2") }
    }

    // Nilai turunan untuk skema scene dan diagnostik.
    var useVirtual: Bool { !virtualUID1.isEmpty }
    var useVirtual2: Bool { !virtualUID2.isEmpty }

    // Perangkat virtual yang bisa dipilih untuk Virtual 1 atau 2: transport virtual, punya input dan output,
    // bukan perangkat fisik utama, dan tidak sama dengan slot lainnya.
    func virtualCandidates(excluding uid: String) -> [DeviceInfo] {
        devices.filter {
            $0.isVirtual && $0.inCh > 0 && $0.outCh > 0 && $0.uid != uid && $0.name != deviceName
        }
    }

    // Virtual device yang masuk aggregate saat terakhir dibuka, sesuai urutan kanal (uid1 lalu uid2).
    private(set) var openedUID1 = ""
    private(set) var openedUID2 = ""

    var openedName1: String { openedUID1.isEmpty ? "" : deviceNameOf(openedUID1) }
    var openedName2: String { openedUID2.isEmpty ? "" : deviceNameOf(openedUID2) }

    func deviceNameOf(_ uid: String) -> String {
        devices.first(where: { $0.uid == uid })?.name ?? uid
    }

    // Pemilik kanal dari backend: 0 fisik, 1 virtual 1, 2 virtual 2, -1 tidak diketahui; plus nomor kanal di perangkat itu.
    func channelOwner(_ index: Int, output: Bool) -> (owner: Int, local: Int) {
        guard let h = io else { return (-1, 0) }
        var owner: Int32 = -1
        var local: Int32 = 0
        ah_io_channel_owner(h, output ? 1 : 0, Int32(index), &owner, &local)
        return (Int(owner), Int(local))
    }

    // Handle IO aktif. Dibaca oleh AudioHub Studio untuk memuat rantai FX.
    private(set) var io: OpaquePointer?
    // Bertambah setiap perangkat berhasil dibuka (termasuk sambung ulang). Rantai FX memuat ulang saat berubah.
    @Published private(set) var ioGeneration = 0
    private var timer: Timer?
    private var reconnectTimer: Timer?
    // Pembacaan "tidak hidup" berturut-turut. Satu kegagalan sesaat dari HAL tidak menghentikan IO.
    private var aliveMisses = 0
    private let aliveMissLimit = 5   // 5 × 0,1 detik = sekitar 0,5 detik

    init() {
        // Engine tetap berjalan saat jendela ditutup. Hanya dihentikan saat aplikasi keluar.
        NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil, queue: .main) { [weak self] _ in
            self?.close()
        }
        refreshDevices()
        installDeviceListener()
    }

    // MARK: - Lokasi file

    private var appSupport: URL {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AudioHub", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    private var presetsDir: URL {
        let dir = appSupport.appendingPathComponent("presets", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    var isOpen: Bool { io != nil }

    // MARK: - Perangkat

    func refreshDevices() {
        devices = systemDevices()
        refreshMissing()
        loadCapabilities()
    }

    // Rate dan buffer yang didukung perangkat utama. Pilihan di luar daftar ini tidak ditawarkan.
    private func loadCapabilities() {
        var rates = [Double](repeating: 0, count: 16)
        var bMin: UInt32 = 0, bMax: UInt32 = 0
        let n = Int(ah_io_device_capabilities(deviceName, &rates, 16, &bMin, &bMax))
        supportedRates = n > 0 ? Array(rates.prefix(n)).sorted() : []
        let candidates: [UInt32] = [64, 128, 256, 512, 1024]
        bufferChoices = bMax > 0 ? candidates.filter { $0 >= bMin && $0 <= bMax } : []
    }

    // Membuka ulang IO agar pilihan baru berlaku. Jika sebelumnya berjalan, langsung dijalankan lagi.
    private func reopenWithPreferences() {
        guard isOpen else { return }
        let wasRunning = running
        open()
        if wasRunning { start() }
    }

    // MARK: - Hot-plug

    private var deviceListener: AudioObjectPropertyListenerBlock?

    // Daftar perangkat berubah (dicabut atau dipasang) langsung memicu pembaruan, tanpa menunggu polling.
    private func installDeviceListener() {
        var a = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                           mScope: kAudioObjectPropertyScopeGlobal,
                                           mElement: kAudioObjectPropertyElementMain)
        let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            DispatchQueue.main.async { self?.refreshDevices() }
        }
        AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &a, DispatchQueue.main, block)
        deviceListener = block
    }

    // MARK: - Kontrol hardware

    // Volume dan mute hardware per kanal output fisik (HP Out). Dibaca saat perangkat dibuka.
    @Published var hwVolume: [Float] = []
    @Published var hwMute: [Bool] = []

    func refreshHardwareControls() {
        guard let h = io, physOut > 0 else { hwVolume = []; hwMute = []; return }
        var vols: [Float] = [], mutes: [Bool] = []
        for ch in 1...physOut {
            var v: Float = 0
            var m: Int32 = 0
            if ah_io_hw_output_get(h, Int32(ch), &v, &m) == 0 {
                vols.append(v)
                mutes.append(m != 0)
            } else {
                vols.append(0)
                mutes.append(false)
            }
        }
        hwVolume = vols
        hwMute = mutes
    }

    func setHwVolume(output j: Int, _ volume: Float) {
        guard let h = io, j < hwVolume.count else { return }
        if ah_io_hw_output_set(h, Int32(j + 1), volume, -1) == 0 { hwVolume[j] = volume }
    }

    func setHwMute(output j: Int, _ mute: Bool) {
        guard let h = io, j < hwMute.count else { return }
        if ah_io_hw_output_set(h, Int32(j + 1), -1, mute ? 1 : 0) == 0 { hwMute[j] = mute }
    }

    // MARK: - Aturan nama preset

    // Pesan kesalahan untuk nama preset, atau nil jika nama boleh dipakai.
    func presetNameProblem(_ raw: String) -> String? {
        let name = sanitize(raw)
        if name.isEmpty { return "Nama preset tidak boleh kosong" }
        if name.count > 20 { return "Nama preset maksimal 20 karakter" }
        return nil
    }

    // True jika nama (setelah dibersihkan) sudah dipakai preset lain.
    func presetExists(_ raw: String) -> Bool {
        presets.contains(sanitize(raw))
    }

    private var virtualUIDsInUse: [String] {
        [virtualUID1, virtualUID2].filter { !$0.isEmpty }
    }

    // Peran yang perangkatnya tidak tersedia saat ini. Tidak pernah diganti diam-diam.
    private func refreshMissing() {
        let present = Set(devices.map { $0.uid })
        var missing: [String] = []
        if !devices.contains(where: { $0.name == deviceName }) { missing.append(deviceName) }
        for uid in virtualUIDsInUse where !present.contains(uid) { missing.append(uid) }
        missingRoles = missing
    }

    // MARK: - Buka dan tutup perangkat

    func open() {
        close()
        refreshDevices()
        if useVirtual && useVirtual2 && virtualUID2 == virtualUID1 {
            status = "Virtual 1 dan Virtual 2 harus perangkat yang berbeda"
            return
        }
        var buf = [CChar](repeating: 0, count: 256)
        let handle: OpaquePointer?
        // Pilihan buffer dan rate dipasang sebelum perangkat dibuka. Virtual device ikut memakai buffer yang sama.
        ah_io_set_preferences(prefBuffer, prefSampleRate)
        // Virtual 1 dipakai jika dipilih; jika hanya Virtual 2 yang dipilih, dialah yang masuk aggregate.
        let uid1: String? = useVirtual ? virtualUID1 : (useVirtual2 ? virtualUID2 : nil)
        let uid2: String? = (useVirtual && useVirtual2) ? virtualUID2 : nil
        openedUID1 = uid1 ?? ""
        openedUID2 = uid2 ?? ""
        if let u1 = uid1 {
            handle = ah_io_open_with_virtuals(deviceName, u1, uid2, &buf, 256)
        } else {
            handle = ah_io_open(deviceName, &buf, 256)
        }
        guard let h = handle else {
            status = String(cString: buf)
            return
        }
        io = h
        inputs = Int(ah_io_inputs(h))
        outputs = Int(ah_io_outputs(h))
        physIn = Int(ah_io_physical_inputs(h))
        physOut = Int(ah_io_physical_outputs(h))
        refreshHardwareControls()
        resetDefaults()
        loadEffectNames()
        refreshPresets()
        if let last = UserDefaults.standard.string(forKey: "lastPreset"), presets.contains(last) {
            loadPreset(last, quiet: true)
        }
        ioGeneration += 1
        status = "Terbuka: \(inputs) in (\(physIn) fisik), \(outputs) out (\(physOut) fisik)"
    }

    // Default: fisik → fisik (input i → output i), gain 0 dB, send 0 dB, tidak dibisukan.
    // Virtual tidak dihubungkan otomatis.
    func resetDefaults() {
        routes = (0..<inputs).map { i in (0..<outputs).map { j in i == j && i < physIn && j < physOut } }
        sends = Array(repeating: Array(repeating: 0, count: outputs), count: inputs)
        gains = Array(repeating: 0, count: inputs)
        mutes = Array(repeating: false, count: inputs)
        linkedPairs = Array(repeating: false, count: inputs / 2)
        outputGains = Array(repeating: 0, count: outputs)
        outputMutes = Array(repeating: false, count: outputs)
        outNames = (0..<outputs).map { defaultOutLabel($0) }
        meters.inDB = Array(repeating: -120, count: inputs)
        meters.outDB = Array(repeating: -120, count: outputs)
        meters.inHold = meters.inDB
        meters.outHold = meters.outDB
        meters.inClip = Array(repeating: false, count: inputs)
        meters.outClip = Array(repeating: false, count: outputs)
        fx = Array(repeating: ChannelFx(), count: inputs)
        outFx = Array(repeating: LimiterFx(), count: outputs)
        currentPreset = ""
        applyAll()
    }

    private func loadEffectNames() {
        var buf = [CChar](repeating: 0, count: 8192)
        _ = ah_io_list_effects(&buf, 8192)
        let text = String(cString: buf)
        effectNames = text.split(separator: "\n").map(String.init).sorted()
    }

    // Kanal virtual diberi nama perangkatnya, mis. "avhub_virtual1 · 1", supaya tidak tertukar antar perangkat.
    func defaultInLabel(_ i: Int) -> String {
        if i < physIn { return "Fisik In \(i + 1)" }
        let (owner, local) = channelOwner(i, output: false)
        if owner == 1 && !openedName1.isEmpty { return "\(openedName1) · \(local)" }
        if owner == 2 && !openedName2.isEmpty { return "\(openedName2) · \(local)" }
        return "Virtual In \(i - physIn + 1)"
    }

    func defaultOutLabel(_ j: Int) -> String {
        if j < physOut { return "Fisik Out \(j + 1)" }
        let (owner, local) = channelOwner(j, output: true)
        if owner == 1 && !openedName1.isEmpty { return "\(openedName1) · \(local)" }
        if owner == 2 && !openedName2.isEmpty { return "\(openedName2) · \(local)" }
        return "Virtual Out \(j - physOut + 1)"
    }

    // Nama yang tampil: nama buatan pengguna jika ada, selain itu nama default
    func inLabel(_ i: Int) -> String {
        guard i < fx.count, !fx[i].name.isEmpty else { return defaultInLabel(i) }
        return fx[i].name
    }

    func outLabel(_ j: Int) -> String {
        guard j < outNames.count, !outNames[j].isEmpty else { return defaultOutLabel(j) }
        return outNames[j]
    }

    func setInputName(_ i: Int, _ name: String) {
        guard i < fx.count else { return }
        fx[i].name = name
    }

    func setOutputName(_ j: Int, _ name: String) {
        guard j < outNames.count else { return }
        outNames[j] = name
    }

    func start() {
        if io == nil { open() }
        guard let h = io else { return }
        if ah_io_start(h) == 0 {
            running = true
            status = "Berjalan"
            aliveMisses = 0
            cancelReconnect()
            timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.tick() }
        } else {
            status = "Gagal memulai perangkat"
        }
    }

    func stop() {
        cancelReconnect()
        haltIO()
        if running { status = "Dihentikan" }
    }

    // Menghentikan IO tanpa membatalkan percobaan sambung ulang
    private func haltIO() {
        timer?.invalidate()
        timer = nil
        if let h = io { ah_io_stop(h) }
        running = false
    }

    func close() {
        cancelReconnect()
        if recordingOutput != nil { stopRecording() }
        haltIO()
        if let h = io { ah_io_close(h) }
        io = nil
    }

    // MARK: - Sambung ulang

    private func beginReconnect() {
        cancelReconnect()
        status = "Perangkat terputus. Mencoba menyambung ulang setiap 2 detik..."
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.attemptReconnect()
        }
    }

    private func attemptReconnect() {
        close()
        open()
        if isOpen {
            start()
            status = "Tersambung kembali: \(status)"
        } else {
            status = "Perangkat belum tersedia, mencoba lagi..."
        }
    }

    private func cancelReconnect() {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
    }

    // MARK: - Routing, send, gain, mute

    // Route boleh dibuat? Aturan ada di backend (virtual → virtual hanya antar perangkat virtual berbeda).
    func routeAllowed(_ i: Int, _ j: Int) -> Bool {
        guard let h = io else { return false }
        return ah_io_route_allowed(h, Int32(i), Int32(j)) == 1
    }

    func setRoute(_ i: Int, _ j: Int, _ on: Bool) {
        guard io != nil else { return }
        if on && !routeAllowed(i, j) {
            status = "Ditolak: route virtual ke virtual dalam perangkat yang sama akan membentuk loop"
            return
        }
        routes[i][j] = on
        pushRoute(i, j)
    }

    // Send per koneksi: 0, −6, −12 dB, atau mati (on = false).
    func setSend(_ i: Int, _ j: Int, db: Float) {
        guard io != nil else { return }
        sends[i][j] = db
        pushRoute(i, j)
    }

    // Route yang berlaku: keluaran monitor (output 1) hanya memakai input yang di-solo, jika ada solo.
    private func effectiveRoute(_ i: Int, _ j: Int) -> Bool {
        if j == 0 && !soloInputs.isEmpty { return routes[i][0] && soloInputs.contains(i) }
        return routes[i][j]
    }

    private func pushRoute(_ i: Int, _ j: Int) {
        guard let h = io else { return }
        _ = ah_io_set_route(h, Int32(i), Int32(j), effectiveRoute(i, j) ? 1 : 0, sends[i][j])
    }

    func toggleSolo(_ i: Int) {
        if soloInputs.contains(i) { soloInputs.remove(i) } else { soloInputs.insert(i) }
        for k in 0..<inputs { pushRoute(k, 0) }
        status = soloInputs.isEmpty ? "Solo dilepas" : "Solo aktif (hanya monitor): \(soloInputs.sorted().map { inLabel($0) }.joined(separator: ", "))"
    }

    func setPan(_ i: Int, _ pan: Float) {
        updateInputFx(i) { $0.pan = max(-1, min(1, pan)) }
    }

    // MARK: - Rekaman

    func startRecording(output j: Int) {
        guard let h = io, recordingOutput == nil else { return }
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let url = appSupport.appendingPathComponent("rekaman-\(stamp).wav")
        if ah_io_record_start(h, Int32(j), url.path) == 0 {
            recordingOutput = j
            recordedFile = url.lastPathComponent
            status = "Merekam \(outLabel(j)) ke \(url.lastPathComponent)"
        } else {
            status = "Gagal memulai rekaman"
        }
    }

    func stopRecording() {
        guard let h = io, recordingOutput != nil else { return }
        ah_io_record_stop(h)
        recordingOutput = nil
        var written: UInt64 = 0, dropped: UInt64 = 0
        ah_io_record_stats(h, &written, &dropped)
        status = "Rekaman disimpan: \(recordedFile) (\(written) frame, terbuang \(dropped))"
    }

    // Input yang terpengaruh: jika pasangan ditautkan, keduanya ikut berubah.
    func affectedInputs(_ i: Int) -> [Int] {
        let p = i / 2
        if i + 1 < inputs, p < linkedPairs.count, linkedPairs[p] { return [2 * p, 2 * p + 1] }
        return [i]
    }

    func setGain(_ i: Int, _ db: Float) {
        guard let h = io else { return }
        for k in affectedInputs(i) {
            gains[k] = db
            ah_io_set_channel(h, Int32(k), db, mutes[k] ? 1 : 0)
        }
    }

    func setMute(_ i: Int, _ mute: Bool) {
        guard let h = io else { return }
        for k in affectedInputs(i) {
            mutes[k] = mute
            ah_io_set_channel(h, Int32(k), gains[k], mute ? 1 : 0)
        }
    }

    func setLink(_ pair: Int, _ on: Bool) {
        guard pair < linkedPairs.count else { return }
        linkedPairs[pair] = on
        if on {
            // Samakan pasangan ke channel pertama
            let a = 2 * pair, b = 2 * pair + 1
            gains[b] = gains[a]
            mutes[b] = mutes[a]
            setGain(a, gains[a])
            setMute(a, mutes[a])
        }
    }

    func setOutputGain(_ j: Int, _ db: Float) {
        guard let h = io else { return }
        outputGains[j] = db
        ah_io_set_output(h, Int32(j), db, outputMutes[j] ? 1 : 0)
    }

    func setOutputMute(_ j: Int, _ mute: Bool) {
        guard let h = io else { return }
        outputMutes[j] = mute
        ah_io_set_output(h, Int32(j), outputGains[j], mute ? 1 : 0)
    }

    // Jumlah sumber aktif yang masuk ke satu output (campuran)
    func sourceCount(_ j: Int) -> Int {
        (0..<inputs).filter { routes[$0][j] }.count
    }

    private func applyAll() {
        guard let h = io else { return }
        for i in 0..<inputs {
            for j in 0..<outputs {
                _ = ah_io_set_route(h, Int32(i), Int32(j), effectiveRoute(i, j) ? 1 : 0, sends[i][j])
            }
            ah_io_set_channel(h, Int32(i), gains[i], mutes[i] ? 1 : 0)
        }
        for j in 0..<outputs {
            ah_io_set_output(h, Int32(j), outputGains[j], outputMutes[j] ? 1 : 0)
        }
        for i in 0..<min(inputs, fx.count) { pushInputFx(i) }
        for j in 0..<min(outputs, outFx.count) { pushOutputFx(j) }
    }

    // MARK: - DSP

    private func pushInputFx(_ i: Int) {
        guard let h = io else { return }
        let f = fx[i]
        var g = AHGateParams(enabled: f.gate.enabled ? 1 : 0, thresholdDb: f.gate.thresholdDb,
                             attackMs: f.gate.attackMs, releaseMs: f.gate.releaseMs, holdMs: f.gate.holdMs)
        var e = AHEqParams(enabled: f.eq.enabled ? 1 : 0, lowDb: f.eq.lowDb, lowHz: f.eq.lowHz,
                           midDb: f.eq.midDb, midHz: f.eq.midHz, midQ: f.eq.midQ,
                           highDb: f.eq.highDb, highHz: f.eq.highHz)
        var c = AHCompParams(enabled: f.comp.enabled ? 1 : 0, thresholdDb: f.comp.thresholdDb, ratio: f.comp.ratio,
                             attackMs: f.comp.attackMs, releaseMs: f.comp.releaseMs, makeupDb: f.comp.makeupDb,
                             kneeDb: f.comp.kneeDb, sidechainInput: Int32(f.comp.sidechainInput))
        var d = AHDelayParams(enabled: f.delay.enabled ? 1 : 0, delayMs: f.delay.delayMs)
        ah_io_set_input_fx(h, Int32(i), &g, &e, &c)
        ah_io_set_input_delay(h, Int32(i), &d)
        ah_io_set_channel_pan(h, Int32(i), f.pan)
    }

    private func pushOutputFx(_ j: Int) {
        guard let h = io else { return }
        let f = outFx[j]
        var l = AHLimiterParams(enabled: f.enabled ? 1 : 0, ceilingDb: f.ceilingDb, releaseMs: f.releaseMs,
                                lookaheadMs: f.lookaheadMs)
        ah_io_set_output_limiter(h, Int32(j), &l)
    }

    // Mengubah parameter DSP satu input dan langsung menerapkannya
    func updateInputFx(_ i: Int, _ change: (inout ChannelFx) -> Void) {
        guard i < fx.count else { return }
        change(&fx[i])
        pushInputFx(i)
    }

    func updateOutputFx(_ j: Int, _ change: (inout LimiterFx) -> Void) {
        guard j < outFx.count else { return }
        change(&outFx[j])
        pushOutputFx(j)
    }

    // Memasang atau melepas plugin Audio Unit pada satu input. Butuh IO berhenti sebentar.
    func setInsert(_ i: Int, _ name: String) {
        guard let h = io, i < fx.count else { return }
        let wasRunning = running
        if wasRunning { haltIO() }
        _ = ah_io_insert_unload(h, Int32(i))
        if name.isEmpty {
            fx[i].insert = ""
        } else if ah_io_insert_load(h, Int32(i), name) == 0 {
            fx[i].insert = name
            status = "Plugin \(name) dipasang di \(inLabel(i))"
        } else {
            fx[i].insert = ""
            status = "Gagal memuat plugin \(name)"
        }
        if wasRunning { start() }
    }

    // MARK: - Tick meter

    private func tick() {
        guard let h = io else { return }
        // Kebijakan: perangkat hilang → berhenti. Auto sambung ulang hanya jika pengguna mengaktifkannya.
        // Pengecekan harus gagal beberapa kali berturut-turut; kegagalan sesaat dicatat lalu diabaikan.
        if ah_io_is_alive(h) == 0 {
            aliveMisses += 1
            if aliveMisses < aliveMissLimit {
                NSLog("AudioHub: IsAlive gagal sesaat (%d/%d), IO tetap berjalan", aliveMisses, aliveMissLimit)
                return
            }
            aliveMisses = 0
            haltIO()
            refreshDevices()
            if autoReconnect {
                beginReconnect()
            } else {
                status = "Perangkat terputus. Hubungkan kembali lalu tekan Buka."
            }
            return
        }
        aliveMisses = 0
        var inP = [Float](repeating: 0, count: 8)
        var outP = [Float](repeating: 0, count: 8)
        ah_io_peaks(h, &inP, &outP)
        var st = AHIOStats()
        ah_io_stats(h, &st)

        let newIn = (0..<inputs).map { toDB(inP[$0]) }
        let newOut = (0..<outputs).map { toDB(outP[$0]) }
        meters.inDB = newIn
        meters.outDB = newOut
        // Peak hold turun 5 dB per detik. Clip menyala saat level ≥ −0,5 dBFS dan tetap sampai dihapus.
        meters.inHold = zip(newIn, meters.inHold).map { max($0, $1 - 0.5) }
        meters.outHold = zip(newOut, meters.outHold).map { max($0, $1 - 0.5) }
        meters.inClip = zip(newIn, meters.inClip).map { $0 >= -0.5 || $1 }
        meters.outClip = zip(newOut, meters.outClip).map { $0 >= -0.5 || $1 }
        meters.callbacks = st.callbacks
        meters.dropouts = st.dropouts
        meters.bufferFrames = st.bufferFrames
        meters.sampleRate = st.sampleRate
        meters.inputLatencyFrames = st.inputLatencyFrames
        meters.outputLatencyFrames = st.outputLatencyFrames
    }

    // MARK: - Binding peran

    // UID perangkat dan nomor kanal untuk kanal virtual. Saat terbuka dari backend; saat tertutup dari pilihan tersimpan.
    private func virtualBinding(_ index: Int, output: Bool, fallbackIndex v: Int) -> (uid: String, channel: Int) {
        guard isOpen else {
            let uids = virtualUIDsInUse
            return (v / 2 < uids.count ? uids[v / 2] : "", v % 2 + 1)
        }
        let (owner, local) = channelOwner(index, output: output)
        switch owner {
        case 1: return (openedUID1, local)
        case 2: return (openedUID2, local)
        default: return ("", 0)
        }
    }

    // Binding untuk setiap input dan output saat ini, dengan peran logis.
    func currentBindings() -> [SceneBinding] {
        let physical = devices.first(where: { $0.name == deviceName })
        let physUID = physical?.uid ?? ""
        var result: [SceneBinding] = []
        for i in 0..<inputs {
            if i < physIn {
                let role = i == 0 ? "Main Microphone" : "Input \(i + 1)"
                result.append(SceneBinding(role: role, kind: "physical", deviceUID: physUID, deviceName: deviceName, channel: i + 1))
            } else {
                let v = i - physIn
                let b = virtualBinding(i, output: false, fallbackIndex: v)
                result.append(SceneBinding(role: "Virtual Input \(v + 1)", kind: "virtual", deviceUID: b.uid,
                                           deviceName: devices.first(where: { $0.uid == b.uid })?.name ?? "", channel: b.channel))
            }
        }
        for j in 0..<outputs {
            if j < physOut {
                let role = j == 0 ? "Monitor Output" : "Output \(j + 1)"
                result.append(SceneBinding(role: role, kind: "physical", deviceUID: physUID, deviceName: deviceName, channel: j + 1))
            } else {
                let v = j - physOut
                let b = virtualBinding(j, output: true, fallbackIndex: v)
                result.append(SceneBinding(role: "Virtual Output \(v + 1)", kind: "virtual", deviceUID: b.uid,
                                           deviceName: devices.first(where: { $0.uid == b.uid })?.name ?? "", channel: b.channel))
            }
        }
        return result
    }

    // Peran dari scene yang perangkatnya tidak ada di sistem. Ditandai, tidak diganti.
    private func missingBindings(_ bindings: [SceneBinding]) -> [String] {
        let present = Set(devices.map { $0.uid })
        return bindings.filter { !$0.deviceUID.isEmpty && !present.contains($0.deviceUID) }.map { $0.role }
    }

    // MARK: - Preset

    func refreshPresets() {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: presetsDir.path)) ?? []
        presets = names.filter { $0.hasSuffix(".json") }.map { String($0.dropLast(5)) }.sorted()
    }

    private func sanitize(_ name: String) -> String {
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: " -_"))
        return String(name.unicodeScalars.filter { allowed.contains($0) }).trimmingCharacters(in: .whitespaces)
    }

    func savePreset(named raw: String) {
        if let problem = presetNameProblem(raw) { status = problem; return }
        let name = sanitize(raw)
        let scene = SceneFile(
            name: name, deviceName: deviceName, useVirtual: useVirtual, useVirtual2: useVirtual2,
            bindings: currentBindings(),
            inputs: (0..<inputs).map { SceneInput(id: "in:\($0)", gainDb: gains[$0], mute: mutes[$0], fx: fx[$0]) },
            outputs: (0..<outputs).map { SceneOutput(id: "out:\($0)", name: outNames[$0], gainDb: outputGains[$0],
                                                     mute: outputMutes[$0], limiter: outFx[$0]) },
            routes: (0..<inputs).flatMap { i in (0..<outputs).map { j in
                SceneRoute(from: "in:\(i)", to: "out:\(j)", sendDb: sends[i][j], enabled: routes[i][j]) } },
            linkedPairs: linkedPairs)
        do {
            let data = try JSONEncoder().encode(scene)
            try data.write(to: presetsDir.appendingPathComponent("\(name).json"), options: .atomic)
            currentPreset = name
            UserDefaults.standard.set(name, forKey: "lastPreset")
            refreshPresets()
            status = "Preset \"\(name)\" disimpan"
        } catch {
            status = "Gagal menyimpan preset: \(error.localizedDescription)"
        }
    }

    func loadPreset(_ name: String, quiet: Bool = false) {
        let url = presetsDir.appendingPathComponent("\(name).json")
        guard let data = try? Data(contentsOf: url) else {
            if !quiet { status = "Preset \"\(name)\" tidak ditemukan" }
            return
        }
        if let scene = try? JSONDecoder().decode(SceneFile.self, from: data) {
            applyScene(scene, quiet: quiet)
        } else if let legacy = try? JSONDecoder().decode(LegacySceneFile.self, from: data) {
            applyLegacy(legacy, name: name, quiet: quiet)
        } else {
            if !quiet { status = "Preset \"\(name)\" tidak bisa dibaca" }
        }
    }

    private func applyScene(_ scene: SceneFile, quiet: Bool) {
        guard scene.schemaVersion == 1 else { status = "Versi preset tidak dikenal"; return }
        guard scene.inputs.count == inputs, scene.outputs.count == outputs else {
            status = "Preset \"\(scene.name)\" tidak cocok dengan jumlah kanal perangkat"
            return
        }
        // Route yang tidak diizinkan backend (virtual ke virtual dalam perangkat yang sama) ditolak
        for r in scene.routes where r.enabled {
            guard let i = Int(r.from.dropFirst(3)), let j = Int(r.to.dropFirst(4)) else { status = "Route tidak valid"; return }
            if !routeAllowed(i, j) {
                status = "Preset berisi route yang tidak diizinkan, ditolak"
                return
            }
        }
        for s in scene.inputs {
            guard let k = Int(s.id.dropFirst(3)), k < inputs else { continue }
            gains[k] = s.gainDb
            mutes[k] = s.mute
            fx[k] = s.fx
        }
        for o in scene.outputs {
            guard let k = Int(o.id.dropFirst(4)), k < outputs else { continue }
            outputGains[k] = o.gainDb
            outputMutes[k] = o.mute
            outFx[k] = o.limiter
            outNames[k] = o.name
        }
        routes = (0..<inputs).map { _ in Array(repeating: false, count: outputs) }
        sends = Array(repeating: Array(repeating: 0, count: outputs), count: inputs)
        for r in scene.routes {
            guard let i = Int(r.from.dropFirst(3)), let j = Int(r.to.dropFirst(4)),
                  i < inputs, j < outputs else { continue }
            routes[i][j] = r.enabled
            sends[i][j] = r.sendDb
        }
        linkedPairs = scene.linkedPairs.count == inputs / 2 ? scene.linkedPairs : Array(repeating: false, count: inputs / 2)
        missingRoles = missingBindings(scene.bindings)
        finishLoad(name: scene.name, quiet: quiet,
                   missingNote: missingRoles.isEmpty ? "" : " Peran tidak tersedia: \(missingRoles.joined(separator: ", ")).")
    }

    private func applyLegacy(_ legacy: LegacySceneFile, name: String, quiet: Bool) {
        guard legacy.routes.count == inputs, legacy.routes.allSatisfy({ $0.count == outputs }),
              legacy.gains.count == inputs, legacy.mutes.count == inputs else {
            status = "Preset \"\(name)\" tidak cocok dengan perangkat yang terbuka"
            return
        }
        routes = legacy.routes
        gains = legacy.gains
        mutes = legacy.mutes
        sends = legacy.sends.flatMap { rows in rows.count == inputs && rows.allSatisfy({ $0.count == outputs }) ? rows : nil }
            ?? Array(repeating: Array(repeating: 0, count: outputs), count: inputs)
        linkedPairs = legacy.linkedPairs.flatMap { $0.count == inputs / 2 ? $0 : nil } ?? Array(repeating: false, count: inputs / 2)
        outputGains = legacy.outputGains.flatMap { $0.count == outputs ? $0 : nil } ?? Array(repeating: 0, count: outputs)
        outputMutes = legacy.outputMutes.flatMap { $0.count == outputs ? $0 : nil } ?? Array(repeating: false, count: outputs)
        finishLoad(name: name, quiet: quiet, missingNote: " (format lama: efek DSP tidak ikut dimuat)")
    }

    private func finishLoad(name: String, quiet: Bool, missingNote: String) {
        currentPreset = name
        UserDefaults.standard.set(name, forKey: "lastPreset")
        applyAll()
        for i in 0..<inputs where !fx[i].insert.isEmpty && !isInsertLoaded(i, fx[i].insert) {
            setInsert(i, fx[i].insert)
        }
        if !quiet { status = "Preset \"\(name)\" dimuat.\(missingNote)" }
    }

    private func isInsertLoaded(_ i: Int, _ name: String) -> Bool {
        guard let h = io else { return false }
        var buf = [CChar](repeating: 0, count: 256)
        ah_io_insert_name(h, Int32(i), &buf, 256)
        return String(cString: buf) == name
    }

    func deletePreset(_ name: String) {
        try? FileManager.default.removeItem(at: presetsDir.appendingPathComponent("\(name).json"))
        if currentPreset == name { currentPreset = "" }
        refreshPresets()
        status = "Preset \"\(name)\" dihapus"
    }

    // MARK: - Template routing

    // Template dibuat dari peran: mic = input fisik 1, monitor = output fisik 1,
    // virtual = output virtual pertama (jika ada). Template juga mengatur DSP mic.
    func applyTemplate(_ name: String) {
        guard isOpen, physIn >= 1, physOut >= 1 else { status = "Template butuh input dan output fisik"; return }
        let hasVirtualOut = outputs > physOut
        let virtualOut = physOut
        routes = (0..<inputs).map { _ in Array(repeating: false, count: outputs) }
        switch name {
        case "Podcast":
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → rekam/streaming
            fx[0].gate = GateFx(enabled: true, thresholdDb: -45, attackMs: 2, releaseMs: 150, holdMs: 80)
            fx[0].comp = CompFx(enabled: true, thresholdDb: -18, ratio: 3, attackMs: 8, releaseMs: 120, makeupDb: 4, kneeDb: 6)
            fx[0].eq = EqFx(enabled: true, lowDb: 0, lowHz: 120, midDb: 2, midHz: 3000, midQ: 0.8, highDb: 0, highHz: 8000)
        case "Streaming":
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → virtual out (OBS)
        case "Online Meeting":
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → aplikasi panggilan
            if inputs > physIn { routes[physIn][0] = true }      // suara panggilan → monitor
        case "Recording":
            routes[0][0] = true                                  // mic → monitor saja
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → aplikasi rekam
        default:
            status = "Template tidak dikenal"
            return
        }
        // Route virtual → virtual tidak pernah dibuat oleh template
        for i in 0..<inputs { for j in 0..<outputs where i >= physIn && j >= physOut { routes[i][j] = false } }
        applyAll()
        currentPreset = ""
        status = "Template \(name) diterapkan"
    }

    // MARK: - Diagnostik

    // Redaksi: nama perangkat dan UID diganti, supaya laporan aman dibagikan.
    func exportDiagnostics() {
        let df = ISO8601DateFormatter()
        let dev = redactDiagnostics ? "[disembunyikan]" : deviceName
        var lines: [String] = []
        lines.append("AudioHub diagnostik")
        lines.append("Waktu: \(df.string(from: Date()))")
        lines.append("Redaksi identitas: \(redactDiagnostics ? "ya" : "tidak")")
        lines.append("Perangkat fisik: \(dev)")
        lines.append("Virtual 1: \(useVirtual), Virtual 2: \(useVirtual2)")
        lines.append("Input: \(inputs) (fisik \(physIn)), Output: \(outputs) (fisik \(physOut))")
        lines.append("Status: \(status)")
        lines.append("Sample rate: \(meters.sampleRate) Hz, buffer: \(meters.bufferFrames) frame")
        lines.append("Callback: \(meters.callbacks), dropout: \(meters.dropouts)")
        lines.append("")
        lines.append("Peran (binding):")
        for b in currentBindings() {
            let name = redactDiagnostics ? "[disembunyikan]" : b.deviceName
            let uid = redactDiagnostics ? "[disembunyikan]" : b.deviceUID
            lines.append("  \(b.role): \(b.kind), perangkat \(name), kanal \(b.channel), uid \(uid)")
        }
        lines.append("")
        lines.append("Routing (input → output, send dB):")
        for i in 0..<inputs {
            for j in 0..<outputs where routes[i][j] {
                lines.append("  \(inLabel(i)) → \(outLabel(j)) : \(String(format: "%.0f", sends[i][j])) dB")
            }
        }
        lines.append("")
        for i in 0..<inputs {
            lines.append("\(inLabel(i)): gain \(String(format: "%.1f", gains[i])) dB, bisu \(mutes[i]), plugin \(fx[i].insert.isEmpty ? "-" : "terpasang")")
        }
        for j in 0..<outputs {
            lines.append("\(outLabel(j)): gain \(String(format: "%.1f", outputGains[j])) dB, bisu \(outputMutes[j]), limiter \(outFx[j].enabled ? "aktif" : "mati")")
        }
        let stamp = df.string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let url = appSupport.appendingPathComponent("diagnostik-\(stamp).txt")
        do {
            try lines.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8)
            status = "Diagnostik disimpan: \(url.lastPathComponent)"
        } catch {
            status = "Gagal menyimpan diagnostik: \(error.localizedDescription)"
        }
    }
}

func toDB(_ v: Float) -> Float {
    v > 0 ? 20 * log10(v) : -120
}
