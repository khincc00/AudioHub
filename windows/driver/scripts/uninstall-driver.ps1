<#
  Menghapus perangkat dan paket driver AVHub. Membutuhkan hak administrator.
  Devnode dihapus lewat devcon (memerlukan devcon.exe di folder yang sama), lalu paket dihapus dari driver store.
#>
param(
    [string]$DriverDir = (Join-Path $PSScriptRoot ".")
)

$ErrorActionPreference = "Stop"

$devcon = Join-Path $DriverDir "devcon.exe"
if (Test-Path $devcon) {
    foreach ($hwid in @("ROOT\AVHUBVC12", "ROOT\AVHUBVC34", "ROOT\AVHUBLOOP")) {
        & $devcon remove $hwid | Out-Host
    }
} else {
    Write-Host "devcon.exe tidak ditemukan. Hapus perangkat AVHub secara manual di Device Manager."
}

# Cari paket di driver store berdasarkan nama berkas INF asli.
$lines = & pnputil.exe /enum-drivers
$packages = @()
$current = @{}
foreach ($line in $lines) {
    if ($line -match "^Published Name:\s*(.+)$") { $current = @{ Name = $matches[1].Trim() } }
    elseif ($line -match "^Original Name:\s*(.+)$") { $current.Original = $matches[1].Trim() }
    elseif ($line.Trim() -eq "" -and $current.Name) { $packages += [pscustomobject]$current; $current = @{} }
}
if ($current.Name) { $packages += [pscustomobject]$current }

$found = $packages | Where-Object { $_.Original -eq "SimpleAudioSample.inf" }
if (-not $found) {
    Write-Host "Tidak ada paket AVHub di driver store."
    exit 0
}
foreach ($pkg in $found) {
    & pnputil.exe /delete-driver $pkg.Name /uninstall /force | Out-Host
}
exit 0
