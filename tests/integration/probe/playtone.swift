// Probe: memutar tone pelan (440 Hz, −30 dBFS) ke satu kanal output secara bergantian.
// Urutan: output 1 selama 2 detik, diam 1 detik, output 2 selama 2 detik.
// Pemakaian: ./playtone "Maono"
// Peringatan: probe ini MEMUTAR AUDIO. Pakai headphone dan volume rendah.
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
guard let dev = deviceIDs().first(where: { name($0).contains(wanted) }) else {
    print("Perangkat \"\(wanted)\" tidak ditemukan"); exit(1)
}
print("Output ke: \(name(dev))")

let rate = 48000.0
// Argumen ketiga opsional: frekuensi nada dalam Hz (default 440).
let freq = CommandLine.arguments.count > 3 ? Double(CommandLine.arguments[3]) ?? 440 : 440
let amp = 0.0316 // −30 dBFS
// Argumen kedua opsional: nomor output tunggal (mis. "2"), 3 detik tanpa urutan.
let onlyChannel = CommandLine.arguments.count > 2 ? Int(CommandLine.arguments[2]) : nil
let segments: [(channel: Int, start: Double, end: Double)] = onlyChannel.map { [($0, 0.0, 3.0)] } ?? [
    (1, 0.0, 2.0),
    (0, 2.0, 3.0),   // diam
    (2, 3.0, 5.0),
]
let totalSeconds = 5.0

final class Tone {
    var frame = 0
    let rate: Double
    let freq: Double
    let amp: Double
    let segments: [(channel: Int, start: Double, end: Double)]
    let fadeSeconds = 0.02

    init(rate: Double, freq: Double, amp: Double, segments: [(channel: Int, start: Double, end: Double)]) {
        self.rate = rate; self.freq = freq; self.amp = amp; self.segments = segments
    }

    func fill(_ out: UnsafeMutablePointer<AudioBufferList>) {
        let bufs = UnsafeMutableAudioBufferListPointer(out)
        for b in bufs { if let p = b.mData { memset(p, 0, Int(b.mDataByteSize)) } }
        guard let first = bufs.first, first.mNumberChannels > 0 else { return }
        let nFrames = Int(first.mDataByteSize) / (4 * Int(first.mNumberChannels))
        for i in 0..<nFrames {
            let t = Double(frame + i) / rate
            for seg in segments where seg.channel > 0 && t >= seg.start && t < seg.end {
                let local = t - seg.start
                let len = seg.end - seg.start
                let env = min(1.0, local / fadeSeconds, (len - local) / fadeSeconds)
                let v = Float(amp * env * sin(2 * Double.pi * freq * local))
                var idx = 0
                for b in bufs {
                    let nch = Int(b.mNumberChannels)
                    guard let p = b.mData?.assumingMemoryBound(to: Float32.self) else { idx += nch; continue }
                    for c in 0..<nch where idx + c + 1 == seg.channel {
                        p[i * nch + c] = v
                    }
                    idx += nch
                }
            }
        }
        frame += nFrames
    }
}

let tone = Tone(rate: rate, freq: freq, amp: amp, segments: segments)
var procID: AudioDeviceIOProcID?
let status = AudioDeviceCreateIOProcIDWithBlock(&procID, dev, nil) { _, _, _, out, _ in
    tone.fill(out)
}
guard status == noErr, let proc = procID else { print("Gagal membuat IOProc: \(status)"); exit(1) }
AudioDeviceStart(dev, proc)
Thread.sleep(forTimeInterval: totalSeconds + 0.5)
AudioDeviceStop(dev, proc)
AudioDeviceDestroyIOProcID(dev, proc)
print("Selesai.")
