// Probe: loopback kabel fisik (output 1 → input 1, output 2 → input 2) pada satu perangkat.
// Output memutar burst pendek bergantian, input direkam pada IOProc yang sama,
// lalu latensi dihitung per sampel dari onset burst.
// Pemakaian: ./loopback "Maono"
// Peringatan: probe ini MEMUTAR AUDIO ke output. Pastikan gain input minimum dan volume headphone rendah.
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
print("Loopback pada: \(name(dev))")

// Argumen kedua opsional: ukuran buffer dalam frame (mis. 128). Buffer asal dikembalikan di akhir.
var bufAddr = addr(kAudioDevicePropertyBufferFrameSize)
var bufSize = UInt32(MemoryLayout<UInt32>.size)
var originalBuffer: UInt32 = 0
AudioObjectGetPropertyData(dev, &bufAddr, 0, nil, &bufSize, &originalBuffer)
let requestedBuffer: UInt32? = CommandLine.arguments.count > 2 ? UInt32(CommandLine.arguments[2]) : nil
if var want = requestedBuffer {
    let r = AudioObjectSetPropertyData(dev, &bufAddr, 0, nil, bufSize, &want)
    print("Set buffer ke \(want) frame: status \(r)")
    Thread.sleep(forTimeInterval: 0.5)
}
var actualBuffer: UInt32 = 0
AudioObjectGetPropertyData(dev, &bufAddr, 0, nil, &bufSize, &actualBuffer)
print("Buffer aktif: \(actualBuffer) frame (asal: \(originalBuffer))")

let rate = 48000
let seconds = 3.0
let totalFrames = Int(seconds * Double(rate))
// Burst 10 ms, 1 kHz, amplitudo 0.1 (−20 dBFS puncak). Output 1 di 1 detik, output 2 di 2 detik.
let burstLen = rate / 100
let burstAmp: Float = 0.1
let burstStart: [Int: Int] = [1: rate * 1, 2: rate * 2]  // channel → frame awal burst

final class Loop {
    var frame = 0
    var recorded: [[Float]] = [[], []]
    let lock = NSLock()

    func fill(input: UnsafePointer<AudioBufferList>, output: UnsafeMutablePointer<AudioBufferList>) {
        lock.lock(); defer { lock.unlock() }
        // Output
        let outBufs = UnsafeMutableAudioBufferListPointer(output)
        for b in outBufs { if let p = b.mData { memset(p, 0, Int(b.mDataByteSize)) } }
        guard let firstOut = outBufs.first, firstOut.mNumberChannels > 0 else { return }
        let nFrames = Int(firstOut.mDataByteSize) / (4 * Int(firstOut.mNumberChannels))
        for i in 0..<nFrames {
            let absFrame = frame + i
            for (ch, start) in burstStart where absFrame >= start && absFrame < start + burstLen {
                let t = Double(absFrame - start) / Double(rate)
                let v = Float(burstAmp) * Float(sin(2 * Double.pi * 1000 * t))
                var idx = 0
                for b in outBufs {
                    let nch = Int(b.mNumberChannels)
                    guard let p = b.mData?.assumingMemoryBound(to: Float32.self) else { idx += nch; continue }
                    for c in 0..<nch where idx + c + 1 == ch { p[i * nch + c] = v }
                    idx += nch
                }
            }
        }
        // Input
        let inBufs = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: input))
        var idx = 0
        for b in inBufs {
            let nch = Int(b.mNumberChannels)
            guard let p = b.mData?.assumingMemoryBound(to: Float32.self) else { idx += nch; continue }
            let count = Int(b.mDataByteSize) / (4 * nch)
            for c in 0..<nch where idx + c < 2 {
                for f in 0..<count where recorded[idx + c].count < totalFrames + nFrames {
                    recorded[idx + c].append(p[f * nch + c])
                }
            }
            idx += nch
        }
        frame += nFrames
    }
}

let loop = Loop()
var procID: AudioDeviceIOProcID?
let status = AudioDeviceCreateIOProcIDWithBlock(&procID, dev, nil) { _, inInput, _, outOutput, _ in
    loop.fill(input: inInput, output: outOutput)
}
guard status == noErr, let proc = procID else { print("Gagal membuat IOProc: \(status)"); exit(1) }
AudioDeviceStart(dev, proc)
Thread.sleep(forTimeInterval: seconds + 0.5)
AudioDeviceStop(dev, proc)
AudioDeviceDestroyIOProcID(dev, proc)

if requestedBuffer != nil {
    var restore = originalBuffer
    AudioObjectSetPropertyData(dev, &bufAddr, 0, nil, bufSize, &restore)
    print("Buffer dikembalikan ke \(originalBuffer) frame")
}

loop.lock.lock()
for c in 0..<2 {
    let r = loop.recorded[c]
    let nans = r.filter { $0.isNaN }.count
    let maxAbs = r.filter { !$0.isNaN }.map { abs($0) }.max() ?? 0
    print("debug input \(c + 1): sampel=\(r.count) nan=\(nans) puncak=\(maxAbs)")
}
for (outCh, start) in burstStart.sorted(by: { $0.key < $1.key }) {
    let inIdx = outCh - 1
    let rec = loop.recorded[inIdx]
    // Cari onset di input yang sama kanalnya, di jendela 0–200 ms setelah burst
    let window = start..<min(start + rate / 5, rec.count)
    let peak = window.map { abs(rec[$0]) }.max() ?? 0
    // Input dipakai dari kanal yang sama dengan output
    if let onset = window.first(where: { abs(rec[$0]) >= peak * 0.5 && peak > 0.001 }) {
        let latency = onset - start
        print(String(format: "output %d → input %d: onset di sampel %d, latensi %d sampel (%.2f ms), puncak %.1f dBFS",
                     outCh, inIdx + 1, onset, latency, Double(latency) * 1000 / Double(rate), 20 * log10(Double(peak))))
    } else {
        print("output \(outCh) → input \(inIdx + 1): tidak ada sinyal terdeteksi")
    }
    // Crosstalk: input kanal lain di jendela yang sama
    let other = loop.recorded[1 - inIdx]
    let otherPeak = (start..<min(start + rate / 5, other.count)).map { abs(other[$0]) }.max() ?? 0
    print(String(format: "  kebocoran ke input %d: %.1f dBFS", 2 - inIdx, otherPeak > 0 ? 20 * log10(Double(otherPeak)) : -999))
}
loop.lock.unlock()
