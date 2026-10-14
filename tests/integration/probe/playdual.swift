// Probe: memutar nada rendah dan tinggi bergantian per kanal output.
// Output 1 dan 2 bergantian: 440 Hz (rendah) lalu 880 Hz (tinggi), 1,5 detik per nada, −30 dBFS.
// Perhatikan nada (rendah/tinggi) dan telinga mana yang memainkannya.
// Pemakaian: ./playdual "Maono"
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

struct Seg { let channel: Int; let freq: Double; let start: Double; let end: Double }
let rate = 48000.0
let amp = 0.0316 // −30 dBFS
let fade = 0.02
// Bergantian: rendah lalu tinggi di output 1, lalu rendah lalu tinggi di output 2.
let segments = [
    Seg(channel: 1, freq: 440, start: 0.0, end: 1.5),
    Seg(channel: 1, freq: 880, start: 1.5, end: 3.0),
    Seg(channel: 2, freq: 440, start: 3.0, end: 4.5),
    Seg(channel: 2, freq: 880, start: 4.5, end: 6.0),
]

final class Tone {
    var frame = 0
    let segments: [Seg]
    init(_ segments: [Seg]) { self.segments = segments }

    func fill(_ out: UnsafeMutablePointer<AudioBufferList>) {
        let bufs = UnsafeMutableAudioBufferListPointer(out)
        for b in bufs { if let p = b.mData { memset(p, 0, Int(b.mDataByteSize)) } }
        guard let first = bufs.first, first.mNumberChannels > 0 else { return }
        let nFrames = Int(first.mDataByteSize) / (4 * Int(first.mNumberChannels))
        for i in 0..<nFrames {
            let t = Double(frame + i) / rate
            for seg in segments where t >= seg.start && t < seg.end {
                let local = t - seg.start
                let len = seg.end - seg.start
                let env = min(1.0, local / fade, (len - local) / fade)
                let v = Float(amp * env * sin(2 * Double.pi * seg.freq * local))
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

let tone = Tone(segments)
var procID: AudioDeviceIOProcID?
let status = AudioDeviceCreateIOProcIDWithBlock(&procID, dev, nil) { _, _, _, out, _ in
    tone.fill(out)
}
guard status == noErr, let proc = procID else { print("Gagal membuat IOProc: \(status)"); exit(1) }
AudioDeviceStart(dev, proc)
Thread.sleep(forTimeInterval: 6.5)
AudioDeviceStop(dev, proc)
AudioDeviceDestroyIOProcID(dev, proc)
print("Selesai.")
