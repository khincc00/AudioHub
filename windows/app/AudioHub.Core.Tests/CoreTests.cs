using AudioHub.Core;
using Xunit;

namespace AudioHub.Core.Tests;

public class DeviceParserTests
{
    [Fact]
    public void ParsesEndpointLines()
    {
        const string text = "in\tMicrophone (PS22 2x2)\t48000\t2\nout\tSpeakers (PS22 2x2)\t48000\t2\n";

        var list = DeviceParser.ParseEndpoints(text);

        Assert.Equal(2, list.Count);
        Assert.Equal(new EndpointInfo(EndpointFlow.Input, "Microphone (PS22 2x2)", 48000, 2), list[0]);
        Assert.Equal(EndpointFlow.Output, list[1].Flow);
    }

    [Fact]
    public void SkipsMalformedEndpointLines()
    {
        const string text = "in\tTanpa kolom\n\nxx\tNama\t48000\t2\nout\tNama\tbukan-angka\t2\nout\tBaik\t44100\t8\n";

        var list = DeviceParser.ParseEndpoints(text);

        Assert.Single(list);
        Assert.Equal("Baik", list[0].Name);
        Assert.Equal(44100, list[0].SampleRate);
        Assert.Equal(8, list[0].Channels);
    }

    [Fact]
    public void KeepsTabsOutOfNames()
    {
        // Nama endpoint dengan titik dua dan tanda kurung tetap utuh.
        var list = DeviceParser.ParseEndpoints("out\tSpeakers (AVHub VC 1/2: Stereo)\t48000\t2\n");

        Assert.Equal("Speakers (AVHub VC 1/2: Stereo)", list[0].Name);
    }

    [Fact]
    public void ParsesProcessLines()
    {
        var list = DeviceParser.ParseProcesses("4120\tSpotify\n980\tDiscord\nabc\tRusak\n");

        Assert.Equal(2, list.Count);
        Assert.Equal(new ProcessInfo(4120, "Spotify"), list[0]);
        Assert.Equal(980u, list[1].ProcessId);
    }
}

public class DeviceMatchingTests
{
    [Theory]
    [InlineData("Microphone (PS22 2x2)", "PS22 2x2")]
    [InlineData("Speakers (PS22 2x2)", "PS22 2x2")]
    [InlineData("Speakers (Realtek(R) Audio)", "Realtek(R) Audio")]
    [InlineData("  Headphones (USB Audio)  ", "USB Audio")]
    [InlineData("Tanpa kurung", "Tanpa kurung")]
    [InlineData("Kosong ()", "Kosong ()")]
    public void KeyMatchesBothEndpointsOfOneDevice(string endpoint, string expected)
    {
        Assert.Equal(expected, DeviceMatching.KeyOf(endpoint));
    }
}

public class SceneFileTests
{
    private static SceneFile SampleScene() => new()
    {
        Name = "Podcast",
        DeviceName = "PS22 2x2",
        UseVirtual = true,
        UseVirtual2 = false,
        UseVirtual3 = null,
        Bindings =
        {
            new SceneBinding { Role = "Fisik In", Kind = "physical", DeviceUID = "Microphone (PS22 2x2)", DeviceName = "PS22 2x2", Channel = 1 },
        },
        Inputs =
        {
            new SceneInput { Id = "in:0", GainDb = -3.5f, Mute = false, Fx = new SceneChannelFx { Name = "Mikrofon", Gate = new SceneGate { Enabled = true } } },
        },
        Outputs =
        {
            new SceneOutput { Id = "out:0", Name = "Monitor", GainDb = 0, Limiter = new SceneLimiter { Enabled = true, CeilingDb = -1 } },
        },
        Routes =
        {
            new SceneRoute { From = "in:0", To = "out:0", SendDb = -6, Enabled = true },
        },
        LinkedPairs = { true, false },
    };

    [Fact]
    public void RoundTripKeepsValues()
    {
        var json = SceneStore.Serialize(SampleScene());

        var back = SceneStore.Deserialize(json);

        Assert.Equal("Podcast", back.Name);
        Assert.Equal(-3.5f, back.Inputs[0].GainDb);
        Assert.True(back.Inputs[0].Fx.Gate.Enabled);
        Assert.Equal(-6, back.Routes[0].SendDb);
        Assert.Equal(new[] { true, false }, back.LinkedPairs);
    }

    [Fact]
    public void WritesCamelCaseAndOmitsNullableVirtual3()
    {
        var json = SceneStore.Serialize(SampleScene());

        Assert.Contains("\"schemaVersion\": 1", json);
        Assert.Contains("\"deviceName\": \"PS22 2x2\"", json);
        Assert.DoesNotContain("useVirtual3", json);
    }

    [Fact]
    public void RejectsWrongSchemaVersion()
    {
        var scene = SampleScene();
        scene.SchemaVersion = 2;

        Assert.Contains("schemaVersion harus 1", SceneStore.Validate(scene));
    }

    [Fact]
    public void RejectsBadIdsAndRanges()
    {
        var scene = SampleScene();
        scene.Inputs[0].Id = "mic:0";
        scene.Routes[0].SendDb = 6;

        var errors = SceneStore.Validate(scene);

        Assert.Contains(errors, e => e.Contains("mic:0"));
        Assert.Contains(errors, e => e.Contains("sendDb"));
    }

    [Fact]
    public void DeserializeThrowsOnInvalidJson()
    {
        Assert.Throws<SceneFormatException>(() => SceneStore.Deserialize("{ bukan json"));
    }

    [Fact]
    public void DeserializeThrowsOnInvalidScene()
    {
        const string json = "{\"schemaVersion\":1,\"name\":\"x\",\"inputs\":[{\"id\":\"salah\"}]}";

        Assert.Throws<SceneFormatException>(() => SceneStore.Deserialize(json));
    }
}

public class DspDefaultsTests
{
    [Fact]
    public void DefaultsMatchMacOSModel()
    {
        // Nilai default sama dengan Model.swift: gate -50 dB, kompresor 4:1, limiter -1 dB.
        var gate = new GateSettings();
        var comp = new CompSettings();
        var limiter = new LimiterSettings();

        Assert.Equal(-50, gate.ThresholdDb);
        Assert.Equal(4, comp.Ratio);
        Assert.Equal(-1, limiter.CeilingDb);
        Assert.Equal(-1, comp.SidechainInput);
    }

    [Fact]
    public void NativeEnabledFlagIsOneOrZero()
    {
        var native = new GateSettings(Enabled: true).ToNative();

        Assert.Equal(1, native.Enabled);
        Assert.Equal(0, new GateSettings().ToNative().Enabled);
    }
}
