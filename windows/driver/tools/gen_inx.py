"""
Generates Source/Main/SimpleAudioSample.inx for the AVHub driver.

The driver binary is the upstream Simple Audio Sample (one speaker render endpoint and one
microphone capture endpoint per device instance). Three root devices use the same binary:
  ROOT\\AVHUBVC12  -> "AVHub VC 1/2"
  ROOT\\AVHUBVC34  -> "AVHub VC 3/4"
  ROOT\\AVHUBLOOP  -> "AVHub Loopback"
Each device has its own install section so that its endpoints get their own names.
The output is UTF-16LE with a BOM and CRLF, like the upstream .inx.

Usage: python3 gen_inx.py <path-to-SimpleAudioSample.inx>
"""
import sys

DEVICES = [
    # section prefix, root hardware id, device description, render name, capture name
    ("AVHUB_VC12", r"ROOT\AVHUBVC12", "AVHub VC 1/2", "AVHub VC 1/2 Out", "AVHub VC 1/2 In"),
    ("AVHUB_VC34", r"ROOT\AVHUBVC34", "AVHub VC 3/4", "AVHub VC 3/4 Out", "AVHub VC 3/4 In"),
    ("AVHUB_LOOP", r"ROOT\AVHUBLOOP", "AVHub Loopback", "AVHub Loopback Out", "AVHub Loopback In"),
]


def device_sections(prefix):
    """Install section, interfaces, and endpoint AddReg sections for one device."""
    s = []
    s.append(f"[{prefix}_SA.NT]")
    s.append("Include = ks.inf, wdmaudio.inf")
    s.append("Needs = KS.Registration, WDMAUDIO.Registration")
    s.append("CopyFiles = SIMPLEAUDIOSAMPLE_SA.CopyList")
    s.append("AddReg = SIMPLEAUDIOSAMPLE_SA.AddReg")
    s.append("")
    s.append(f"[{prefix}_SA.NT.Interfaces]")
    p = prefix + "."
    for cat, name, sec in [
        ("%KSCATEGORY_AUDIO%", "%KSNAME_WaveSpeaker%", "I.WaveSpeaker"),
        ("%KSCATEGORY_RENDER%", "%KSNAME_WaveSpeaker%", "I.WaveSpeaker"),
        ("%KSCATEGORY_REALTIME%", "%KSNAME_WaveSpeaker%", "I.WaveSpeaker"),
        ("%KSCATEGORY_AUDIO%", "%KSNAME_TopologySpeaker%", "I.TopologySpeaker"),
        ("%KSCATEGORY_TOPOLOGY%", "%KSNAME_TopologySpeaker%", "I.TopologySpeaker"),
        ("%KSCATEGORY_AUDIO%", "%KSNAME_WaveMicArray1%", "I.WaveMicArray1"),
        ("%KSCATEGORY_REALTIME%", "%KSNAME_WaveMicArray1%", "I.WaveMicArray1"),
        ("%KSCATEGORY_CAPTURE%", "%KSNAME_WaveMicArray1%", "I.WaveMicArray1"),
        ("%KSCATEGORY_AUDIO%", "%KSNAME_TopologyMicArray1%", "I.TopologyMicArray1"),
        ("%KSCATEGORY_TOPOLOGY%", "%KSNAME_TopologyMicArray1%", "I.TopologyMicArray1"),
    ]:
        s.append(f"AddInterface = {cat}, {name}, {p}{sec}")
    s.append("")
    s.append(f"[{prefix}_SA.NT.HW]")
    s.append("AddReg = AUDIOHW.AddReg")
    s.append("")
    s.append(f"[{prefix}_SA.NT.Services]")
    s.append("AddService = SimpleAudioSample, 0x00000002, SimpleAudioSample_Service_Inst")
    s.append("")

    # Wave render (speaker). Topology render adds the endpoint association keys.
    for sec, pname in [
        ("I.WaveSpeaker", "WaveSpeaker"),
        ("I.TopologySpeaker", "TopologySpeaker"),
        ("I.WaveMicArray1", "WaveMicArray1"),
        ("I.TopologyMicArray1", "TopologyMicArray1"),
    ]:
        s.append(f"[{prefix}.{sec}]")
        s.append(f"AddReg = {prefix}.{sec}.AddReg")
        s.append("")
        s.append(f"[{prefix}.{sec}.AddReg]")
        s.append("HKR, , CLSID, , %Proxy.CLSID%")
        s.append(f"HKR, , FriendlyName, , %{prefix}.{pname}.szPname%")
        if sec.startswith("I.Topology"):
            s.append(r"HKR, EP\0, %PKEY_AudioEndpoint_Association%, , %KSNODETYPE_ANY%")
            s.append(r"HKR, EP\0, %PKEY_AudioEndpoint_Supports_EventDriven_Mode%, 0x00010001, 0x1")
        s.append("")
    return s


