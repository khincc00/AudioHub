import AppKit
import CoreAudio
import Foundation
import SwiftUI

// Edisi demo dibangun dengan `DEMO=1 sh apps/macos/AudioHub/build.sh`, yang menambahkan -D DEMO.
// Edisi penuh tidak terpengaruh. Batasan ini dipakai di engine dan di UI.
enum Edition {
    #if DEMO
    static let isDemo = true
    #else
    static let isDemo = false
    #endif

    // Jumlah sel route yang boleh aktif sekaligus. Delapan cukup untuk mic ke monitor dan ke aplikasi panggilan.
    static var maxActiveRoutes: Int { isDemo ? 8 : Int.max }
    static let contactURL = URL(string: "https://khincreator.com")!  // info langganan dari lencana DEMO
    static let purchaseURL = URL(string: "http://lynk.id/khincc/y856pq2k9w9p/checkout")!  // checkout langganan
    static var allowsExtraVirtuals: Bool { !isDemo }  // VC 3/4 dan Loopback; demo hanya VC 1/2
    static var allowsAppCapture: Bool { !isDemo }
    static var allowsDynamics: Bool { !isDemo }    // gate, EQ, kompresor, delay, limiter, rantai FX
    static var allowsRecording: Bool { !isDemo }
    static var allowsPresets: Bool { !isDemo }     // preset dan template
}

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
    var insert = ""           // nama effect Audio Unit atau VST2, kosong = tanpa plugin
    var insertState = ""      // state plugin (base64): parameter AU atau VST2, dipulihkan saat preset dimuat
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
        insertState = try c.decodeIfPresent(String.self, forKey: .insertState) ?? ""
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
    var useVirtual3: Bool? = nil  // opsional: preset lama tidak punya Loopback
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

// Peran perangkat virtual. Menentukan nama kanalnya: VC In/Out 1–2, VC In/Out 3–4, atau Loopback 1–2.
enum VirtualRole {
    case vc1, vc2, loopback
}

// Proses audio di macOS yang bisa ditangkap sebagai sumber input (process tap, macOS 14.4+).
struct AudioProcess: Identifiable, Hashable {
    let objectID: AudioObjectID
    let bundleID: String
    let name: String
    let isOutputting: Bool
    var id: String { bundleID }
}

