// Probe: rekam input langsung dari Core Audio (tanpa ffmpeg/AVFoundation)
// dan laporkan level per kanal fisik. Read-only: tidak memutar audio.
// Pemakaian: ./capture "Maono ProStudio 2x2" 12
import CoreAudio
import Foundation

func addr(_ sel: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: sel, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
}

func deviceIDs() -> [AudioObjectID] {
    var a = addr(kAudioHardwarePropertyDevices)
    var size: UInt32 = 0
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size)
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size, &ids)
    return ids
}

func name(_ id: AudioObjectID) -> String {
    var a = addr(kAudioObjectPropertyName)
    var s: CFString = "" as CFString
    var size = UInt32(MemoryLayout<CFString>.size)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &s)
    return s as String
}

let wanted = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "Maono"
let seconds = CommandLine.arguments.count > 2 ? Double(CommandLine.arguments[2]) ?? 12 : 12
guard let dev = deviceIDs().first(where: { name($0).contains(wanted) }) else {
    print("Perangkat dengan nama \"\(wanted)\" tidak ditemukan"); exit(1)
}
print("Merekam dari: \(name(dev)) selama \(seconds) detik")

final class Meter {
    let lock = NSLock()
    var channels = 0
    var windowFrames = 0
    var sums: [Double] = []
    var identicalSamples = 0
    var comparedSamples = 0
    var rows: [String] = []
    var totalFrames = 0
    var formatWarned = false
    let windowSize = 24000 // 0.5 s pada 48 kHz

    func process(_ list: UnsafePointer<AudioBufferList>) {
        lock.lock(); defer { lock.unlock() }
        let bufs = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: list))
        let total = bufs.reduce(0) { $0 + Int($1.mNumberChannels) }
        if channels == 0 {
            channels = total
            sums = [Double](repeating: 0, count: total)
            rows.append("waktu(s)  " + (1...total).map { "ch\($0) dBFS" }.joined(separator: "   "))
        }
        // Kumpulkan sampel per kanal fisik dalam satu frame
        var frames = 0
        var perChannel: [[Float32]] = Array(repeating: [], count: total)
        var idx = 0
        for b in bufs {
            let nch = Int(b.mNumberChannels)
            guard let raw = b.mData, nch > 0 else { continue }
            let samples = raw.assumingMemoryBound(to: Float32.self)
            let count = Int(b.mDataByteSize) / (4 * nch)
            if !formatWarned && Int(b.mDataByteSize) != count * nch * 4 {
                print("Peringatan: format bukan float32, hasil tidak valid"); formatWarned = true
            }
            frames = max(frames, count)
            for f in 0..<count { for c in 0..<nch { perChannel[idx + c].append(samples[f * nch + c]) } }
            idx += nch
        }
        for c in 0..<min(total, perChannel.count) {
            for v in perChannel[c] { sums[c] += Double(v) * Double(v) }
        }
        if total >= 2 {
            let n = min(perChannel[0].count, perChannel[1].count)
            for i in 0..<n {
                comparedSamples += 1
                if perChannel[0][i] == perChannel[1][i] { identicalSamples += 1 }
            }
        }
        totalFrames += frames
        windowFrames += frames
        if windowFrames >= windowSize {
            let t = Double(totalFrames - windowFrames) / 48000
            let cols = sums.map { s -> String in
                let rms = sqrt(s / Double(windowFrames))
                return rms > 0 ? String(format: "%8.1f", 20 * log10(rms)) : "    -inf"
            }
            rows.append(String(format: "%5.1f     ", t) + cols.joined(separator: "   "))
            sums = [Double](repeating: 0, count: total)
            windowFrames = 0
        }
    }
}

let meter = Meter()
var procID: AudioDeviceIOProcID?
let status = AudioDeviceCreateIOProcIDWithBlock(&procID, dev, nil) { _, inInput, _, _, _ in
    meter.process(inInput)
}
guard status == noErr, let proc = procID else { print("Gagal membuat IOProc: \(status)"); exit(1) }
AudioDeviceStart(dev, proc)
Thread.sleep(forTimeInterval: seconds)
AudioDeviceStop(dev, proc)
AudioDeviceDestroyIOProcID(dev, proc)

meter.lock.lock()
meter.rows.forEach { print($0) }
let pct = meter.comparedSamples > 0 ? 100.0 * Double(meter.identicalSamples) / Double(meter.comparedSamples) : 0
print(String(format: "Kanal 1 dan 2 identik sample per sample: %.2f%% (%d dari %d)", pct, meter.identicalSamples, meter.comparedSamples))
print("Total frame: \(meter.totalFrames), kanal fisik terdeteksi: \(meter.channels)")
meter.lock.unlock()