def build():
    lines = []
    lines += [
        "[Version]",
        'Signature = "$Windows NT$"',
        "Class = MEDIA",
        "ClassGUID = {4d36e96c-e325-11ce-bfc1-08002be10318}",
        "Provider = %ProviderName%",
        "DriverVer = 11/04/2026,1.0.0.1",
        "CatalogFile = SimpleAudioSample.cat",
        "PnpLockdown = 1",
        "",
        "[SourceDisksNames]",
        '2222 = "AVHub Virtual Audio Driver Disk",,,""',
        "",
        "[SourceDisksFiles]",
        "simpleaudiosample.sys = 2222",
        "",
        "[SignatureAttributes]",
        "simpleaudiosample.sys = SignatureAttributes.DRM",
        "",
        "[SignatureAttributes.DRM]",
        "DRMLevel = 1300",
        "",
        "[SignatureAttributes.PETrust]",
        "PETrust = true",
        "",
        "[Manufacturer]",
        "%MfgName% = AVHUB, NT$ARCH$.10.0...22000",
        "",
        "[AVHUB.NT$ARCH$.10.0...22000]",
    ]
    for prefix, hwid, desc, _, _ in DEVICES:
        lines.append(f"%{prefix}.DeviceDesc% = {prefix}_SA, {hwid}")
    lines += [
        "",
        "[DestinationDirs]",
        "SIMPLEAUDIOSAMPLE_SA.CopyList = 13   ;   13 = Package's DriverStore directory",
        "",
        ";=============================================================================",
        ";   Shared install settings",
        ";=============================================================================",
        "[SIMPLEAUDIOSAMPLE_SA.CopyList]",
        "simpleaudiosample.sys",
        "",
        "[SIMPLEAUDIOSAMPLE_SA.AddReg]",
        "HKR,,AssociatedFilters,,\"wdmaud,swmidi,redbook\"",
        "HKR,,Driver,,simpleaudiosample.sys",
        "",
        ";  For demo only, review these power management settings for your architecture.",
        "HKR,PowerSettings,SingleComponentMultiFxStates,3,1,0,0,0     ;  Turn on multi Fx support.",
        "HKR,PowerSettings,ConservationIdleTime,3,3,0,0,0             ;  3 seconds for idle power management when on battery",
        "HKR,PowerSettings,PerformanceIdleTime,3,3,0,0,0              ;  3 seconds for idle power management when on AC power",
        "HKR,PowerSettings,IdlePowerState,3,3,0,0,0                   ;  go to D3 for idle power management",
        "",
        "HKR,Drivers,SubClasses,,\"wave,midi,mixer\"",
        "HKR,Drivers\\wave\\wdmaud.drv,Driver,,wdmaud.drv",
        "HKR,Drivers\\midi\\wdmaud.drv,Driver,,wdmaud.drv",
        "HKR,Drivers\\mixer\\wdmaud.drv,Driver,,wdmaud.drv",
        "HKR,Drivers\\wave\\wdmaud.drv,Description,,%SIMPLEAUDIOSAMPLE_SA.DeviceDesc%",
        "HKR,Drivers\\mixer\\wdmaud.drv,Description,,%SIMPLEAUDIOSAMPLE_SA.DeviceDesc%",
        "",
        "[AUDIOHW.AddReg]",
        ";  FILE_DEVICE_SOUND",
        "HKR,,DeviceType,0x10001,0x00000001",
        ";  SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX",
        'HKR,,Security,,"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGWGX;;;WD)(A;;GRGWGX;;;RC)"',
        "",
    ]
    for prefix, _, _, _, _ in DEVICES:
        lines += device_sections(prefix)

    lines += [
        ";=============================================================================",
        ";   SimpleAudioSample service",
        ";=============================================================================",
        "[SimpleAudioSample_Service_Inst]",
        "DisplayName = %SimpleAudioSample.SvcDesc%",
        "ServiceType = 1",
        "StartType = 3",
        "ErrorControl = 1",
        "ServiceBinary = %13%\\simpleaudiosample.sys",
        "",
        "[Strings]",
        "",
        "KSNAME_WaveSpeaker = \"WaveSpeaker\"",
        "KSNAME_TopologySpeaker = \"TopologySpeaker\"",
        "KSNAME_WaveMicArray1 = \"WaveMicArray1\"",
        "KSNAME_TopologyMicArray1 = \"TopologyMicArray1\"",
        "",
        "Proxy.CLSID = \"{17CCA71B-ECD7-11D0-B908-00A0C9223196}\"",
        "KSCATEGORY_AUDIO = \"{6994AD04-93EF-11D0-A3CC-00A0C9223196}\"",
        "KSCATEGORY_RENDER = \"{65E8773E-8F56-11D0-A3B9-00A0C9223196}\"",
        "KSCATEGORY_CAPTURE = \"{65E8773D-8F56-11D0-A3B9-00A0C9223196}\"",
        "KSCATEGORY_REALTIME = \"{EB115FFC-10C8-4964-831D-6DCB02E6F23F}\"",
        "KSCATEGORY_TOPOLOGY = \"{DDA54A40-1E4C-11D1-A050-405705C10000}\"",
        "KSNODETYPE_ANY = \"{00000000-0000-0000-0000-000000000000}\"",
        "PKEY_AudioEndpoint_Association = \"{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E}, 2\"",
        "PKEY_AudioEndpoint_Supports_EventDriven_Mode = \"{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E}, 7\"",
        "",
        "ProviderName = \"AudioHub\"",
        "MfgName = \"AudioHub\"",
        "SimpleAudioSample.SvcDesc = \"AVHub Virtual Audio Driver (WDM)\"",
        "SIMPLEAUDIOSAMPLE_SA.DeviceDesc = \"AVHub Virtual Audio Driver (WDM)\"",
        "",
    ]
    for prefix, _, desc, render, capture in DEVICES:
        lines += [
            f"{prefix}.DeviceDesc = \"{desc}\"",
            f"{prefix}.WaveSpeaker.szPname = \"{render}\"",
            f"{prefix}.TopologySpeaker.szPname = \"{render}\"",
            f"{prefix}.WaveMicArray1.szPname = \"{capture}\"",
            f"{prefix}.TopologyMicArray1.szPname = \"{capture}\"",
            "",
        ]
    return "\r\n".join(lines) + "\r\n"


if __name__ == "__main__":
    out = sys.argv[1]
    with open(out, "wb") as f:
        f.write(b"\xff\xfe")  # UTF-16LE BOM
        f.write(build().encode("utf-16-le"))
    print("wrote", out)
