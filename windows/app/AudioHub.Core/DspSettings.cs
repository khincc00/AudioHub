namespace AudioHub.Core;

// Parameter DSP per input dan per output. Nilai default sama dengan apps/macos/AudioHub/Sources/Model.swift.

public sealed record GateSettings(bool Enabled = false, float ThresholdDb = -50, float AttackMs = 2,
                                  float ReleaseMs = 100, float HoldMs = 50);

public sealed record EqSettings(bool Enabled = false, float LowDb = 0, float LowHz = 120, float MidDb = 0,
                                float MidHz = 1000, float MidQ = 1, float HighDb = 0, float HighHz = 8000);

public sealed record CompSettings(bool Enabled = false, float ThresholdDb = -20, float Ratio = 4,
                                  float AttackMs = 10, float ReleaseMs = 150, float MakeupDb = 0, float KneeDb = 0,
                                  int SidechainInput = -1);

public sealed record DelaySettings(bool Enabled = false, float DelayMs = 0);

public sealed record LimiterSettings(bool Enabled = false, float CeilingDb = -1, float ReleaseMs = 50,
                                     float LookaheadMs = 0);

// Pemetaan ke struct native. Sidechain tidak ada di skema scene v1, jadi tidak ikut disimpan.
internal static class DspMapping
{
    internal static NativeMethods.GateParams ToNative(this GateSettings g) => new()
    {
        Enabled = g.Enabled ? 1 : 0, ThresholdDb = g.ThresholdDb, AttackMs = g.AttackMs,
        ReleaseMs = g.ReleaseMs, HoldMs = g.HoldMs,
    };

    internal static NativeMethods.EqParams ToNative(this EqSettings e) => new()
    {
        Enabled = e.Enabled ? 1 : 0, LowDb = e.LowDb, LowHz = e.LowHz, MidDb = e.MidDb, MidHz = e.MidHz,
        MidQ = e.MidQ, HighDb = e.HighDb, HighHz = e.HighHz,
    };

    internal static NativeMethods.CompParams ToNative(this CompSettings c) => new()
    {
        Enabled = c.Enabled ? 1 : 0, ThresholdDb = c.ThresholdDb, Ratio = c.Ratio, AttackMs = c.AttackMs,
        ReleaseMs = c.ReleaseMs, MakeupDb = c.MakeupDb, KneeDb = c.KneeDb, SidechainInput = c.SidechainInput,
    };

    internal static NativeMethods.DelayParams ToNative(this DelaySettings d) => new()
    {
        Enabled = d.Enabled ? 1 : 0, DelayMs = d.DelayMs,
    };

    internal static NativeMethods.LimiterParams ToNative(this LimiterSettings l) => new()
    {
        Enabled = l.Enabled ? 1 : 0, CeilingDb = l.CeilingDb, ReleaseMs = l.ReleaseMs, LookaheadMs = l.LookaheadMs,
    };
}
