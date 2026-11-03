using System.Text.Json;

namespace AudioHub.Windows;

// Pengaturan antarmuka yang diingat antar-sesi, disimpan di %LOCALAPPDATA%\AudioHub\settings.json.
// Gagal membaca atau menulis tidak boleh menghentikan aplikasi: nilai default dipakai.
public sealed class AppSettings
{
    public string Language { get; set; } = Localization.English;
    public string? PhysicalKey { get; set; }
    public bool Virtual12 { get; set; } = true;
    public bool Virtual34 { get; set; }
    public bool Loopback { get; set; }

    public static string FolderPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AudioHub");

    public static string FilePath => Path.Combine(FolderPath, "settings.json");

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath)) ?? new AppSettings();
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            // Berkas rusak: pakai default. Berkas akan ditulis ulang saat pengaturan berikutnya disimpan.
        }
        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(FolderPath);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Tidak fatal: pengaturan hanya tidak tersimpan untuk sesi ini.
        }
    }
}
