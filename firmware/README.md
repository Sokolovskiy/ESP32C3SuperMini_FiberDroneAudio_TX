# Flashing identical ESP32-C3 Super Mini boards

`FiberDroneAudio_TX_full.bin` combines the current bootloader (0x0), partition table (0x8000), and microphone-mode application (0x10000). Flash this combined image at **0x0**. It uses the existing project settings: DIO, 80 MHz flash, 2 MB configured flash size, microphone enabled, USB diagnostics enabled, zero sample-rate trim. Only use it on boards compatible with those settings. It does not include board-specific secrets or a copy of another device's complete flash.

Initialize the installed ESP-IDF PowerShell environment on this computer:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
cd C:\Projects\ESP_FiberDroneAudio_TX
```

Connect one board through USB, disconnect its FC 5V supply, close any serial monitor using its port, and run:

```powershell
.\firmware\flash.ps1 -Port COM144
```

Repeat for each board, supplying the actual Windows COM port. No source rebuild is needed between identical boards. Alternatively, use:

```powershell
python -m esptool --chip esp32c3 --port COM144 --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 2MB 0x0 .\firmware\FiberDroneAudio_TX_full.bin
```

If automatic bootloader entry fails, hold BOOT, tap RESET, release BOOT and retry using the port shown by Windows. After flashing, tap RESET if necessary. Inspect startup/audio diagnostics with `idf.py -p COM144 monitor` from the source project. Exit with Ctrl+]. Test the microphone/optical/RX audio path on each unit. USB flashing success does not validate audio or remove the need to check TX/RX clock drift.

For another Windows PC without ESP-IDF, Python plus esptool is sufficient:

```powershell
python -m pip install esptool==5.6.0
.\flash.ps1 -Port COM144
```

Copy this entire `firmware` directory to that PC. The optional `-Python` script argument can select a different Python executable. `SHA256SUMS.txt` records the combined image checksum.

After changing firmware source/configuration, rebuild and regenerate this bundle from the project root in an ESP-IDF terminal:

```powershell
idf.py build
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Push-Location build
try {
    python -m esptool --chip esp32c3 merge-bin -o ..\firmware\FiberDroneAudio_TX_full.bin '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw 'Firmware merge failed' }
} finally {
    Pop-Location
}
$digest = (Get-FileHash .\firmware\FiberDroneAudio_TX_full.bin -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  FiberDroneAudio_TX_full.bin" | Set-Content .\firmware\SHA256SUMS.txt
```

These offsets/settings come from the current `build/flasher_args.json`; regenerate instructions/script settings if partitioning or flash settings change. Merging with esptool is documented in the [official guide](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/esptool/basic-commands.html).
