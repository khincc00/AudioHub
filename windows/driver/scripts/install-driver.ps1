<#
  Memasang driver perangkat virtual AVHub (Windows) dan membuat tiga perangkat:
    ROOT\AVHUBVC12  -> AVHub VC 1/2
    ROOT\AVHUBVC34  -> AVHub VC 3/4
    ROOT\AVHUBLOOP  -> AVHub Loopback

  Dijalankan installer sebagai administrator, atau manual dari PowerShell (Run as administrator):
    powershell -ExecutionPolicy Bypass -File install-driver.ps1 -DriverDir "C:\Program Files\AudioHub\driver"

  DriverDir harus berisi SimpleAudioSample.inf, simpleaudiosample.sys, SimpleAudioSample.cat, dan devcon.exe.
  Tanpa SimpleAudioSample.inf, skrip mencetak pesan dan keluar dengan kode 0 agar installer tetap selesai.
  Lihat windows/driver/README.md.
#>
param(
    [string]$DriverDir = (Join-Path $PSScriptRoot ".")
)

$ErrorActionPreference = "Stop"

$inf = Join-Path $DriverDir "SimpleAudioSample.inf"
if (-not (Test-Path $inf)) {
    Write-Host "SimpleAudioSample.inf tidak ditemukan di $DriverDir. Driver AVHub dilewati."
    exit 0
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Pemasangan driver butuh hak administrator."
}

$devcon = Join-Path $DriverDir "devcon.exe"
if (-not (Test-Path $devcon)) {
    throw "devcon.exe tidak ditemukan di $DriverDir. Salin dari Windows Driver Kit (Tools\x64)."
}

# Memasukkan paket ke driver store. Driver ini ditandatangani dengan test certificate pada build uji;
# lihat README tentang testsigning.
& pnputil.exe /add-driver $inf /install | Out-Host
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) {
    throw "pnputil gagal dengan kode $LASTEXITCODE."
}

# Root device tidak dibuat oleh pnputil. devcon install membuat devnode untuk setiap hardware ID.
$hardwareIds = @("ROOT\AVHUBVC12", "ROOT\AVHUBVC34", "ROOT\AVHUBLOOP")
foreach ($hwid in $hardwareIds) {
    & $devcon install $inf $hwid | Out-Host
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 1) {
        throw "devcon gagal membuat $hwid dengan kode $LASTEXITCODE."
    }
}

Write-Host "Driver AVHub terpasang. Periksa di Pengaturan Suara. Restart Windows jika perangkat belum muncul."
exit 0
