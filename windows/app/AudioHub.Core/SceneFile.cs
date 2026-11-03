using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace AudioHub.Core;

// Format scene/preset v1, sesuai contracts/scene-schema/scene.v1.schema.json. Berkas yang dibuat di macOS
// bisa dibaca di sini, dan sebaliknya, selama field-field di bawah ini dipakai.
// Perangkat disimpan lewat nama endpoint (deviceUID diisi nama endpoint, karena Windows tidak punya UID Core Audio).

public sealed class SceneFile
{
    public int SchemaVersion { get; set; } = 1;
    public string Name { get; set; } = "";
    public string DeviceName { get; set; } = "";
    public bool UseVirtual { get; set; }
    public bool UseVirtual2 { get; set; }
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? UseVirtual3 { get; set; }
    public List<SceneBinding> Bindings { get; set; } = new();
    public List<SceneInput> Inputs { get; set; } = new();
    public List<SceneOutput> Outputs { get; set; } = new();
    public List<SceneRoute> Routes { get; set; } = new();
    public List<bool> LinkedPairs { get; set; } = new();
    public List<string> Buses { get; set; } = new();
}

public sealed class SceneBinding
{
    public string Role { get; set; } = "";
    public string Kind { get; set; } = "physical";     // "physical" atau "virtual"
    public string DeviceUID { get; set; } = "";
    public string DeviceName { get; set; } = "";
    public int Channel { get; set; } = 1;              // 1-based, dalam perangkatnya
}

public sealed class SceneInput
{
    public string Id { get; set; } = "";               // "in:0", "in:1", ...
    public float GainDb { get; set; }
    public bool Mute { get; set; }
    public SceneChannelFx Fx { get; set; } = new();
}

public sealed class SceneChannelFx
{
    public string Name { get; set; } = "";
    public int ColorIndex { get; set; }
    public SceneGate Gate { get; set; } = new();
    public SceneEq Eq { get; set; } = new();
    public SceneComp Comp { get; set; } = new();
    public SceneDelay Delay { get; set; } = new();
    public string Insert { get; set; } = "";
}

public sealed class SceneGate
{
    public bool Enabled { get; set; }
    public float ThresholdDb { get; set; } = -50;
    public float AttackMs { get; set; } = 2;
    public float ReleaseMs { get; set; } = 100;
    public float HoldMs { get; set; } = 50;
}

public sealed class SceneEq
{
    public bool Enabled { get; set; }
    public float LowDb { get; set; }
    public float LowHz { get; set; } = 120;
    public float MidDb { get; set; }
    public float MidHz { get; set; } = 1000;
    public float MidQ { get; set; } = 1;
    public float HighDb { get; set; }
    public float HighHz { get; set; } = 8000;
}

public sealed class SceneComp
{
    public bool Enabled { get; set; }
    public float ThresholdDb { get; set; } = -20;
    public float Ratio { get; set; } = 4;
    public float AttackMs { get; set; } = 10;
    public float ReleaseMs { get; set; } = 150;
    public float MakeupDb { get; set; }
    public float KneeDb { get; set; }
}

public sealed class SceneDelay
{
    public bool Enabled { get; set; }
    public float DelayMs { get; set; }
}

public sealed class SceneOutput
{
    public string Id { get; set; } = "";               // "out:0", ...
    public string Name { get; set; } = "";
    public float GainDb { get; set; }
    public bool Mute { get; set; }
    public SceneLimiter Limiter { get; set; } = new();
}

public sealed class SceneLimiter
{
    public bool Enabled { get; set; }
    public float CeilingDb { get; set; } = -1;
    public float ReleaseMs { get; set; } = 50;
}

public sealed class SceneRoute
{
    public string From { get; set; } = "";             // "in:N"
    public string To { get; set; } = "";               // "out:N"
    public float SendDb { get; set; }
    public bool Enabled { get; set; }
}

public sealed class SceneFormatException : Exception
{
    public SceneFormatException(string message) : base(message) { }
}

public static partial class SceneStore
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
    };

    [GeneratedRegex("^in:[0-9]+$")] private static partial Regex InputId();
    [GeneratedRegex("^out:[0-9]+$")] private static partial Regex OutputId();

    public static string Serialize(SceneFile scene) => JsonSerializer.Serialize(scene, Options);

    // Membaca dan memvalidasi. Melempar SceneFormatException jika berkas tidak sesuai skema v1.
    public static SceneFile Deserialize(string json)
    {
        SceneFile? scene;
        try
        {
            scene = JsonSerializer.Deserialize<SceneFile>(json, Options);
        }
        catch (JsonException ex)
        {
            throw new SceneFormatException("Berkas scene tidak bisa dibaca: " + ex.Message);
        }
        if (scene is null) throw new SceneFormatException("Berkas scene kosong.");

        var errors = Validate(scene);
        if (errors.Count > 0) throw new SceneFormatException(string.Join("; ", errors));
        return scene;
    }

    public static IReadOnlyList<string> Validate(SceneFile scene)
    {
        var errors = new List<string>();
        if (scene.SchemaVersion != 1) errors.Add("schemaVersion harus 1");
        if (string.IsNullOrWhiteSpace(scene.Name) || scene.Name.Length > 80) errors.Add("name wajib, maksimal 80 karakter");
        foreach (var input in scene.Inputs)
        {
            if (!InputId().IsMatch(input.Id)) errors.Add($"id input tidak valid: {input.Id}");
            if (input.GainDb < -60 || input.GainDb > 12) errors.Add($"gainDb input {input.Id} di luar rentang");
        }
        foreach (var output in scene.Outputs)
        {
            if (!OutputId().IsMatch(output.Id)) errors.Add($"id output tidak valid: {output.Id}");
            if (output.GainDb < -60 || output.GainDb > 12) errors.Add($"gainDb output {output.Id} di luar rentang");
            if (output.Limiter.CeilingDb < -12 || output.Limiter.CeilingDb > 0) errors.Add($"ceilingDb {output.Id} di luar rentang");
        }
        foreach (var route in scene.Routes)
        {
            if (!InputId().IsMatch(route.From)) errors.Add($"from tidak valid: {route.From}");
            if (!OutputId().IsMatch(route.To)) errors.Add($"to tidak valid: {route.To}");
            if (route.SendDb < -60 || route.SendDb > 0) errors.Add($"sendDb {route.From}->{route.To} di luar rentang");
        }
        foreach (var binding in scene.Bindings)
        {
            if (binding.Kind is not ("physical" or "virtual")) errors.Add($"kind tidak valid: {binding.Kind}");
            if (binding.Channel < 1) errors.Add($"channel harus minimal 1 ({binding.Role})");
        }
        return errors;
    }
}
