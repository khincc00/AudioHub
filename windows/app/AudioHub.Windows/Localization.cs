namespace AudioHub.Windows;

// Teks antarmuka dalam Bahasa Inggris (default) dan Bahasa Indonesia. Bahasa diganti langsung tanpa restart.
// Kunci yang tidak ada di tabel dikembalikan apa adanya, supaya kekurangan terlihat saat uji.
public static class Localization
{
    public const string English = "en";
    public const string Indonesian = "id";

    public static string ActiveLanguage { get; set; } = English;

    private static readonly Dictionary<string, (string En, string Id)> Table = new()
    {
        ["title"] = ("AudioHub", "AudioHub"),
        ["language"] = ("Language", "Bahasa"),
        ["physicalDevice"] = ("Physical device", "Perangkat fisik"),
        ["virtual12"] = ("VC 1/2", "VC 1/2"),
        ["virtual34"] = ("VC 3/4", "VC 3/4"),
        ["loopback"] = ("Loopback", "Loopback"),
        ["open"] = ("Open", "Buka"),
        ["close"] = ("Close", "Tutup"),
        ["start"] = ("Start", "Mulai"),
        ["stop"] = ("Stop", "Berhenti"),
        ["record"] = ("Record", "Rekam"),
        ["stopRecord"] = ("Stop recording", "Hentikan rekaman"),
        ["recordOutput"] = ("Record output", "Rekam output"),
        ["refresh"] = ("Refresh", "Muat ulang"),
        ["saveScene"] = ("Save scene", "Simpan scene"),
        ["loadScene"] = ("Load scene", "Muat scene"),
        ["apps"] = ("Apps (capture audio)", "Aplikasi (tangkap audio)"),
        ["isolate"] = ("Isolate (mute app output)", "Isolasi (bisukan output aplikasi)"),
        ["appsHint"] = ("Takes effect after Open.", "Berlaku setelah Open."),
        ["matrix"] = ("Routing matrix", "Matriks routing"),
        ["inputs"] = ("Inputs", "Input"),
        ["outputs"] = ("Outputs", "Output"),
        ["mute"] = ("Mute", "Bisu"),
        ["dynamics"] = ("FX", "FX"),
        ["statusStopped"] = ("Stopped", "Berhenti"),
        ["statusRunning"] = ("Running", "Berjalan"),
        ["statusOpened"] = ("Device open", "Perangkat dibuka"),
        ["statusClosed"] = ("Device closed", "Perangkat ditutup"),
        ["statusRecording"] = ("Recording to", "Merekam ke"),
        ["statusRecorded"] = ("Saved", "Tersimpan"),
        ["statusDropouts"] = ("Dropouts", "Dropout"),
        ["statusLatency"] = ("Latency", "Latensi"),
        ["statusAlive"] = ("Device lost, reopen", "Perangkat hilang, buka ulang"),
        ["noDevice"] = ("No device open", "Belum ada perangkat dibuka"),
        ["noAppsFound"] = ("No app with audio session", "Tidak ada aplikasi dengan sesi audio"),
        ["physIn"] = ("Physical In", "Fisik In"),
        ["physOut"] = ("Physical Out", "Fisik Out"),
        ["vcIn"] = ("VC In", "VC In"),
        ["vcOut"] = ("VC Out", "VC Out"),
        ["loopIn"] = ("Loopback In", "Loopback In"),
        ["loopOut"] = ("Loopback Out", "Loopback Out"),
        ["appIn"] = ("App", "Aplikasi"),
        ["gate"] = ("Noise gate", "Noise gate"),
        ["comp"] = ("Compressor", "Kompresor"),
        ["eq"] = ("EQ", "EQ"),
        ["delay"] = ("Delay", "Delay"),
        ["limiter"] = ("Limiter", "Limiter"),
        ["enabled"] = ("Enabled", "Aktif"),
        ["threshold"] = ("Threshold (dB)", "Ambang (dB)"),
        ["hold"] = ("Hold (ms)", "Tahan (ms)"),
        ["pickDevice"] = ("Choose a physical device first.", "Pilih perangkat fisik dulu."),
        ["recordFailed"] = ("Could not start recording. Check the output folder.", "Gagal memulai rekaman. Periksa folder tujuan."),
        ["loopHint"] = ("Virtual to virtual on the same device sends no audio.", "Virtual ke virtual pada perangkat yang sama tidak mengirim audio."),
        ["ratio"] = ("Ratio", "Rasio"),
        ["attack"] = ("Attack (ms)", "Attack (ms)"),
        ["release"] = ("Release (ms)", "Release (ms)"),
        ["makeup"] = ("Makeup (dB)", "Makeup (dB)"),
        ["low"] = ("Low (dB)", "Rendah (dB)"),
        ["mid"] = ("Mid (dB)", "Tengah (dB)"),
        ["high"] = ("High (dB)", "Tinggi (dB)"),
        ["delayMs"] = ("Delay (ms)", "Delay (ms)"),
        ["ceiling"] = ("Ceiling (dB)", "Ceiling (dB)"),
        ["ok"] = ("OK", "OK"),
        ["cancel"] = ("Cancel", "Batal"),
        ["fxTitle"] = ("Input processing: {0}", "Pemrosesan input: {0}"),
        ["limiterTitle"] = ("Output limiter: {0}", "Limiter output: {0}"),
        ["errorTitle"] = ("Error", "Galat"),
    };

    public static string T(string key)
    {
        if (!Table.TryGetValue(key, out var entry)) return key;
        return ActiveLanguage == Indonesian ? entry.Id : entry.En;
    }

    public static string Format(string key, params object[] args) => string.Format(T(key), args);
}
