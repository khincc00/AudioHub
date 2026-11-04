; Installer AudioHub untuk Windows (Inno Setup 6).
; Dibangun dari CI atau manual:
;   iscc /DAppVersion=0.3.0 /DPublishDir="..\build\publish" /DDriverDir="..\driver\build" AudioHub.iss
; PublishDir: hasil `dotnet publish` aplikasi WinUI (berisi AudioHub.exe dan audiohub_io.dll).
; DriverDir : folder driver AVHub (SimpleAudioSample.inf, simpleaudiosample.sys, SimpleAudioSample.cat, devcon.exe). Opsional; dilewati bila tidak ada.

#ifndef AppVersion
  #define AppVersion "0.3.0"
#endif
#ifndef PublishDir
  #define PublishDir "..\build\publish"
#endif
#ifndef DriverDir
  #define DriverDir "..\driver\build"
#endif

[Setup]
AppId={{912303A6-1268-41E1-92CF-459B1C6AE4D3}
AppName=AudioHub
AppVersion={#AppVersion}
AppPublisher=KhinccOfficial
DefaultDirName={autopf}\AudioHub
DefaultGroupName=AudioHub
OutputDir=..\build\installer
OutputBaseFilename=AudioHub-Setup-{#AppVersion}-win64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile=..\..\LICENSE
UninstallDisplayIcon={app}\AudioHub.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "installdriver"; Description: "Pasang driver perangkat virtual AVHub (diperlukan untuk VC 1/2, VC 3/4, dan Loopback)"; GroupDescription: "Driver"; Flags: checkedonce

[Files]
Source: "{#PublishDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DriverDir}\*"; DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Tasks: installdriver
Source: "..\driver\scripts\install-driver.ps1"; DestDir: "{app}\driver"; Flags: ignoreversion; Tasks: installdriver
Source: "..\driver\scripts\uninstall-driver.ps1"; DestDir: "{app}\driver"; Flags: ignoreversion; Tasks: installdriver

[Icons]
Name: "{group}\AudioHub"; Filename: "{app}\AudioHub.exe"
Name: "{group}\Uninstall AudioHub"; Filename: "{uninstallexe}"
Name: "{autodesktop}\AudioHub"; Filename: "{app}\AudioHub.exe"; Tasks: desktopicon

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\driver\install-driver.ps1"" -DriverDir ""{app}\driver"""; StatusMsg: "Memasang driver AVHub..."; Flags: runhidden waituntilterminated; Tasks: installdriver
Filename: "{app}\AudioHub.exe"; Description: "{cm:LaunchProgram,AudioHub}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\driver\uninstall-driver.ps1"""; Flags: runhidden waituntilterminated skipifdoesntexist

