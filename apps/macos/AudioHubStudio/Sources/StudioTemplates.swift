import Foundation

extension EngineController {
    // Skema live: mikrofon (input fisik pertama) dan input VB1 → output VB2 (pasangan stereo).
    // Track "Vokal" = mikrofon, "Musik" = input pertama dari Virtual 1. TikTok Live membaca VB2.
    // Mengembalikan pesan untuk status.
    func applyLiveTikTokTemplate() -> String {
        if !isOpen { open() }
        guard isOpen else { return "Perangkat belum terbuka" }
        guard physIn >= 1 else { return "Skema ini butuh input fisik untuk mikrofon" }

        let vb1In = (0..<inputs).filter { channelOwner($0, output: false).owner == 1 }
        let vb2Out = (0..<outputs).filter { channelOwner($0, output: true).owner == 2 }
        guard let music = vb1In.first else { return "Virtual 1 (VB1) belum dipilih" }
        guard let outL = vb2Out.first else { return "Virtual 2 (VB2) belum dipilih" }
        let outR = vb2Out.count > 1 ? vb2Out[1] : outL

        let mic = 0
        // Musik stereo: pasangan kedua dari VB1 jika ada, jika tidak mono ke kedua sisi.
        let musicR = vb1In.count > 1 ? vb1In[1] : music

        for i in 0..<inputs {
            for j in 0..<outputs { setRoute(i, j, false) }
        }
        setRoute(mic, outL, true)
        setRoute(mic, outR, true)
        setRoute(music, outL, true)
        setRoute(musicR, outR, true)

        setInputName(mic, "Vokal")
        setInputName(music, "Musik")
        return "Skema live: \(inLabel(mic)) dan \(inLabel(music)) → VB2"
    }
}
