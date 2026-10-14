import CoreAudio
import Foundation

func addr(_ sel: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
}

func devices() -> [AudioObjectID] {
    var a = addr(kAudioHardwarePropertyDevices)
    var size: UInt32 = 0
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size)
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size, &ids)
    return ids
}

func cfString(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector) -> String {
    var a = addr(sel)
    var s: CFString = "" as CFString
    var size = UInt32(MemoryLayout<CFString>.size)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &s)
    return s as String
}

func uint32(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> UInt32 {
    var a = addr(sel, scope)
    var v: UInt32 = 0
    var size = UInt32(MemoryLayout<UInt32>.size)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &v)
    return v
}

func float64(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector) -> Float64 {
    var a = addr(sel)
    var v: Float64 = 0
    var size = UInt32(MemoryLayout<Float64>.size)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &v)
    return v
}

func channels(_ id: AudioObjectID, _ scope: AudioObjectPropertyScope) -> Int {
    var a = addr(kAudioDevicePropertyStreamConfiguration, scope)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &a, 0, nil, &size) == noErr, size > 0 else { return 0 }
    let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { raw.deallocate() }
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, raw)
    let list = raw.assumingMemoryBound(to: AudioBufferList.self)
    let buffers = UnsafeMutableAudioBufferListPointer(list)
    return buffers.reduce(0) { $0 + Int($1.mNumberChannels) }
}

func bufferRange(_ id: AudioObjectID) -> (Float64, Float64) {
    var a = addr(kAudioDevicePropertyBufferFrameSizeRange)
    var r = AudioValueRange()
    var size = UInt32(MemoryLayout<AudioValueRange>.size)
    AudioObjectGetPropertyData(id, &a, 0, nil, &size, &r)
    return (r.mMinimum, r.mMaximum)
}

for id in devices() {
    let name = cfString(id, kAudioObjectPropertyName)
    let uid = cfString(id, kAudioDevicePropertyDeviceUID)
    guard name.contains("Maono") || name.contains("EMEET") || name.contains("MacBook Air Microphone") else { continue }
    let inCh = channels(id, kAudioObjectPropertyScopeInput)
    let outCh = channels(id, kAudioObjectPropertyScopeOutput)
    let rate = float64(id, kAudioDevicePropertyNominalSampleRate)
    let buf = uint32(id, kAudioDevicePropertyBufferFrameSize)
    let (bmin, bmax) = bufferRange(id)
    let latIn = uint32(id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput)
    let latOut = uint32(id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput)
    print("name: \(name)")
    print("  uid: \(uid)")
    print("  id: \(id)")
    print("  input_channels: \(inCh)  output_channels: \(outCh)")
    print("  nominal_rate: \(rate)")
    print("  buffer_frames_current: \(buf)  range: \(Int(bmin))-\(Int(bmax))")
    print("  latency_frames_in: \(latIn)  latency_frames_out: \(latOut)")
}
