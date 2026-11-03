using System.Runtime.InteropServices;
using System.Text;

namespace AudioHub.Core;

public sealed record OpenOptions(
    string PhysicalName,
    string? Virtual1Name = null,
    string? Virtual2Name = null,
    string? Virtual3Name = null,
    IReadOnlyList<uint>? TapProcessIds = null,
    bool IsolateTaps = false);

public sealed class AudioEngineException : Exception
{
    public AudioEngineException(string message) : base(message) { }
}

public readonly record struct Route(int Input, int Output, bool Enabled, float SendDb);

public readonly record struct ChannelOwnership(int Owner, int LocalChannel);

public readonly record struct EngineStats(
    ulong Callbacks,
    ulong Dropouts,
    ulong MissingFrames,
    int Inputs,
    int Outputs,
    uint BufferFrames,
    double SampleRate,
    uint InputLatencyFrames,
    uint OutputLatencyFrames);

// Pemilik kanal, sama dengan nilai ah_io_channel_owner.
public static class ChannelOwner
{
    public const int Physical = 0;
    public const int Virtual1 = 1;
    public const int Virtual2 = 2;
    public const int Virtual3 = 3;
    public const int Tap = 4;
}

// Pembungkus handle native. Satu instance = satu sesi IO (endpoint terbuka, engine berjalan atau berhenti).
public sealed class AudioEngine : IDisposable
{
    private IntPtr _handle;
    private bool _running;

    private AudioEngine(IntPtr handle, int inputs, int outputs, int physicalInputs, int physicalOutputs)
    {
        _handle = handle;
        Inputs = inputs;
        Outputs = outputs;
        PhysicalInputs = physicalInputs;
        PhysicalOutputs = physicalOutputs;
    }

    public int Inputs { get; }
    public int Outputs { get; }
    public int PhysicalInputs { get; }
    public int PhysicalOutputs { get; }

    public bool IsRunning => _running;

    public bool IsAlive => _handle != IntPtr.Zero && NativeMethods.ah_io_is_alive(_handle) != 0;

    public static AudioEngine Open(OpenOptions options)
    {
        if (string.IsNullOrWhiteSpace(options.PhysicalName))
            throw new ArgumentException("Nama perangkat fisik kosong.", nameof(options));

        var allocated = new List<IntPtr>();
        try
        {
            IntPtr Str(string? s)
            {
                if (string.IsNullOrEmpty(s)) return IntPtr.Zero;
                var p = Marshal.StringToCoTaskMemUTF8(s);
                allocated.Add(p);
                return p;
            }

            var pids = options.TapProcessIds ?? Array.Empty<uint>();
            IntPtr pidArray = IntPtr.Zero;
            if (pids.Count > 0)
            {
                pidArray = Marshal.AllocCoTaskMem(sizeof(uint) * pids.Count);
                allocated.Add(pidArray);
                for (int i = 0; i < pids.Count; i++)
                    Marshal.WriteInt32(pidArray, i * sizeof(uint), unchecked((int)pids[i]));
            }

            var cfg = new NativeMethods.OpenConfig
            {
                PhysicalName = Str(options.PhysicalName),
                Virt1Name = Str(options.Virtual1Name),
                Virt2Name = Str(options.Virtual2Name),
                Virt3Name = Str(options.Virtual3Name),
                TapPids = pidArray,
                TapCount = pids.Count,
                TapIsolate = options.IsolateTaps ? 1 : 0,
            };

            var err = new byte[512];
            var handle = NativeMethods.ah_io_open(ref cfg, err, err.Length);
            if (handle == IntPtr.Zero) throw new AudioEngineException(ReadCString(err));

            return new AudioEngine(
                handle,
                NativeMethods.ah_io_inputs(handle),
                NativeMethods.ah_io_outputs(handle),
                NativeMethods.ah_io_physical_inputs(handle),
                NativeMethods.ah_io_physical_outputs(handle));
        }
        finally
        {
            foreach (var p in allocated) Marshal.FreeCoTaskMem(p);
        }
    }

    public void Start()
    {
        ThrowIfDisposed();
        if (_running) return;
        if (NativeMethods.ah_io_start(_handle) != 0)
            throw new AudioEngineException("Gagal menjalankan engine. Periksa perangkat dan pengaturan sample rate di Windows.");
        _running = true;
    }

    public void Stop()
    {
        if (_handle == IntPtr.Zero || !_running) return;
        NativeMethods.ah_io_stop(_handle);
        _running = false;
    }

    public bool RouteAllowed(int input, int output) =>
        _handle != IntPtr.Zero && NativeMethods.ah_io_route_allowed(_handle, input, output) != 0;

