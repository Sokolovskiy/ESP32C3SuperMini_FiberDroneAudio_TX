param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$image = Join-Path $PSScriptRoot 'FiberDroneAudio_TX_full.bin'
if (-not (Test-Path -LiteralPath $image)) { throw "Firmware image not found: $image" }
# This writes only the explicitly selected device. No erase-flash is necessary.
& $Python -m esptool --chip esp32c3 --port $Port --baud 460800 `
    --before default-reset --after hard-reset write-flash `
    --flash-mode dio --flash-freq 80m --flash-size 2MB 0x0 $image
if ($LASTEXITCODE -ne 0) { throw "Flashing failed on $Port" }
Write-Host "Firmware written successfully to $Port. Check USB startup/audio diagnostics."
