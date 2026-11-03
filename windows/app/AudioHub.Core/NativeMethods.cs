using System.Runtime.InteropServices;

namespace AudioHub.Core;

// Deklarasi P/Invoke ke audiohub_io.dll (windows/io/include/audiohub_io.h). Struct harus sama persis
// dengan layout C: urutan field dan tipenya.
internal static class NativeMethods
{
    internal const string Library = "audiohub_io";

    [StructLayout(LayoutKind.Sequential)]
    internal struct OpenConfig
    {
        public IntPtr PhysicalName;
        public IntPtr Virt1Name;
        public IntPtr Virt2Name;
        public IntPtr Virt3Name;
        public IntPtr TapPids;
        public int TapCount;
        public int TapIsolate;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Stats
    {
        public ulong Callbacks;
        public ulong Dropouts;
        public ulong MissingFrames;
        public int Inputs;
        public int Outputs;
        public uint BufferFrames;
        public double SampleRate;
        public uint InputLatencyFrames;
        public uint OutputLatencyFrames;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GateParams
    {
        public int Enabled;
        public float ThresholdDb, AttackMs, ReleaseMs, HoldMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct EqParams
    {
        public int Enabled;
        public float LowDb, LowHz, MidDb, MidHz, MidQ, HighDb, HighHz;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CompParams
    {
        public int Enabled;
        public float ThresholdDb, Ratio, AttackMs, ReleaseMs, MakeupDb, KneeDb;
        public int SidechainInput;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DelayParams
    {
        public int Enabled;
        public float DelayMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct LimiterParams
    {
        public int Enabled;
        public float CeilingDb, ReleaseMs, LookaheadMs;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr ah_io_open(ref OpenConfig cfg, byte[] errBuf, int errLen);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_close(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_start(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_stop(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_is_alive(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_inputs(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_outputs(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_physical_inputs(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_physical_outputs(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_route_allowed(IntPtr io, int input, int output);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_channel_owner(IntPtr io, int output, int channel, out int owner, out int local);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_set_route(IntPtr io, int input, int output, int enabled, float sendDb);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_channel(IntPtr io, int input, float gainDb, int mute);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_channel_pan(IntPtr io, int input, float pan);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_output(IntPtr io, int output, float gainDb, int mute);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_input_fx(IntPtr io, int input, ref GateParams gate, ref EqParams eq, ref CompParams comp);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_input_delay(IntPtr io, int input, ref DelayParams delay);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_set_output_limiter(IntPtr io, int output, ref LimiterParams limiter);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_peaks(IntPtr io, float[] inPeaks, float[] outPeaks);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_stats(IntPtr io, out Stats stats);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_record_start(IntPtr io, int output, IntPtr utf8Path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_record_stop(IntPtr io);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ah_io_record_stats(IntPtr io, out ulong written, out ulong dropped);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_list_devices(byte[] buf, int len);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ah_io_list_processes(byte[] buf, int len);
}
