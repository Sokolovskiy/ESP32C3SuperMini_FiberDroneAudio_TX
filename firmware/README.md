# Flash ESP32-C3 Super Mini

## Download

Download these files and save them in the same folder of your choice:

- [Firmware: FiberDroneAudio_TX_full.bin](FiberDroneAudio_TX_full.bin?raw=true)
- [Optional Windows flashing script: flash.ps1](flash.ps1?raw=true)
- [SHA-256 checksum: SHA256SUMS.txt](SHA256SUMS.txt?raw=true)

On GitHub, use **Download raw file** if clicking a link opens the file preview. You do not need to clone the source repository or install ESP-IDF to flash this image.

The combined image includes the bootloader, partition table and microphone application. Its flash address is **0x0**. Settings: ESP32-C3, DIO, 80 MHz flash, 2 MB configured flash size, microphone enabled, USB diagnostics enabled, zero sample-rate trim. Use boards compatible with these settings. This image contains no board-specific identity or complete flash dump.

## Flash with Python and esptool

1. Install Python if it is not already available. Open a terminal in the folder containing the downloaded firmware.
2. Install the flashing tool:

   ```powershell
   python -m pip install esptool==5.6.0
   ```

3. Disconnect the board's FC 5V supply and connect the board through USB. Close any serial monitor using its port.
4. Find the board's port in Windows Device Manager under **Ports (COM & LPT)**. Enter that port when prompted, then flash:

   ```powershell
   $port = Read-Host 'ESP32 serial port'
   python -m esptool --chip esp32c3 --port $port --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 2MB 0x0 .\FiberDroneAudio_TX_full.bin
   ```

5. Wait for successful write verification. Tap RESET if the application does not start automatically.

The commands above use Windows PowerShell. On Linux/macOS, use `python3` if needed and replace `$port` with the actual device path, such as `/dev/ttyACM0` or `/dev/cu.usbmodem...`; use the path assigned to your board.

If automatic bootloader entry fails, hold **BOOT**, tap **RESET**, release **BOOT**, then retry using the currently assigned port.

## Optional Windows script

With `flash.ps1` and the firmware in the same folder, run:

```powershell
$port = Read-Host 'ESP32 serial port'
.\flash.ps1 -Port $port
```

Python and esptool must already be installed. If you use an ESP-IDF terminal, its Python environment already includes esptool. The optional `-Python` argument selects a different Python executable.

## Flash multiple boards

Connect one board at a time and repeat the flashing command with its assigned port. The same binary works for identical compatible boards; rebuilding is unnecessary. After each flash, check that the microphone/optical/RX audio path works. Successful flash verification does not validate the audio link or TX/RX clock drift.

For USB diagnostics with a source checkout and an ESP-IDF terminal, run `idf.py -p $port monitor` from the repository root. Exit with `Ctrl+]`.

## Regenerate the firmware after source changes

This section is for developers with ESP-IDF v6.1 installed. From the repository root in an ESP-IDF PowerShell terminal:

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

Offsets/settings come from `build/flasher_args.json`. Update the flashing instructions and script settings if partitioning or flash settings change. See the [official esptool guide](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/esptool/basic-commands.html).
