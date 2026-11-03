namespace AudioHub.Core;

public enum EndpointFlow { Input, Output }

// Endpoint audio Windows yang aktif. Satu perangkat fisik muncul sebagai dua endpoint (input dan output).
public sealed record EndpointInfo(EndpointFlow Flow, string Name, int SampleRate, int Channels);

// Proses yang punya sesi audio dan bisa ditangkap sebagai sumber input.
public sealed record ProcessInfo(uint ProcessId, string Name);

public static class DeviceParser
{
    // Satu baris per endpoint: "in<TAB>nama<TAB>sampleRate<TAB>kanal" (lihat audiohub_io.h).
    public static IReadOnlyList<EndpointInfo> ParseEndpoints(string text)
    {
        var list = new List<EndpointInfo>();
        foreach (var line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var parts = line.Split('\t');
            if (parts.Length != 4) continue;
            var flow = parts[0] switch
            {
                "in" => EndpointFlow.Input,
                "out" => EndpointFlow.Output,
                _ => (EndpointFlow?)null,
            };
            if (flow is null) continue;
            if (!int.TryParse(parts[2], System.Globalization.NumberStyles.Integer,
                              System.Globalization.CultureInfo.InvariantCulture, out var rate)) continue;
            if (!int.TryParse(parts[3], System.Globalization.NumberStyles.Integer,
                              System.Globalization.CultureInfo.InvariantCulture, out var channels)) continue;
            list.Add(new EndpointInfo(flow.Value, parts[1], rate, channels));
        }
        return list;
    }

    // Satu baris per proses: "pid<TAB>nama".
    public static IReadOnlyList<ProcessInfo> ParseProcesses(string text)
    {
        var list = new List<ProcessInfo>();
        foreach (var line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var parts = line.Split('\t');
            if (parts.Length != 2) continue;
            if (!uint.TryParse(parts[0], System.Globalization.NumberStyles.Integer,
                               System.Globalization.CultureInfo.InvariantCulture, out var pid)) continue;
            list.Add(new ProcessInfo(pid, parts[1]));
        }
        return list;
    }
}

public static class DeviceMatching
{
    // Kunci pencocokan perangkat fisik. Nama endpoint input dan output biasanya berbeda awalannya
    // ("Microphone (PS22 2x2)" dan "Speakers (PS22 2x2)"), tetapi teks dalam kurung sama.
    // Jika tidak ada kurung, nama utuh dipakai.
    public static string KeyOf(string endpointName)
    {
        var name = endpointName.Trim();
        // Kurung pertama sampai kurung penutup terakhir, supaya "Realtek(R)" di dalam nama tetap utuh.
        int open = name.IndexOf('(');
        int close = name.LastIndexOf(')');
        if (open >= 0 && close > open)
        {
            var key = name.Substring(open + 1, close - open - 1).Trim();
            if (key.Length > 0) return key;
        }
        return name;
    }
}