// Tap yang aktif untuk sesi berjalan. Urutan sama dengan kanal tap di agregat.
struct AppTap {
    let name: String
    let bundleID: String
    let tapID: AudioObjectID
    let uid: String
}

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
    @Published var status = L10n.t("Belum dibuka")
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

    // Tiga peran virtual: VC 1/2, VC 3/4, dan Loopback 1/2. Dipilih dari perangkat virtual yang terpasang, apa pun vendornya.
    // Kosong = tidak dipakai. Disimpan supaya tetap terpilih setelah aplikasi dibuka ulang.
    @Published var virtualUID1: String = UserDefaults.standard.string(forKey: "virtualUID1") ?? "" {
        didSet { UserDefaults.standard.set(virtualUID1, forKey: "virtualUID1") }
    }
    @Published var virtualUID2: String = UserDefaults.standard.string(forKey: "virtualUID2") ?? "" {
        didSet { UserDefaults.standard.set(virtualUID2, forKey: "virtualUID2") }
    }
    @Published var virtualUID3: String = UserDefaults.standard.string(forKey: "virtualUID3") ?? "" {
        didSet { UserDefaults.standard.set(virtualUID3, forKey: "virtualUID3") }
    }

    // Nilai turunan untuk skema scene dan diagnostik.
    var useVirtual: Bool { !virtualUID1.isEmpty }
    var useVirtual2: Bool { !virtualUID2.isEmpty }
    var useVirtual3: Bool { !virtualUID3.isEmpty }

    // Aplikasi yang audionya ditangkap sebagai sumber input. Disimpan per bundle ID. Berlaku setelah Buka.
    @Published var tapBundleIDs: [String] = UserDefaults.standard.stringArray(forKey: "tapBundleIDs") ?? [] {
        didSet { UserDefaults.standard.set(tapBundleIDs, forKey: "tapBundleIDs") }
    }
    // Aplikasi yang diisolasi: audionya tidak keluar lagi di output asli, hanya lewat AudioHub. Berlaku setelah Buka.
    @Published var tapIsolateBundleIDs: [String] = UserDefaults.standard.stringArray(forKey: "tapIsolateBundleIDs") ?? [] {
        didSet { UserDefaults.standard.set(tapIsolateBundleIDs, forKey: "tapIsolateBundleIDs") }
    }
    // File plugin VST2 (.vst atau .dylib) yang dipilih pengguna. Muncul di daftar plugin sebagai "VST2: <nama file>".
    @Published var vst2Paths: [String] = UserDefaults.standard.stringArray(forKey: "vst2Paths") ?? [] {
        didSet { UserDefaults.standard.set(vst2Paths, forKey: "vst2Paths") }
    }
    // Tap yang aktif untuk sesi berjalan, sesuai urutan kanal tap di agregat.
    private(set) var appTaps: [AppTap] = []
    // Catatan tap dari pembukaan terakhir. Ditampilkan setelah status pembukaan.
    private var tapNote = ""

    // Perangkat virtual aktif jika sisi output (VC Out) punya route yang tersambung. Sisi input (VC In) perangkat
    // itu hanya menerima audio saat output-nya aktif; kalau output mati, VC In tidak menerima apa-apa.
    @Published private(set) var deviceLive: [VirtualRole: Bool] = [:]

    // Pilihan virtual yang terisi, dengan perannya, urut VC 1/2, VC 3/4, lalu Loopback.
    var selectedVirtuals: [(role: VirtualRole, uid: String)] {
        [(.vc1, virtualUID1), (.vc2, virtualUID2), (.loopback, virtualUID3)]
            .filter { !$0.uid.isEmpty && (Edition.allowsExtraVirtuals || $0.role == .vc1) }
    }

    // Perangkat virtual yang bisa dipilih untuk satu peran: transport virtual, punya input dan output,
    // bukan perangkat fisik utama, dan tidak dipakai peran lain (uids berisi UID peran lain).
    func virtualCandidates(excluding uids: [String]) -> [DeviceInfo] {
        devices.filter {
            $0.isVirtual && $0.inCh > 0 && $0.outCh > 0 && !uids.contains($0.uid) && $0.name != deviceName
        }
    }

    // Virtual device yang masuk aggregate saat terakhir dibuka, dengan perannya, sesuai urutan kanal.
    private(set) var openedVirtuals: [(role: VirtualRole, uid: String)] = []

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
        ah_io_vst2_set_paths(vst2Paths.joined(separator: "\n"))
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
        pickDemoVirtualIfNeeded()
        refreshMissing()
        loadCapabilities()
    }

    // Edisi demo hanya memakai VC 1/2. Jika belum ada yang dipilih, pilih avhub_virtual1, atau perangkat virtual non-Loopback pertama.
    private func pickDemoVirtualIfNeeded() {
        guard Edition.isDemo, virtualUID1.isEmpty else { return }
        let candidates = virtualCandidates(excluding: [])
        let pick = candidates.first { $0.name.lowercased().contains("virtual1") }
            ?? candidates.first { !$0.name.lowercased().contains("loopback") }
        if let pick { virtualUID1 = pick.uid }
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

    // Peran yang perangkatnya tidak tersedia saat ini. Tidak pernah diganti diam-diam.
    private func refreshMissing() {
        let present = Set(devices.map { $0.uid })
        var missing: [String] = []
        if !devices.contains(where: { $0.name == deviceName }) { missing.append(deviceName) }
        for slot in selectedVirtuals where !present.contains(slot.uid) { missing.append(slot.uid) }
        missingRoles = missing
    }

    // MARK: - Buka dan tutup perangkat

    func open() {
        close()
        refreshDevices()
        let picks = selectedVirtuals
        if Set(picks.map { $0.uid }).count != picks.count {
            status = L10n.t("VC 1/2, VC 3/4, dan Loopback harus perangkat yang berbeda")
            return
        }
        var buf = [CChar](repeating: 0, count: 256)
        let handle: OpaquePointer?
        // Pilihan buffer dan rate dipasang sebelum perangkat dibuka. Virtual device ikut memakai buffer yang sama.
        ah_io_set_preferences(prefBuffer, prefSampleRate)
        // Virtual yang kosong dilewati. Slot pertama yang terisi menjadi clock agregat, dan urutan kanalnya mengikuti picks.
        openedVirtuals = picks
        if let first = picks.first {
            let uid2: String? = picks.count > 1 ? picks[1].uid : nil
            let uid3: String? = picks.count > 2 ? picks[2].uid : nil
            // Tap aplikasi masuk ke agregat, jadi hanya bisa dipakai bersama minimal satu virtual.
            let tapUIDs = makeAppTaps()
            let cUIDs: [UnsafePointer<CChar>?] = tapUIDs.map { UnsafePointer(strdup($0)) }
            defer { cUIDs.forEach { free(UnsafeMutableRawPointer(mutating: $0)) } }
            handle = cUIDs.withUnsafeBufferPointer { ptrs in
                ah_io_open_with_virtuals(deviceName, first.uid, uid2, uid3, ptrs.baseAddress, Int32(cUIDs.count), &buf, 256)
            }
            if handle == nil { releaseAppTaps() }
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
        // Preset terakhir tidak dimuat otomatis: semua route dan input mulai dari keadaan mati.
        ioGeneration += 1
        status = L10n.f("Terbuka: %d in (%d fisik), %d out (%d fisik)", inputs, physIn, outputs, physOut)
        if !tapNote.isEmpty { status += " · " + tapNote }
    }

    // Default: semua route mati, gain 0 dB, send 0 dB, tidak dibisukan. Panel input selalu mendengarkan;
    // suara baru diteruskan jika sel matriks ditekan. Seperti default Routing Center.
    func resetDefaults() {
        routes = (0..<inputs).map { _ in Array(repeating: false, count: outputs) }
        deviceLive = [:]
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
        refreshLive()
        applyAll()
    }

    private func loadEffectNames() {
        var buf = [CChar](repeating: 0, count: 8192)
        _ = ah_io_list_effects(&buf, 8192)
        let text = String(cString: buf)
        effectNames = text.split(separator: "\n").map(String.init).sorted()
    }

    // Nama default mengikuti peran perangkat, bukan posisinya di agregat: VC 1/2 dan VC 3/4 memakai nomor kanal
    // tetap, dan Loopback dinomori dari 1 di perangkatnya sendiri.
    private func virtualLabel(owner: Int, local: Int, output: Bool) -> String? {
        guard owner >= 1, owner <= openedVirtuals.count else { return nil }
        let side = output ? "Out" : "In"
        switch openedVirtuals[owner - 1].role {
        case .vc1: return "VC \(side) \(local)"
        case .vc2: return "VC \(side) \(local + 2)"
        case .loopback: return "Loopback \(local)"
        }
    }

    func defaultInLabel(_ i: Int) -> String {
        if i < physIn { return "MIC In \(i + 1)" }
        let (owner, local) = channelOwner(i, output: false)
        if owner == 4 { return tapLabel(local: local) }
        return virtualLabel(owner: owner, local: local, output: false) ?? "Virtual In \(i - physIn + 1)"
    }

    func defaultOutLabel(_ j: Int) -> String {
        if j < physOut { return "HP Out \(j + 1)" }
        let (owner, local) = channelOwner(j, output: true)
        return virtualLabel(owner: owner, local: local, output: true) ?? "Virtual Out \(j - physOut + 1)"
    }

    // Kanal pertama dari sebuah kelompok (fisik, VC 1/2, VC 3/4, Loopback). Dipakai untuk memberi jarak di matriks.
    func startsGroup(_ index: Int, output: Bool) -> Bool {
        index > 0 && channelOwner(index, output: output).owner != channelOwner(index - 1, output: output).owner
    }

    // Kanal dikelompokkan per pemilik, lalu per pasang stereo. Dipakai untuk layar utama.
    func channelPairs(output: Bool) -> [[Int]] {
        let count = output ? outputs : inputs
        var groups: [[Int]] = []
        var owner = Int.min
        for i in 0..<count {
            let o = channelOwner(i, output: output).owner
            if groups.isEmpty || o != owner || groups[groups.count - 1].count == 2 {
                groups.append([])
                owner = o
            }
            groups[groups.count - 1].append(i)
        }
        return groups
    }

    // MARK: Tangkap audio per aplikasi (process tap)

    // Proses dengan bundle ID yang ada di sistem. Yang sedang memutar audio ditandai.
    func audioProcesses() -> [AudioProcess] {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyProcessObjectList,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        let system = AudioObjectID(kAudioObjectSystemObject)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr, size > 0 else { return [] }
        var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return [] }
        var result: [AudioProcess] = []
        for pid in ids {
            guard let bundle = processString(pid, kAudioProcessPropertyBundleID), !bundle.isEmpty,
                  bundle != Bundle.main.bundleIdentifier,
                  // Hanya aplikasi yang dikenal sistem sebagai aplikasi biasa, bukan daemon sistem
                  let app = NSRunningApplication.runningApplications(withBundleIdentifier: bundle).first,
                  let name = app.localizedName else { continue }
            result.append(AudioProcess(objectID: pid, bundleID: bundle, name: name,
                                       isOutputting: processFlag(pid, kAudioProcessPropertyIsRunningOutput)))
        }
        // Yang sedang memutar audio ditaruh di atas, lalu urut nama
        return result.sorted {
            if $0.isOutputting != $1.isOutputting { return $0.isOutputting }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }

    private func processString(_ obj: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
        var addr = AudioObjectPropertyAddress(mSelector: selector,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: CFString? = nil
        var size = UInt32(MemoryLayout<CFString?>.size)
        guard AudioObjectGetPropertyData(obj, &addr, 0, nil, &size, &value) == noErr else { return nil }
        return value as String?
    }

    private func processFlag(_ obj: AudioObjectID, _ selector: AudioObjectPropertySelector) -> Bool {
        var addr = AudioObjectPropertyAddress(mSelector: selector,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(obj, &addr, 0, nil, &size, &value) == noErr else { return false }
        return value != 0
    }

    // Membuat satu tap per aplikasi yang dipilih dan sedang berjalan. Tap hanya dibuat saat perangkat dibuka.
    // Mengembalikan UID tap untuk dimasukkan ke agregat. Tap yang gagal dilaporkan lewat status.
    private func makeAppTaps() -> [String] {
        releaseAppTaps()
        let processes = audioProcesses()
        var notRunning: [String] = []
        var failed: [String] = []
        for bundle in (Edition.allowsAppCapture ? tapBundleIDs : []).prefix(4) {
            guard let proc = processes.first(where: { $0.bundleID == bundle }) else {
                notRunning.append(bundle)
                continue
            }
            let desc = CATapDescription(stereoMixdownOfProcesses: [proc.objectID])
            // Tanpa isolasi, audio tetap keluar lewat output asli (rawValue 0, CATapUnmuted).
            // Dengan isolasi, output asli dibisukan selama ditangkap (rawValue 2, CATapMutedWhenTapped).
            desc.muteBehavior = CATapMuteBehavior(rawValue: tapIsolateBundleIDs.contains(bundle) ? 2 : 0)!
            desc.name = proc.name
            desc.isPrivate = true
            var tapID = AudioObjectID(kAudioObjectUnknown)
            let st = AudioHardwareCreateProcessTap(desc, &tapID)
            if st == noErr {
                // UID tap dari sistem, dipakai agregat. Agregat dengan tap harus private.
                appTaps.append(AppTap(name: proc.name, bundleID: bundle, tapID: tapID, uid: tapUID(tapID) ?? desc.uuid.uuidString))
            } else {
                failed.append("\(proc.name) (\(st))")
            }
        }
        tapNote = ""
        if !notRunning.isEmpty {
            tapNote = L10n.f("Tidak berjalan: %@", notRunning.joined(separator: ", "))
        }
        if !failed.isEmpty {
            tapNote = L10n.f("Gagal menangkap %@. Beri izin Audio Capture di Pengaturan Sistem.", failed.joined(separator: ", "))
        }
        return appTaps.map { $0.uid }
    }

    private func tapUID(_ tapID: AudioObjectID) -> String? {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioTapPropertyUID,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: CFString? = nil
        var size = UInt32(MemoryLayout<CFString?>.size)
        guard AudioObjectGetPropertyData(tapID, &addr, 0, nil, &size, &value) == noErr else { return nil }
        return value as String?
    }

    private func releaseAppTaps() {
        for t in appTaps { AudioHardwareDestroyProcessTap(t.tapID) }
        appTaps = []
    }

    // Nama kanal tap: "Spotify 1", "Spotify 2", lalu aplikasi berikutnya. Tiap aplikasi punya dua kanal.
    private func tapLabel(local: Int) -> String {
        let idx = (local - 1) / 2
        let ch = (local - 1) % 2 + 1
        guard idx < appTaps.count else { return L10n.f("Aplikasi %d", local) }
        return "\(appTaps[idx].name) \(ch)"
    }


    // Peran perangkat virtual untuk kanal ini, atau nil jika kanal fisik.
    func virtualRole(channel i: Int, output: Bool) -> VirtualRole? {
        let (owner, _) = channelOwner(i, output: output)
        guard owner >= 1, owner <= openedVirtuals.count else { return nil }
        return openedVirtuals[owner - 1].role
    }

    // Kanal aktif jika fisik, atau jika perangkat virtualnya aktif.
    func isLive(channel i: Int, output: Bool) -> Bool {
        guard let role = virtualRole(channel: i, output: output) else { return true }
        return deviceLive[role] ?? false
    }

    // Menghitung ulang status perangkat virtual dari route yang ada. Jika berubah, semua route dikirim ulang.
    private func refreshLive() {
        var next: [VirtualRole: Bool] = [:]
        for slot in openedVirtuals {
            next[slot.role] = (0..<outputs).contains { j in
                virtualRole(channel: j, output: true) == slot.role && routes.contains { $0[j] }
            }
        }
        guard next != deviceLive else { return }
        deviceLive = next
        for i in 0..<inputs { for j in 0..<outputs { pushRoute(i, j) } }
    }

    // Sel matriks pasangan: tersambung jika ada route aktif di antara kedua pasangan.
    func pairRouted(_ ins: [Int], _ outs: [Int]) -> Bool {
        ins.contains { i in outs.contains { j in routes[i][j] } }
    }

    // Sel matriks pasangan boleh dipakai jika ada kombinasi kanal yang diizinkan (virtual ke virtual perangkat sama tidak).
    // Semua sel boleh dipencet, termasuk yang menyalakan perangkat virtual. Audio hanya lewat jika kedua sisinya aktif (effectiveRoute).
    func pairAllowed(_ ins: [Int], _ outs: [Int]) -> Bool {
        ins.contains { i in outs.contains { j in routeAllowed(i, j) } }
    }

    // Menyambung atau memutus semua route antara dua pasangan, hanya untuk kombinasi yang diizinkan.
    func setPairRoute(_ ins: [Int], _ outs: [Int], _ on: Bool) {
        for i in ins { for j in outs where routeAllowed(i, j) { setRoute(i, j, on) } }
        refreshLive()
    }

    // Judul pasangan dari nama default, bukan nama buatan pengguna: "MIC In 1/2", "VC Out 3/4", "Loopback 1/2".
    func pairTitle(_ indices: [Int], output: Bool) -> String {
        let labels = indices.map { output ? defaultOutLabel($0) : defaultInLabel($0) }
        guard let first = labels.first else { return "" }
        guard labels.count > 1 else { return first }
        let base = first.split(separator: " ").dropLast().joined(separator: " ")
        let numbers = labels.compactMap { $0.split(separator: " ").last.map(String.init) }.joined(separator: "/")
        return "\(base) \(numbers)"
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
            status = L10n.t("Berjalan")
            aliveMisses = 0
            cancelReconnect()
            timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.tick() }
        } else {
            status = L10n.t("Gagal memulai perangkat")
        }
    }

    func stop() {
        let wasRunning = running
        cancelReconnect()
        haltIO()
        if wasRunning { status = L10n.t("Dihentikan") }
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
        releaseAppTaps()
    }

    // MARK: - Sambung ulang

    private func beginReconnect() {
        cancelReconnect()
        status = L10n.t("Perangkat terputus. Mencoba menyambung ulang setiap 2 detik...")
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.attemptReconnect()
        }
    }

    private func attemptReconnect() {
        close()
        open()
        if isOpen {
            start()
            status = L10n.f("Tersambung kembali: %@", status)
        } else {
            status = L10n.t("Perangkat belum tersedia, mencoba lagi...")
        }
    }

    private func cancelReconnect() {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
    }

    // MARK: - Routing, send, gain, mute

    // Route boleh dibuat? Hanya indeks kanal yang diperiksa. Route virtual → virtual dalam perangkat yang sama boleh.
    func routeAllowed(_ i: Int, _ j: Int) -> Bool {
        guard let h = io else { return false }
        return ah_io_route_allowed(h, Int32(i), Int32(j)) == 1
    }


    func setRoute(_ i: Int, _ j: Int, _ on: Bool) {
        guard io != nil else { return }
        if on && !routeAllowed(i, j) {
            status = L10n.t("Ditolak: indeks kanal tidak valid")
            return
        }
        if on && !routes[i][j] && routes.joined().filter({ $0 }).count >= Edition.maxActiveRoutes {
            status = L10n.f("Edisi demo: maksimal %d route aktif", Edition.maxActiveRoutes)
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
        guard isLive(channel: i, output: false), isLive(channel: j, output: true) else { return false }
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
        status = soloInputs.isEmpty ? L10n.t("Solo dilepas") : L10n.f("Solo aktif (hanya monitor): %@", soloInputs.sorted().map { inLabel($0) }.joined(separator: ", "))
    }

    func setPan(_ i: Int, _ pan: Float) {
        updateInputFx(i) { $0.pan = max(-1, min(1, pan)) }
    }

    // MARK: - Rekaman

    func startRecording(output j: Int) {
        guard Edition.allowsRecording else { status = L10n.t("Fitur ini tidak tersedia di edisi demo"); return }
        guard let h = io, recordingOutput == nil else { return }
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let url = appSupport.appendingPathComponent("rekaman-\(stamp).wav")
        if ah_io_record_start(h, Int32(j), url.path) == 0 {
            recordingOutput = j
            recordedFile = url.lastPathComponent
            status = L10n.f("Merekam %@ ke %@", outLabel(j), url.lastPathComponent)
        } else {
            status = L10n.t("Gagal memulai rekaman")
        }
    }

    func stopRecording() {
        guard let h = io, recordingOutput != nil else { return }
        ah_io_record_stop(h)
        recordingOutput = nil
        var written: UInt64 = 0, dropped: UInt64 = 0
        ah_io_record_stats(h, &written, &dropped)
        status = L10n.f("Rekaman disimpan: %@ (%d frame, terbuang %d)", recordedFile, Int(written), Int(dropped))
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

    // Memasang atau melepas plugin (Audio Unit atau VST2) pada satu input. Butuh IO berhenti sebentar.
    // state, jika ada, dipulihkan setelah plugin dimuat (parameter dan pengaturan dari preset).
    func setInsert(_ i: Int, _ name: String, state: Data? = nil) {
        guard let h = io, i < fx.count else { return }
        let wasRunning = running
        if wasRunning { haltIO() }
        _ = ah_io_insert_unload(h, Int32(i))
        if name.isEmpty {
            fx[i].insert = ""
            fx[i].insertState = ""
        } else if ah_io_insert_load(h, Int32(i), name) == 0 {
            fx[i].insert = name
            if let state, !state.isEmpty {
                let bytes = [UInt8](state)
                _ = bytes.withUnsafeBufferPointer { ah_io_fx_state_set(h, Int32(i), 0, $0.baseAddress, Int32($0.count)) }
            }
            status = L10n.f("Plugin %@ dipasang di %@", name, inLabel(i))
        } else {
            fx[i].insert = ""
            fx[i].insertState = ""
            status = L10n.f("Gagal memuat plugin %@", name)
        }
        if wasRunning { start() }
    }

    // Parameter satu plugin pada input i (slot 0). Kosong jika tidak ada plugin atau IO belum dibuka.
    struct PluginParam: Identifiable {
        let id: UInt32
        let index: Int
        let name: String
        let minValue: Float
        let maxValue: Float
        var value: Float
        var text: String
    }

    func pluginParams(_ i: Int) -> [PluginParam] {
        guard let h = io, i < fx.count, !fx[i].insert.isEmpty else { return [] }
        let n = Int(ah_io_fx_param_count(h, Int32(i), 0))
        guard n > 0 else { return [] }
        return (0..<n).compactMap { k -> PluginParam? in
            var p = AHFxParam()
            guard ah_io_fx_param_get(h, Int32(i), 0, Int32(k), &p) == 0 else { return nil }
            let name = withUnsafeBytes(of: p.name) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
            return PluginParam(id: p.id, index: k, name: name, minValue: p.minValue, maxValue: p.maxValue,
                               value: p.value, text: pluginParamText(i, k))
        }
    }

    func pluginParamText(_ i: Int, _ index: Int) -> String {
        guard let h = io else { return "" }
        var buf = [CChar](repeating: 0, count: 128)
        guard ah_io_fx_param_text(h, Int32(i), 0, Int32(index), &buf, 128) == 0 else { return "" }
        return String(cString: buf)
    }

    func setPluginParam(_ i: Int, id: UInt32, value: Float) {
        guard let h = io else { return }
        _ = ah_io_fx_param_set(h, Int32(i), 0, id, value)
    }

    // State plugin pada input i dalam bentuk base64, untuk disimpan di preset. Kosong jika tidak ada plugin.
    private func pluginStateBase64(_ i: Int) -> String {
        guard let h = io, !fx[i].insert.isEmpty else { return "" }
        let size = ah_io_fx_state_get(h, Int32(i), 0, nil, 0)
        guard size > 0 else { return "" }
        var buf = [UInt8](repeating: 0, count: Int(size))
        let got = buf.withUnsafeMutableBufferPointer { ah_io_fx_state_get(h, Int32(i), 0, $0.baseAddress, size) }
        guard got == size else { return "" }
        return Data(buf).base64EncodedString()
    }

    // Nama plugin VST2 di daftar, sama dengan yang dibuat backend: "VST2: <nama file tanpa akhiran>".
    static func vst2DisplayName(_ path: String) -> String {
        let base = (path as NSString).lastPathComponent
        for suffix in [".vst.dylib", ".vst", ".dylib"] where base.hasSuffix(suffix) {
            return "VST2: " + String(base.dropLast(suffix.count))
        }
        return "VST2: " + base
    }

    func addVst2Plugin(_ path: String) {
        guard !vst2Paths.contains(path) else { return }
        vst2Paths.append(path)
        applyVst2Paths()
    }

    func removeVst2Plugin(_ path: String) {
        let name = EngineController.vst2DisplayName(path)
        for i in 0..<fx.count where fx[i].insert == name {
            if io != nil { setInsert(i, "") } else { fx[i].insert = ""; fx[i].insertState = "" }
        }
        vst2Paths.removeAll { $0 == path }
        applyVst2Paths()
    }

    private func applyVst2Paths() {
        ah_io_vst2_set_paths(vst2Paths.joined(separator: "\n"))
        loadEffectNames()
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
                status = L10n.t("Perangkat terputus. Hubungkan kembali lalu tekan Buka.")
            }
            return
        }
        aliveMisses = 0
        // Sama dengan kMaxCh di backend. Buffer harus cukup untuk semua kanal input dan output.
        var inP = [Float](repeating: 0, count: 32)
        var outP = [Float](repeating: 0, count: 32)
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
            let picks = selectedVirtuals
            return (v / 2 < picks.count ? picks[v / 2].uid : "", v % 2 + 1)
        }
        let (owner, local) = channelOwner(index, output: output)
        guard owner >= 1, owner <= openedVirtuals.count else { return ("", 0) }
        return (openedVirtuals[owner - 1].uid, local)
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
        guard Edition.allowsPresets else { status = L10n.t("Fitur ini tidak tersedia di edisi demo"); return }
        if let problem = presetNameProblem(raw) { status = problem; return }
        let name = sanitize(raw)
        let scene = SceneFile(
            name: name, deviceName: deviceName, useVirtual: useVirtual, useVirtual2: useVirtual2, useVirtual3: useVirtual3,
            bindings: currentBindings(),
            inputs: (0..<inputs).map { i in
                var f = fx[i]
                f.insertState = pluginStateBase64(i)
                return SceneInput(id: "in:\(i)", gainDb: gains[i], mute: mutes[i], fx: f)
            },
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
            status = L10n.f("Preset \"%@\" disimpan", name)
        } catch {
            status = L10n.t("Gagal menyimpan preset:") + " \(error.localizedDescription)"
        }
    }

    func loadPreset(_ name: String, quiet: Bool = false) {
        guard Edition.allowsPresets else { if !quiet { status = L10n.t("Fitur ini tidak tersedia di edisi demo") }; return }
        let url = presetsDir.appendingPathComponent("\(name).json")
        guard let data = try? Data(contentsOf: url) else {
            if !quiet { status = L10n.f("Preset \"%@\" tidak ditemukan", name) }
            return
        }
        if let scene = try? JSONDecoder().decode(SceneFile.self, from: data) {
            applyScene(scene, quiet: quiet)
        } else if let legacy = try? JSONDecoder().decode(LegacySceneFile.self, from: data) {
            applyLegacy(legacy, name: name, quiet: quiet)
        } else {
            if !quiet { status = L10n.f("Preset \"%@\" tidak bisa dibaca", name) }
        }
    }

    private func applyScene(_ scene: SceneFile, quiet: Bool) {
        guard scene.schemaVersion == 1 else { status = L10n.t("Versi preset tidak dikenal"); return }
        guard scene.inputs.count == inputs, scene.outputs.count == outputs else {
            status = L10n.f("Preset \"%@\" tidak cocok dengan jumlah kanal perangkat", scene.name)
            return
        }
        // Route yang tidak diizinkan backend (virtual ke virtual dalam perangkat yang sama) ditolak
        for r in scene.routes where r.enabled {
            guard let i = Int(r.from.dropFirst(3)), let j = Int(r.to.dropFirst(4)) else { status = L10n.t("Route tidak valid"); return }
            if !routeAllowed(i, j) {
                status = L10n.t("Preset berisi route yang tidak diizinkan, ditolak")
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
            status = L10n.f("Preset \"%@\" tidak cocok dengan perangkat yang terbuka", name)
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
        refreshLive()
        applyAll()
        // Plugin dimuat ulang jika belum terpasang, atau jika preset membawa state (parameter) yang harus dipulihkan.
        for i in 0..<inputs where !fx[i].insert.isEmpty {
            let state = fx[i].insertState.isEmpty ? nil : Data(base64Encoded: fx[i].insertState)
            if !isInsertLoaded(i, fx[i].insert) || state != nil {
                setInsert(i, fx[i].insert, state: state)
            }
        }
        if !quiet { status = L10n.f("Preset \"%@\" dimuat.", name) + missingNote }
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
        status = L10n.f("Preset \"%@\" dihapus", name)
    }

    // MARK: - Template routing

    // Template dibuat dari peran: mic = input fisik 1, monitor = output fisik 1,
    // virtual = output virtual pertama (jika ada). Template juga mengatur DSP mic.
    func applyTemplate(_ name: String) {
        guard Edition.allowsPresets else { status = L10n.t("Fitur ini tidak tersedia di edisi demo"); return }
        guard isOpen, physIn >= 1, physOut >= 1 else { status = L10n.t("Template butuh input dan output fisik"); return }
        let hasVirtualOut = outputs > physOut
        let virtualOut = physOut
        routes = (0..<inputs).map { _ in Array(repeating: false, count: outputs) }
        switch name {
        case L10n.t("Podcast"):
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → rekam/streaming
            fx[0].gate = GateFx(enabled: true, thresholdDb: -45, attackMs: 2, releaseMs: 150, holdMs: 80)
            fx[0].comp = CompFx(enabled: true, thresholdDb: -18, ratio: 3, attackMs: 8, releaseMs: 120, makeupDb: 4, kneeDb: 6)
            fx[0].eq = EqFx(enabled: true, lowDb: 0, lowHz: 120, midDb: 2, midHz: 3000, midQ: 0.8, highDb: 0, highHz: 8000)
        case L10n.t("Streaming"):
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → virtual out (OBS)
        case L10n.t("Online Meeting"):
            routes[0][0] = true                                  // mic → monitor
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → aplikasi panggilan
            if inputs > physIn { routes[physIn][0] = true }      // suara panggilan → monitor
        case L10n.t("Recording"):
            routes[0][0] = true                                  // mic → monitor saja
            if hasVirtualOut { routes[0][virtualOut] = true }    // mic → aplikasi rekam
        default:
            status = L10n.t("Template tidak dikenal")
            return
        }
        // Route virtual → virtual tidak pernah dibuat oleh template
        for i in 0..<inputs { for j in 0..<outputs where i >= physIn && j >= physOut { routes[i][j] = false } }
        refreshLive()
        applyAll()
        currentPreset = ""
        status = L10n.f("Template %@ diterapkan", name)
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
        lines.append("VC 1/2: \(useVirtual), VC 3/4: \(useVirtual2), Loopback 1/2: \(useVirtual3)")
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

// MARK: - Bahasa antarmuka

// Bahasa antarmuka. Default Inggris; Bahasa Indonesia tetap tersedia.
// Kunci kamus adalah teks Indonesia asli, jadi teks yang belum punya terjemahan tetap tampil apa adanya.
enum AppLanguage: String, CaseIterable, Identifiable {
    case en
    case id

    var id: String { rawValue }

    // Nama bahasa ditulis dalam bahasanya sendiri, supaya selalu bisa dikenali.
    var label: String {
        switch self {
        case .en: return "English"
        case .id: return "Bahasa Indonesia"
        }
    }
}

enum L10n {
    static let storageKey = "appLanguage"

    static var current: AppLanguage {
        AppLanguage(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? .en
    }

    static func t(_ key: String) -> String {
        guard current == .en else { return key }
        return english[key] ?? key
    }

    // Teks dengan argumen (format printf), mis. "Terbuka: %d in (%d fisik)…".
    static func f(_ key: String, _ args: CVarArg...) -> String {
        String(format: t(key), arguments: args)
    }

    static let english: [String: String] = [
        // Umum
        "Bahasa": "Language",
        "Tampilan": "Appearance",
        "Sistem": "System",
        "Terang": "Light",
        "Gelap": "Dark",
        "Perangkat": "Device",
        "Status": "Status",
        "Buka": "Open",
        "Mulai": "Start",
        "Hentikan": "Stop",
        "Berhenti": "Stopped",
        "Berjalan": "Running",
        "Belum dibuka": "Not opened",
        "Dihentikan": "Stopped",
        "Tidak ada": "None",
        "Batal": "Cancel",
        "Simpan": "Save",
        "Hapus": "Delete",
        "Muat": "Load",
        "Timpa": "Overwrite",
        "Ekspor": "Export",
        "Lanjutan": "Advanced",
        "Ringkasan": "Summary",
        "Nama": "Name",
        "Template": "Template",
        "Solo": "Solo",
        "Pan": "Pan",
        "Gain": "Gain",
        "Buffer": "Buffer",
        "Dropout": "Dropout",
        "Callback": "Callback",
        "Engine": "Engine",
        "Plugin": "Plugin",
        "Rekaman": "Recording",
        "Recording": "Recording",
        "Streaming": "Streaming",
        "Podcast": "Podcast",
        "Online Meeting": "Online Meeting",
        "Fisik": "Physical",
        "dB": "dB",

        // Bagian utama
        "Routing Center": "Routing Center",
        "Routing Preset": "Routing Preset",
        "Belum ada perangkat terbuka": "No device open",
        "Pilih perangkat di kanan, lalu tekan Buka.": "Choose a device on the right, then press Open.",
        "Buka ulang perangkat dengan pengaturan di atas": "Reopen the device with the settings above",
        "Pengaturan Perangkat": "Device Settings",
        "Peran perangkat": "Device roles",
        "Kontrol perangkat": "Device controls",
        "Sambung ulang otomatis": "Auto reconnect",
        "Sample rate": "Sample rate",
        "Latensi input": "Input latency",
        "Latensi output": "Output latency",
        "Scene aktif": "Active scene",
        "Perbarui daftar perangkat": "Refresh device list",
        "Reset ke default": "Reset to default",
        "Pilih preset": "Choose preset",
        "Nama preset": "Preset name",
        "Simpan sebagai": "Save as",
        "Mengatur routing dan DSP mic sesuai skenario.": "Sets mic routing and DSP for each scenario.",
        "Putuskan koneksi": "Disconnect",
        "Tautkan pasangan stereo": "Link stereo pair",
        "Atur kanal…": "Channel settings…",
        "Bisukan": "Mute",
        "Bisukan kanal": "Mute channel",
        "Batalkan bisu": "Unmute",
        "Bisukan output": "Mute output",
        "BISU": "MUTED",
        "Klik untuk mengganti warna": "Click to change color",
        "Tampilkan graph": "Show graph",
        "Tampilkan matriks": "Show matrix",
        "Graph hanya menampilkan. Ubah koneksi di tampilan Matriks.": "Graph is view-only. Change connections in the Matrix view.",
        "Rekam output ini ke berkas WAV": "Record this output to a WAV file",
        "Hentikan rekaman": "Stop recording",
        "Limiter dan rekam": "Limiter and record",
        "Solo: hanya memengaruhi monitor": "Solo: affects monitor only",
        "Sembunyikan nama dan UID perangkat": "Hide device names and UIDs",
        "Simpan laporan diagnostik": "Save diagnostic report",
        "Laporan berisi routing, gain, dan status. Aman dibagikan bila nama perangkat disembunyikan.": "The report contains routing, gain, and status. Safe to share if device names are hidden.",

        // Kanal, DSP, dan efek
        "Dinamika, EQ, delay, dan plugin": "Dynamics, EQ, delay, and plugins",
        "Aktifkan EQ": "Enable EQ",
        "Aktifkan delay": "Enable delay",
        "Aktifkan gate": "Enable gate",
        "Aktifkan kompresor": "Enable compressor",
        "Aktifkan limiter": "Enable limiter",
        "Gate": "Gate",
        "EQ": "EQ",
        "Kompresor": "Compressor",
        "Limiter": "Limiter",
        "Delay": "Delay",
        "Delay menambah latensi sebesar waktunya.": "Delay adds latency equal to its time.",
        "Ceiling": "Ceiling",
        "Lookahead": "Lookahead",
        "Lookahead menunda sinyal supaya batas tidak terlewati. Latensi bertambah sebesar nilainya.": "Lookahead delays the signal so the ceiling is not exceeded. Latency increases by this amount.",
        "Sidechain": "Sidechain",
        "Tanpa sidechain": "No sidechain",
        "Tanpa plugin": "No plugin",
        "Audio Unit": "Audio Unit",
        "Buka Rantai FX…": "Open FX chain…",
        "Rantai FX menyimpan beberapa Audio Unit per track dengan urutan, bypass, dan parameter.": "The FX chain stores several Audio Units per track, with order, bypass, and parameters.",
        "Pemasangan plugin menghentikan audio sebentar. Parameter dan pengaturan ikut tersimpan di preset.": "Installing a plugin briefly stops audio. Parameters and settings are saved with the preset.",
        "Parameter plugin": "Plugin parameters",
        "Tidak ada parameter yang bisa diatur.": "No adjustable parameters.",
        "Plugin VST2": "VST2 plugins",
        "Tambah plugin VST2…": "Add VST2 plugin…",
        "Plugin VST2 dari file .vst atau .dylib. Plugin yang memakai SWELL dari REAPER (misalnya ReaFIR) tidak bisa dimuat di luar REAPER.": "VST2 plugins from .vst or .dylib files. Plugins that use REAPER's SWELL (for example ReaFIR) cannot load outside REAPER.",
        "Plugin %@ dipasang di %@": "Plugin %@ installed on %@",
        "Gagal memuat plugin %@": "Failed to load plugin %@",
        "Clip": "Clip",
        "Clip. Klik untuk menghapus": "Clip. Click to clear",

        // Pesan status
        "Ditolak: indeks kanal tidak valid": "Rejected: invalid channel index",
        "Gagal memulai perangkat": "Failed to start device",
        "Gagal memulai rekaman": "Failed to start recording",
        "Perangkat belum tersedia, mencoba lagi...": "Device not available yet, retrying...",
        "Perangkat terputus. Hubungkan kembali lalu tekan Buka.": "Device disconnected. Reconnect it and press Open.",
        "Perangkat terputus. Mencoba menyambung ulang setiap 2 detik...": "Device disconnected. Retrying every 2 seconds...",
        "Preset berisi route yang tidak diizinkan, ditolak": "Preset contains disallowed routes, rejected",
        "Route tidak valid": "Invalid route",
        "Template butuh input dan output fisik": "Template needs physical input and output",
        "Template tidak dikenal": "Unknown template",
        "VC 1/2, VC 3/4, dan Loopback harus perangkat yang berbeda": "VC 1/2, VC 3/4, and Loopback must be different devices",
        "Versi preset tidak dikenal": "Unknown preset version",
        "Terbuka: %d in (%d fisik), %d out (%d fisik)": "Opened: %d in (%d physical), %d out (%d physical)",
        "Tersambung kembali: %@": "Reconnected: %@",
        "Preset \"%@\" disimpan": "Preset \"%@\" saved",
        "Preset \"%@\" tidak ditemukan": "Preset \"%@\" not found",
        "Template %@ diterapkan": "Template %@ applied",
        "Solo dilepas": "Solo cleared",
        "Solo aktif (hanya monitor): %@": "Solo active (monitor only): %@",
        "Diagnostik disimpan: %@": "Diagnostics saved: %@",
        "Rekaman disimpan: %@ (%d frame, terbuang %d)": "Recording saved: %@ (%d frames, dropped %d)",
        "Peran tidak tersedia: %@.": "Role unavailable: %@.",
        "Tidak tersedia: %@. Scene tidak diganti otomatis.": "Unavailable: %@. Scene is not replaced automatically.",
        "%@ (tidak tersedia)": "%@ (unavailable)",
        "%d kanal": "%d channels",
        "%d sumber": "%d sources",
        "Tidak berjalan: %@": "Not running: %@",
        "Gagal menangkap %@. Beri izin Audio Capture di Pengaturan Sistem.": "Could not capture %@. Allow Audio Capture in System Settings.",
        "Aplikasi %d": "App %d",
        "Isolasi": "Isolate",
        "Isolasi: audio aplikasi tidak lagi keluar di output asli, hanya lewat AudioHub. Berlaku setelah Buka.": "Isolate: the app's audio no longer plays on the main output, only through AudioHub. Applies after Open.",
        "Aplikasi (tangkap audio)": "Apps (capture audio)",
        "Pilih aplikasi yang audionya dijadikan sumber input. Berlaku setelah Buka. Saat pertama kali, macOS meminta izin Audio Capture.": "Choose apps whose audio becomes an input source. Applies after Open. The first time, macOS asks for Audio Capture permission.",
        "Belum ada aplikasi yang memutar audio.": "No app is playing audio yet.",
        "Tangkap aplikasi butuh VC 1/2 dipilih.": "Capturing apps needs VC 1/2 selected.",
        "Gagal menyimpan preset:": "Failed to save preset:",
        "Hapus preset \"%@\"?": "Delete preset \"%@\"?",
        "Timpa preset \"%@\"?": "Overwrite preset \"%@\"?",
        "Preset \"%@\" dimuat.": "Preset \"%@\" loaded.",
        "Preset \"%@\" dihapus": "Preset \"%@\" deleted",
        "Preset \"%@\" tidak cocok dengan jumlah kanal perangkat": "Preset \"%@\" does not match the device channel count",
        "Preset \"%@\" tidak cocok dengan perangkat yang terbuka": "Preset \"%@\" does not match the open device",
        "Preset \"%@\" tidak bisa dibaca": "Preset \"%@\" cannot be read",
        "Merekam %@ ke %@": "Recording %@ to %@",
        "Edisi demo: maksimal %d route aktif": "Demo edition: up to %d active routes",
        "Edisi demo: maksimal %d route aktif. Tangkap aplikasi, efek, rekaman, dan preset tidak tersedia.": "Demo edition: up to %d active routes. App capture, effects, recording, and presets are not available.",
        "Fitur ini tidak tersedia di edisi demo": "This feature is not available in the demo edition",
        "Terkunci di edisi demo": "Locked in the demo edition",
        "Edisi Demo": "Demo Edition",
        "Fitur yang terkunci: tangkap aplikasi, efek dan dinamika, rekaman, preset, dan template. Route aktif dibatasi %d.": "Locked features: app capture, effects and dynamics, recording, presets, and templates. Active routes are limited to %d.",
        "Untuk berlangganan, beli lewat tautan ini:": "To subscribe, buy through this link:",
        "Beli langganan": "Buy subscription",
        "Info lebih lanjut, hubungi developer di:": "More information, contact the developer at:",
        "Info edisi demo dan langganan": "Demo edition info and subscription",
        "Lanjutkan demo": "Continue demo",
    ]
}