    public ChannelOwnership ChannelOwnerOf(bool output, int channel)
    {
        ThrowIfDisposed();
        NativeMethods.ah_io_channel_owner(_handle, output ? 1 : 0, channel, out var owner, out var local);
        return new ChannelOwnership(owner, local);
    }

    public bool SetRoute(int input, int output, bool enabled, float sendDb)
    {
        ThrowIfDisposed();
        return NativeMethods.ah_io_set_route(_handle, input, output, enabled ? 1 : 0, sendDb) == 0;
    }

    public void SetChannel(int input, float gainDb, bool mute)
    {
        ThrowIfDisposed();
        NativeMethods.ah_io_set_channel(_handle, input, gainDb, mute ? 1 : 0);
    }

    public void SetChannelPan(int input, float pan)
    {
        ThrowIfDisposed();
        NativeMethods.ah_io_set_channel_pan(_handle, input, pan);
    }

    public void SetOutput(int output, float gainDb, bool mute)
    {
        ThrowIfDisposed();
        NativeMethods.ah_io_set_output(_handle, output, gainDb, mute ? 1 : 0);
    }

    public void SetInputDynamics(int input, GateSettings gate, EqSettings eq, CompSettings comp)
    {
        ThrowIfDisposed();
        var g = gate.ToNative();
        var e = eq.ToNative();
        var c = comp.ToNative();
        NativeMethods.ah_io_set_input_fx(_handle, input, ref g, ref e, ref c);
    }

    public void SetInputDelay(int input, DelaySettings delay)
    {
        ThrowIfDisposed();
        var d = delay.ToNative();
        NativeMethods.ah_io_set_input_delay(_handle, input, ref d);
    }

    public void SetOutputLimiter(int output, LimiterSettings limiter)
    {
        ThrowIfDisposed();
        var l = limiter.ToNative();
        NativeMethods.ah_io_set_output_limiter(_handle, output, ref l);
    }

    // Mengisi inPeaks (panjang Inputs) dan outPeaks (panjang Outputs) dengan peak blok terakhir, linear.
    public void ReadPeaks(float[] inPeaks, float[] outPeaks)
    {
        if (_handle == IntPtr.Zero) return;
        NativeMethods.ah_io_peaks(_handle, inPeaks, outPeaks);
    }

    public EngineStats GetStats()
    {
        ThrowIfDisposed();
        NativeMethods.ah_io_stats(_handle, out var s);
        return new EngineStats(s.Callbacks, s.Dropouts, s.MissingFrames, s.Inputs, s.Outputs, s.BufferFrames,
                               s.SampleRate, s.InputLatencyFrames, s.OutputLatencyFrames);
    }

    public bool StartRecording(int output, string path)
    {
        ThrowIfDisposed();
        var bytes = Encoding.UTF8.GetBytes(path + "\0");
        var p = Marshal.AllocCoTaskMem(bytes.Length);
        try
        {
            Marshal.Copy(bytes, 0, p, bytes.Length);
            return NativeMethods.ah_io_record_start(_handle, output, p) == 0;
        }
        finally
        {
            Marshal.FreeCoTaskMem(p);
        }
    }

    public void StopRecording()
    {
        if (_handle != IntPtr.Zero) NativeMethods.ah_io_record_stop(_handle);
    }

    public (ulong Written, ulong Dropped) RecordingStats()
    {
        if (_handle == IntPtr.Zero) return (0, 0);
        NativeMethods.ah_io_record_stats(_handle, out var written, out var dropped);
        return (written, dropped);
    }

    // Daftar endpoint aktif di sistem. Tidak membutuhkan engine yang sudah dibuka.
    public static IReadOnlyList<EndpointInfo> ListEndpoints()
    {
        var buf = new byte[64 * 1024];
        int n = NativeMethods.ah_io_list_devices(buf, buf.Length);
        return DeviceParser.ParseEndpoints(Encoding.UTF8.GetString(buf, 0, Math.Min(n, buf.Length)));
    }

    // Proses yang punya sesi audio. Dipakai untuk memilih aplikasi yang ditangkap.
    public static IReadOnlyList<ProcessInfo> ListProcesses()
    {
        var buf = new byte[64 * 1024];
        int n = NativeMethods.ah_io_list_processes(buf, buf.Length);
        return DeviceParser.ParseProcesses(Encoding.UTF8.GetString(buf, 0, Math.Min(n, buf.Length)));
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        NativeMethods.ah_io_close(_handle);
        _handle = IntPtr.Zero;
        _running = false;
    }

    private void ThrowIfDisposed()
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(AudioEngine));
    }

    private static string ReadCString(byte[] buf)
    {
        int end = Array.IndexOf(buf, (byte)0);
        if (end < 0) end = buf.Length;
        var text = Encoding.UTF8.GetString(buf, 0, end);
        return text.Length > 0 ? text : "Gagal membuka perangkat audio.";
    }
}
