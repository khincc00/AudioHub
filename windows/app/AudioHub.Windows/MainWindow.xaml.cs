using AudioHub.Core;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Windows.Storage.Pickers;
using WinRT.Interop;
using static AudioHub.Windows.Localization;

namespace AudioHub.Windows;

public sealed partial class MainWindow : Window
{
    // Nama perangkat virtual AVHub di Windows. Driver-nya belum dibuat; lihat windows/driver/README.md.
    private const string Virt12Name = "AVHub VC 1/2";
    private const string Virt34Name = "AVHub VC 3/4";
    private const string LoopbackName = "AVHub Loopback";

    private readonly AppSettings _settings;
    private readonly DispatcherQueueTimer _meterTimer;
    private readonly DispatcherQueueTimer _statsTimer;
    private readonly List<Strip> _inputStrips = new();
    private readonly List<Strip> _outputStrips = new();
    private readonly Dictionary<(int Input, int Output), ToggleButton> _routeCells = new();
    private readonly Dictionary<int, InputDsp> _inputDsp = new();
    private readonly Dictionary<int, LimiterSettings> _outputLimiter = new();
    private AudioEngine? _engine;
    private float[] _inPeaks = Array.Empty<float>();
    private float[] _outPeaks = Array.Empty<float>();
    private string? _recordingPath;
    private bool _ready;

    public MainWindow()
    {
        InitializeComponent();
        _settings = AppSettings.Load();
        ActiveLanguage = _settings.Language;

        _meterTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _meterTimer.Interval = TimeSpan.FromMilliseconds(33);
        _meterTimer.Tick += (_, _) => UpdateMeters();

        _statsTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _statsTimer.Interval = TimeSpan.FromSeconds(1);
        _statsTimer.Tick += (_, _) => UpdateStats();

        Closed += (_, _) => CloseEngine();

        LanguageBox.Items.Add(new ComboBoxItem { Content = "English", Tag = English });
        LanguageBox.Items.Add(new ComboBoxItem { Content = "Bahasa Indonesia", Tag = Indonesian });
        LanguageBox.SelectedIndex = ActiveLanguage == Indonesian ? 1 : 0;

        ApplyText();
        RefreshDevices();
        RefreshProcesses();
        SetStatus(T("statusClosed"));
        _ready = true;
    }

    private sealed class Strip
    {
        public required int Index { get; init; }
        public required bool IsOutput { get; init; }
        public required int Owner { get; init; }
        public required int Local { get; init; }
        public required TextBlock Name { get; init; }
        public required ProgressBar Meter { get; init; }
        public required Slider Gain { get; init; }
        public required ToggleButton Mute { get; init; }
        public required Button Fx { get; init; }
        public required StackPanel Root { get; init; }
    }

    private sealed class InputDsp
    {
        public GateSettings Gate { get; set; } = new();
        public EqSettings Eq { get; set; } = new();
        public CompSettings Comp { get; set; } = new();
        public DelaySettings Delay { get; set; } = new();
    }

    // MARK: - Teks dan bahasa

    private void ApplyText()
    {
        TitleText.Text = T("title");
        Title = T("title");
        LanguageLabel.Text = T("language");
        PhysicalLabel.Text = T("physicalDevice");
        Virt12Check.Content = T("virtual12");
        Virt34Check.Content = T("virtual34");
        LoopCheck.Content = T("loopback");
        OpenButton.Content = T("open");
        CloseButton.Content = T("close");
        StartButton.Content = T("start");
        StopButton.Content = T("stop");
        RecordButton.Content = _recordingPath is null ? T("record") : T("stopRecord");
        SaveSceneButton.Content = T("saveScene");
        LoadSceneButton.Content = T("loadScene");
        RefreshButton.Content = T("refresh");
        AppsHeader.Text = T("apps");
        AppsHint.Text = T("appsHint");
        IsolateCheck.Content = T("isolate");
        MatrixHeader.Text = T("matrix");
        InputsHeader.Text = T("inputs");
        OutputsHeader.Text = T("outputs");
        foreach (var strip in _inputStrips.Concat(_outputStrips))
        {
            strip.Name.Text = ChannelName(strip.Owner, strip.Local, strip.IsOutput);
            strip.Mute.Content = T("mute");
            strip.Fx.Content = T("dynamics");
        }
    }

    private void LanguageBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_ready || LanguageBox.SelectedItem is not ComboBoxItem { Tag: string lang }) return;
        ActiveLanguage = lang;
        _settings.Language = lang;
        _settings.Save();
        ApplyText();
        if (_engine is null) SetStatus(T("statusClosed"));
    }

    private static string ChannelName(int owner, int local, bool output)
    {
        return owner switch
        {
            ChannelOwner.Physical => $"{T(output ? "physOut" : "physIn")} {local}",
            ChannelOwner.Virtual1 => $"{T(output ? "vcOut" : "vcIn")} {local}",
            ChannelOwner.Virtual2 => $"{T(output ? "vcOut" : "vcIn")} {local + 2}",
            ChannelOwner.Virtual3 => $"{T(output ? "loopOut" : "loopIn")} {local}",
            _ => $"{T("appIn")} {local}",
        };
    }

    private void SetStatus(string text) => StatusText.Text = text;

    private async void ShowError(string message)
    {
        var dialog = new ContentDialog
        {
            Title = T("errorTitle"),
            Content = message,
            CloseButtonText = T("ok"),
            XamlRoot = RootGrid.XamlRoot,
        };
        await dialog.ShowAsync();
    }

    // MARK: - Perangkat dan proses

    private void RefreshButton_Click(object sender, RoutedEventArgs e)
    {
        RefreshDevices();
        RefreshProcesses();
    }

    private void RefreshDevices()
    {
        var endpoints = AudioEngine.ListEndpoints();
        var inputKeys = endpoints.Where(x => x.Flow == EndpointFlow.Input).Select(x => DeviceMatching.KeyOf(x.Name));
        var outputKeys = endpoints.Where(x => x.Flow == EndpointFlow.Output).Select(x => DeviceMatching.KeyOf(x.Name));
        // Perangkat fisik hanya ditawarkan jika input dan output-nya sama-sama ada.
        var keys = inputKeys.Intersect(outputKeys).Distinct().OrderBy(k => k, StringComparer.CurrentCultureIgnoreCase).ToList();

        PhysicalBox.Items.Clear();
        foreach (var key in keys)
        {
            PhysicalBox.Items.Add(new ComboBoxItem { Content = key, Tag = key });
        }
        var previous = PhysicalBox.Items.OfType<ComboBoxItem>().FirstOrDefault(i => (string)i.Tag == _settings.PhysicalKey);
        PhysicalBox.SelectedItem = previous ?? PhysicalBox.Items.FirstOrDefault();

        Virt12Check.IsChecked = _settings.Virtual12;
        Virt34Check.IsChecked = _settings.Virtual34;
        LoopCheck.IsChecked = _settings.Loopback;
    }

    private void RefreshProcesses()
    {
        var selected = ProcessList.SelectedItems.OfType<ListViewItem>().Select(i => (uint)i.Tag).ToHashSet();
        ProcessList.Items.Clear();
        foreach (var p in AudioEngine.ListProcesses())
        {
            var item = new ListViewItem { Content = $"{p.Name} ({p.ProcessId})", Tag = p.ProcessId };
            ProcessList.Items.Add(item);
            if (selected.Contains(p.ProcessId)) ProcessList.SelectedItems.Add(item);
        }
        if (ProcessList.Items.Count == 0) SetStatus(T("noAppsFound"));
    }

    // MARK: - Buka, mulai, berhenti, tutup

    private void OpenButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is not null) return;
        if (PhysicalBox.SelectedItem is not ComboBoxItem { Tag: string key })
        {
            ShowError(T("pickDevice"));
            return;
        }

        var taps = ProcessList.SelectedItems.OfType<ListViewItem>().Select(i => (uint)i.Tag).ToList();
        var options = new OpenOptions(
            key,
            Virt12Check.IsChecked == true ? Virt12Name : null,
            Virt34Check.IsChecked == true ? Virt34Name : null,
            LoopCheck.IsChecked == true ? LoopbackName : null,
            taps,
            IsolateCheck.IsChecked == true);
        try
        {
            _engine = AudioEngine.Open(options);
        }
        catch (AudioEngineException ex)
        {
            ShowError(ex.Message);
            return;
        }

        _settings.PhysicalKey = key;
        _settings.Virtual12 = Virt12Check.IsChecked == true;
        _settings.Virtual34 = Virt34Check.IsChecked == true;
        _settings.Loopback = LoopCheck.IsChecked == true;
        _settings.Save();

        _inPeaks = new float[_engine.Inputs];
        _outPeaks = new float[_engine.Outputs];
        BuildMatrix();
        BuildStrips();
        PopulateRecordOutputs();
        _meterTimer.Start();
        _statsTimer.Start();
        SetStatus(T("statusOpened"));
    }

    private void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null) return;
        try
        {
            _engine.Start();
            SetStatus(T("statusRunning"));
        }
        catch (AudioEngineException ex)
        {
            ShowError(ex.Message);
        }
    }

    private void StopButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null) return;
        _engine.Stop();
        SetStatus(T("statusOpened"));
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e) => CloseEngine();

    private void CloseEngine()
    {
        if (_engine is null) return;
        _meterTimer.Stop();
        _statsTimer.Stop();
        if (_recordingPath is not null) StopRecording();
        _engine.Dispose();
        _engine = null;
        MatrixGrid.Children.Clear();
        MatrixGrid.RowDefinitions.Clear();
        MatrixGrid.ColumnDefinitions.Clear();
        _routeCells.Clear();
        InputStrips.Children.Clear();
        OutputStrips.Children.Clear();
        _inputStrips.Clear();
        _outputStrips.Clear();
        RecordOutputBox.Items.Clear();
        SetStatus(T("statusClosed"));
    }

    // MARK: - Matriks routing

    private void BuildMatrix()
    {
        MatrixGrid.Children.Clear();
        MatrixGrid.RowDefinitions.Clear();
        MatrixGrid.ColumnDefinitions.Clear();
        _routeCells.Clear();
        var engine = _engine!;
        int nIn = engine.Inputs;
        int nOut = engine.Outputs;

        // Baris dan kolom pertama untuk header.
        for (int c = 0; c <= nOut; c++) MatrixGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        for (int r = 0; r <= nIn; r++) MatrixGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        for (int o = 0; o < nOut; o++)
        {
            var owner = engine.ChannelOwnerOf(true, o);
            var header = new TextBlock
            {
                Text = ChannelName(owner.Owner, owner.LocalChannel, true),
                Width = 36,
                TextAlignment = TextAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis,
                FontSize = 11,
            };
            Grid.SetRow(header, 0);
            Grid.SetColumn(header, o + 1);
            MatrixGrid.Children.Add(header);
        }

        for (int i = 0; i < nIn; i++)
        {
            var owner = engine.ChannelOwnerOf(false, i);
            var label = new TextBlock { Text = ChannelName(owner.Owner, owner.LocalChannel, false), Width = 120, FontSize = 11 };
            Grid.SetRow(label, i + 1);
            Grid.SetColumn(label, 0);
            MatrixGrid.Children.Add(label);
        }

        for (int i = 0; i < nIn; i++)
        {
            for (int o = 0; o < nOut; o++)
            {
                int input = i;
                int output = o;
                var cell = new ToggleButton { Width = 36, Height = 28, Padding = new Thickness(0), Tag = (i, o) };
                bool loop = IsLoop(input, output);
                bool allowed = engine.RouteAllowed(input, output);
                cell.IsEnabled = allowed && !loop;
                if (loop) ToolTipService.SetToolTip(cell, T("loopHint"));
                cell.Checked += (_, _) => engine.SetRoute(input, output, true, 0f);
                cell.Unchecked += (_, _) => engine.SetRoute(input, output, false, 0f);
                Grid.SetRow(cell, i + 1);
                Grid.SetColumn(cell, o + 1);
                MatrixGrid.Children.Add(cell);
                _routeCells[(i, o)] = cell;
            }
        }
    }

    // Route dari virtual ke virtual yang sama membentuk loop (route_policy.h).
    private bool IsLoop(int input, int output)
    {
        var inOwner = _engine!.ChannelOwnerOf(false, input).Owner;
        var outOwner = _engine.ChannelOwnerOf(true, output).Owner;
        bool virtualInput = inOwner is ChannelOwner.Virtual1 or ChannelOwner.Virtual2 or ChannelOwner.Virtual3;
        return virtualInput && inOwner == outOwner;
    }

    // MARK: - Strip input dan output

    private void BuildStrips()
    {
        InputStrips.Children.Clear();
        OutputStrips.Children.Clear();
        _inputStrips.Clear();
        _outputStrips.Clear();

        for (int i = 0; i < _engine!.Inputs; i++)
        {
            var strip = MakeStrip(i, false);
            _inputStrips.Add(strip);
            InputStrips.Children.Add(strip.Root);
            if (_inputDsp.TryGetValue(i, out var dsp)) ApplyDsp(i, dsp);
        }
        for (int o = 0; o < _engine.Outputs; o++)
        {
            var strip = MakeStrip(o, true);
            _outputStrips.Add(strip);
            OutputStrips.Children.Add(strip.Root);
            if (_outputLimiter.TryGetValue(o, out var limiter)) _engine.SetOutputLimiter(o, limiter);
        }
    }

    private Strip MakeStrip(int index, bool output)
    {
        var owner = _engine!.ChannelOwnerOf(output, index);
        var name = new TextBlock
        {
            Text = ChannelName(owner.Owner, owner.LocalChannel, output),
            Width = 110,
            TextTrimming = TextTrimming.CharacterEllipsis,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var meter = new ProgressBar { Minimum = 0, Maximum = 100, Width = 70, Height = 8, VerticalAlignment = VerticalAlignment.Center };
        var gain = new Slider { Minimum = -60, Maximum = 12, Value = 0, StepFrequency = 0.5, Width = 110 };
        var mute = new ToggleButton { Padding = new Thickness(6, 2, 6, 2) };
        var fx = new Button { Padding = new Thickness(6, 2, 6, 2) };
        var root = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };

        var strip = new Strip
        {
            Index = index,
            IsOutput = output,
            Owner = owner.Owner,
            Local = owner.LocalChannel,
            Name = name,
            Meter = meter,
            Gain = gain,
            Mute = mute,
            Fx = fx,
            Root = root,
        };

        root.Children.Add(name);
        root.Children.Add(meter);
        root.Children.Add(gain);
        root.Children.Add(mute);
        root.Children.Add(fx);

        gain.ValueChanged += (_, _) => PushGain(strip);
        mute.Checked += (_, _) => PushGain(strip);
        mute.Unchecked += (_, _) => PushGain(strip);
        fx.Click += (_, _) => ShowFxDialog(strip);
        mute.Content = T("mute");
        fx.Content = T("dynamics");
        return strip;
    }

    private void PushGain(Strip strip)
    {
        if (_engine is null) return;
        bool mute = strip.Mute.IsChecked == true;
        if (strip.IsOutput) _engine.SetOutput(strip.Index, (float)strip.Gain.Value, mute);
        else _engine.SetChannel(strip.Index, (float)strip.Gain.Value, mute);
    }

    private void UpdateMeters()
    {
        if (_engine is null) return;
        _engine.ReadPeaks(_inPeaks, _outPeaks);
        foreach (var s in _inputStrips) s.Meter.Value = MeterPercent(_inPeaks[s.Index]);
        foreach (var s in _outputStrips) s.Meter.Value = MeterPercent(_outPeaks[s.Index]);
    }

    // Peak linear ke persen pada skala −60 dB … 0 dB.
    private static double MeterPercent(float peak)
    {
        if (peak <= 0) return 0;
        double db = 20 * Math.Log10(peak);
        return Math.Clamp((db + 60) / 60 * 100, 0, 100);
    }

    private void UpdateStats()
    {
        if (_engine is null) return;
        if (!_engine.IsAlive)
        {
            SetStatus(T("statusAlive"));
            return;
        }
        var s = _engine.GetStats();
        double latencyMs = s.SampleRate > 0 ? (s.InputLatencyFrames + s.OutputLatencyFrames) * 1000.0 / s.SampleRate : 0;
        string state = _engine.IsRunning ? T("statusRunning") : T("statusOpened");
        string text = $"{state}  |  {T("statusDropouts")}: {s.Dropouts}  |  {T("statusLatency")}: {latencyMs:F1} ms";
        if (_recordingPath is not null) text += $"  |  {T("statusRecording")}: {Path.GetFileName(_recordingPath)}";
        SetStatus(text);
    }

    // MARK: - Dinamika per input dan limiter per output

    private void ApplyDsp(int input, InputDsp dsp)
    {
        _engine!.SetInputDynamics(input, dsp.Gate, dsp.Eq, dsp.Comp);
        _engine.SetInputDelay(input, dsp.Delay);
    }

    private InputDsp DspOf(int input)
    {
        if (!_inputDsp.TryGetValue(input, out var dsp))
        {
            dsp = new InputDsp();
            _inputDsp[input] = dsp;
        }
        return dsp;
    }

    private async void ShowFxDialog(Strip strip)
    {
        if (_engine is null) return;
        if (strip.IsOutput) await ShowLimiterDialog(strip);
        else await ShowInputDialog(strip);
    }

    private async Task ShowInputDialog(Strip strip)
    {
        var current = DspOf(strip.Index);
        var panel = new StackPanel { Spacing = 6, Width = 420 };

        var gateOn = AddToggle(panel, T("gate"), current.Gate.Enabled);
        var gateThr = AddSlider(panel, T("threshold"), -80, 0, current.Gate.ThresholdDb);
        var gateAtk = AddSlider(panel, T("attack"), 0, 100, current.Gate.AttackMs);
        var gateRel = AddSlider(panel, T("release"), 0, 1000, current.Gate.ReleaseMs);
        var gateHold = AddSlider(panel, T("hold"), 0, 500, current.Gate.HoldMs);

        var compOn = AddToggle(panel, T("comp"), current.Comp.Enabled);
        var compThr = AddSlider(panel, T("threshold"), -60, 0, current.Comp.ThresholdDb);
        var compRatio = AddSlider(panel, T("ratio"), 1, 20, current.Comp.Ratio);
        var compAtk = AddSlider(panel, T("attack"), 0, 200, current.Comp.AttackMs);
        var compRel = AddSlider(panel, T("release"), 0, 1000, current.Comp.ReleaseMs);
        var compMakeup = AddSlider(panel, T("makeup"), 0, 24, current.Comp.MakeupDb);

        var eqOn = AddToggle(panel, T("eq"), current.Eq.Enabled);
        var eqLow = AddSlider(panel, T("low"), -15, 15, current.Eq.LowDb);
        var eqMid = AddSlider(panel, T("mid"), -15, 15, current.Eq.MidDb);
        var eqHigh = AddSlider(panel, T("high"), -15, 15, current.Eq.HighDb);

        var delayOn = AddToggle(panel, T("delay"), current.Delay.Enabled);
        var delayMs = AddSlider(panel, T("delayMs"), 0, 1000, current.Delay.DelayMs);

        var dialog = new ContentDialog
        {
            Title = Format("fxTitle", strip.Name.Text),
            Content = new ScrollViewer { Content = panel, MaxHeight = 520 },
            PrimaryButtonText = T("ok"),
            CloseButtonText = T("cancel"),
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = RootGrid.XamlRoot,
        };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;

        var gate = new GateSettings(gateOn.IsOn, (float)gateThr.Value, (float)gateAtk.Value,
                                    (float)gateRel.Value, (float)gateHold.Value);
        var comp = new CompSettings(compOn.IsOn, (float)compThr.Value, (float)compRatio.Value,
                                    (float)compAtk.Value, (float)compRel.Value, (float)compMakeup.Value,
                                    current.Comp.KneeDb, current.Comp.SidechainInput);
        var eq = new EqSettings(eqOn.IsOn, (float)eqLow.Value, current.Eq.LowHz, (float)eqMid.Value,
                                current.Eq.MidHz, current.Eq.MidQ, (float)eqHigh.Value, current.Eq.HighHz);
        var delay = new DelaySettings(delayOn.IsOn, (float)delayMs.Value);

        current.Gate = gate;
        current.Comp = comp;
        current.Eq = eq;
        current.Delay = delay;
        ApplyDsp(strip.Index, current);
    }

    private async Task ShowLimiterDialog(Strip strip)
    {
        var current = _outputLimiter.TryGetValue(strip.Index, out var l) ? l : new LimiterSettings();
        var panel = new StackPanel { Spacing = 6, Width = 380 };
        var on = AddToggle(panel, T("limiter"), current.Enabled);
        var ceiling = AddSlider(panel, T("ceiling"), -12, 0, current.CeilingDb);
        var release = AddSlider(panel, T("release"), 1, 1000, current.ReleaseMs);

        var dialog = new ContentDialog
        {
            Title = Format("limiterTitle", strip.Name.Text),
            Content = panel,
            PrimaryButtonText = T("ok"),
            CloseButtonText = T("cancel"),
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = RootGrid.XamlRoot,
        };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;

        var updated = new LimiterSettings(on.IsOn, (float)ceiling.Value, (float)release.Value, current.LookaheadMs);
        _outputLimiter[strip.Index] = updated;
        _engine!.SetOutputLimiter(strip.Index, updated);
    }

    private static ToggleSwitch AddToggle(Panel parent, string header, bool on)
    {
        var t = new ToggleSwitch { Header = header, IsOn = on };
        parent.Children.Add(t);
        return t;
    }

    private static Slider AddSlider(Panel parent, string label, double min, double max, double value)
    {
        parent.Children.Add(new TextBlock { Text = label, Opacity = 0.8 });
        var s = new Slider { Minimum = min, Maximum = max, Value = Math.Clamp(value, min, max), StepFrequency = 0.5 };
        parent.Children.Add(s);
        return s;
    }

    // MARK: - Matriks: tombol routing ke output (untuk rekaman)

    private void PopulateRecordOutputs()
    {
        RecordOutputBox.Items.Clear();
        for (int o = 0; o < _engine!.Outputs; o++)
        {
            var owner = _engine.ChannelOwnerOf(true, o);
            RecordOutputBox.Items.Add(new ComboBoxItem
            {
                Content = ChannelName(owner.Owner, owner.LocalChannel, true),
                Tag = o,
            });
        }
        RecordOutputBox.SelectedIndex = 0;
    }

    private void RecordButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null) return;
        if (_recordingPath is not null)
        {
            StopRecording();
            RecordButton.Content = T("record");
            return;
        }

        int output = RecordOutputBox.SelectedItem is ComboBoxItem { Tag: int o } ? o : 0;
        var folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyMusic), "AudioHub");
        Directory.CreateDirectory(folder);
        var path = Path.Combine(folder, $"AudioHub-{DateTime.Now:yyyyMMdd-HHmmss}.wav");
        if (!_engine.StartRecording(output, path))
        {
            ShowError(T("recordFailed"));
            return;
        }
        _recordingPath = path;
        RecordButton.Content = T("stopRecord");
    }

    private void StopRecording()
    {
        _engine?.StopRecording();
        var (written, dropped) = _engine?.RecordingStats() ?? (0UL, 0UL);
        SetStatus($"{T("statusRecorded")}: {Path.GetFileName(_recordingPath)}  ({written} frames, {T("statusDropouts")}: {dropped})");
        _recordingPath = null;
        RecordButton.Content = T("record");
    }

    // MARK: - Scene

    private async void SaveSceneButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null) return;
        var picker = new FileSavePicker { SuggestedFileName = "scene" };
        picker.FileTypeChoices.Add("AudioHub scene", new List<string> { ".json" });
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        var file = await picker.PickSaveFileAsync();
        if (file is null) return;

        var scene = BuildScene(Path.GetFileNameWithoutExtension(file.Name));
        await File.WriteAllTextAsync(file.Path, SceneStore.Serialize(scene));
        SetStatus($"{T("statusRecorded")}: {file.Name}");
    }

    private async void LoadSceneButton_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null)
        {
            ShowError(T("noDevice"));
            return;
        }
        var picker = new FileOpenPicker();
        picker.FileTypeFilter.Add(".json");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        var file = await picker.PickSingleFileAsync();
        if (file is null) return;

        SceneFile scene;
        try
        {
            scene = SceneStore.Deserialize(await File.ReadAllTextAsync(file.Path));
        }
        catch (SceneFormatException ex)
        {
            ShowError(ex.Message);
            return;
        }
        ApplyScene(scene);
    }

    private SceneFile BuildScene(string name)
    {
        var scene = new SceneFile
        {
            Name = string.IsNullOrWhiteSpace(name) ? "scene" : name,
            DeviceName = _settings.PhysicalKey ?? "",
            UseVirtual = Virt12Check.IsChecked == true,
            UseVirtual2 = Virt34Check.IsChecked == true,
            UseVirtual3 = LoopCheck.IsChecked == true,
        };

        foreach (var s in _inputStrips)
        {
            var dsp = DspOf(s.Index);
            scene.Inputs.Add(new SceneInput
            {
                Id = $"in:{s.Index}",
                GainDb = (float)s.Gain.Value,
                Mute = s.Mute.IsChecked == true,
                Fx = new SceneChannelFx
                {
                    Name = s.Name.Text,
                    Gate = new SceneGate { Enabled = dsp.Gate.Enabled, ThresholdDb = dsp.Gate.ThresholdDb, AttackMs = dsp.Gate.AttackMs, ReleaseMs = dsp.Gate.ReleaseMs, HoldMs = dsp.Gate.HoldMs },
                    Comp = new SceneComp { Enabled = dsp.Comp.Enabled, ThresholdDb = dsp.Comp.ThresholdDb, Ratio = dsp.Comp.Ratio, AttackMs = dsp.Comp.AttackMs, ReleaseMs = dsp.Comp.ReleaseMs, MakeupDb = dsp.Comp.MakeupDb, KneeDb = dsp.Comp.KneeDb },
                    Eq = new SceneEq { Enabled = dsp.Eq.Enabled, LowDb = dsp.Eq.LowDb, LowHz = dsp.Eq.LowHz, MidDb = dsp.Eq.MidDb, MidHz = dsp.Eq.MidHz, MidQ = dsp.Eq.MidQ, HighDb = dsp.Eq.HighDb, HighHz = dsp.Eq.HighHz },
                    Delay = new SceneDelay { Enabled = dsp.Delay.Enabled, DelayMs = dsp.Delay.DelayMs },
                },
            });
        }

        foreach (var s in _outputStrips)
        {
            var limiter = _outputLimiter.TryGetValue(s.Index, out var l) ? l : new LimiterSettings();
            scene.Outputs.Add(new SceneOutput
            {
                Id = $"out:{s.Index}",
                Name = s.Name.Text,
                GainDb = (float)s.Gain.Value,
                Mute = s.Mute.IsChecked == true,
                Limiter = new SceneLimiter { Enabled = limiter.Enabled, CeilingDb = limiter.CeilingDb, ReleaseMs = limiter.ReleaseMs },
            });
        }

        foreach (var ((input, output), cell) in _routeCells)
        {
            if (cell.IsChecked == true)
            {
                scene.Routes.Add(new SceneRoute { From = $"in:{input}", To = $"out:{output}", SendDb = 0, Enabled = true });
            }
        }
        return scene;
    }

    // Menerapkan scene ke perangkat yang sedang dibuka. Scene dengan jumlah kanal berbeda diterapkan sebagian.
    private void ApplyScene(SceneFile scene)
    {
        foreach (var input in scene.Inputs)
        {
            if (!TryIndex(input.Id, out int i) || i >= _inputStrips.Count) continue;
            var strip = _inputStrips[i];
            strip.Gain.Value = Math.Clamp(input.GainDb, -60, 12);
            strip.Mute.IsChecked = input.Mute;
            var dsp = DspOf(i);
            dsp.Gate = new GateSettings(input.Fx.Gate.Enabled, input.Fx.Gate.ThresholdDb, input.Fx.Gate.AttackMs, input.Fx.Gate.ReleaseMs, input.Fx.Gate.HoldMs);
            dsp.Comp = new CompSettings(input.Fx.Comp.Enabled, input.Fx.Comp.ThresholdDb, input.Fx.Comp.Ratio, input.Fx.Comp.AttackMs, input.Fx.Comp.ReleaseMs, input.Fx.Comp.MakeupDb, input.Fx.Comp.KneeDb);
            dsp.Eq = new EqSettings(input.Fx.Eq.Enabled, input.Fx.Eq.LowDb, input.Fx.Eq.LowHz, input.Fx.Eq.MidDb, input.Fx.Eq.MidHz, input.Fx.Eq.MidQ, input.Fx.Eq.HighDb, input.Fx.Eq.HighHz);
            dsp.Delay = new DelaySettings(input.Fx.Delay.Enabled, input.Fx.Delay.DelayMs);
            ApplyDsp(i, dsp);
        }

        foreach (var output in scene.Outputs)
        {
            if (!TryIndex(output.Id, out int o) || o >= _outputStrips.Count) continue;
            var strip = _outputStrips[o];
            strip.Gain.Value = Math.Clamp(output.GainDb, -60, 12);
            strip.Mute.IsChecked = output.Mute;
            var limiter = new LimiterSettings(output.Limiter.Enabled, output.Limiter.CeilingDb, output.Limiter.ReleaseMs);
            _outputLimiter[o] = limiter;
            _engine!.SetOutputLimiter(o, limiter);
        }

        foreach (var cell in _routeCells.Values) cell.IsChecked = false;
        foreach (var route in scene.Routes)
        {
            if (!route.Enabled) continue;
            if (!TryIndex(route.From, out int i) || !TryIndex(route.To, out int o)) continue;
            if (_routeCells.TryGetValue((i, o), out var cell) && cell.IsEnabled) cell.IsChecked = true;
        }
    }

    private static bool TryIndex(string id, out int index)
    {
        index = -1;
        int colon = id.IndexOf(':');
        return colon >= 0 && int.TryParse(id.AsSpan(colon + 1), out index);
    }
}
