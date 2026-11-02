# FiberDroneAudio TX — ESP32-C3

ESP-IDF v6.1, C. INMP441 → I2S DMA → original STM32 gain/limiter → PCM16 mono → UART1 → optical transmitter. The STM32 RX firmware and packet format are unchanged. No Wi-Fi/Bluetooth services are initialized.

## Get the project or ready-to-flash firmware

```powershell
git clone https://github.com/Sokolovskiy/ESP32C3SuperMini_FiberDroneAudio_TX.git
cd ESP32C3SuperMini_FiberDroneAudio_TX
idf.py build
```

Run these commands in an ESP-IDF v6.1 terminal. The checked-in `sdkconfig` preserves the configuration used for the supplied binary; `sdkconfig.defaults` records the basic target/console defaults. The COM port depends on the connected board.

For flashing identical boards without rebuilding, use [the combined firmware image](firmware/FiberDroneAudio_TX_full.bin) and [batch flashing instructions](firmware/README.md). The combined image includes the bootloader, partition table and microphone application and is written at address **0x0**. The `build` directory and machine-specific VS Code settings are excluded from Git.

## Wiring

| Signal | ESP32-C3 Super Mini |
|---|---|
| Microphone VDD | 3V3 |
| Microphone GND | GND |
| Microphone SCK/BCLK | GPIO4 |
| Microphone WS/LRCLK | GPIO5 |
| Microphone SD | GPIO6 |
| Microphone L/R | GND (left) |
| Optical module UART input | GPIO10, UART1 TX, 3.3 V logic |
| Optical module local ground | ESP32 GND |
| Diagnostic/flash connection | Native USB, GPIO18/19 internally |

GPIO10 is a valid ESP32-C3 output and UART1 can be routed through the GPIO matrix. It has no conflict with this project's I2S or native USB signals. SPI2 is not used. Super Mini is a board family with multiple vendors: verify that the pad marked `10` on your actual board is GPIO10 and has no additional load. The optical module model/schematic was not supplied; its supply voltage and input electrical compatibility must be checked separately. Do not infer its power supply from the UART logic level.

The tested Philips I2S master configuration is retained: two 32-bit slots, left channel only, BCLK/WS/DIN 4/5/6, unused MCLK output. The sample rate and DMA block sizing change for RX compatibility and latency. The diagnostic console is explicitly moved from UART0 plus secondary USB to primary USB Serial/JTAG. GPIO10 carries only binary audio packets; ROM boot output also uses separate console signals.

## Build and flash

In the ESP-IDF v6.1 PowerShell terminal:

```powershell
cd C:\Projects\ESP_FiberDroneAudio_TX
idf.py build
idf.py -p COM144 flash monitor
```

For an ordinary PowerShell terminal on this machine, initialize the installed environment first:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
```

`Ctrl+]` exits the monitor. If Windows assigns a different port after reconnecting USB, replace COM144 with that port. Flash only TX. Hardware has not been flashed or tested as part of this migration.

The application binary is `build/FiberDroneAudio_TX.bin`. Use `idf.py flash` to also program its matching bootloader and partition table; the application binary alone is not a complete blank-chip image.

## Configuration

All pipeline parameters are in `main/audio_config.h`:

- `AUDIO_TEST_TONE=0`: live microphone, default. `1`: 1 kHz sine, PCM16 peak 4096 before volume. Tone bypasses microphone gain/limiting and uses I2S DMA for pacing, even if the microphone is disconnected. The phase increment uses the driver clock estimate, rather than a 16-sample repeating table or a millisecond task delay.
- `AUDIO_DIAGNOSTICS=1`: one USB report approximately every 5 seconds. Set to `0` to disable periodic reports; startup/error logging remains.
- `AUDIO_RATE_TRIM_PPM=0`: nominal RX rate. Positive trim speeds TX up; negative trim slows it down. Rebuild and flash after editing. Fractional divider resolution limits attainable rates; read the startup estimate and verify WS with an instrument.
- Gain 64x, volume 100%, limiter ceiling `0x00100000`, release 4 Q8 units per 16-sample block match the supplied STM32 TX. The ceiling corresponds to about ±4096 PCM16, intentionally well below full scale. No noise gate, extra compressor or noise suppression was present in the supplied active TX path or added here.

The packet format is fixed: `A5 5A sequence [16 signed PCM16 little-endian samples] CRC`, exactly 36 bytes. CRC-8 is MSB-first polynomial 0x07, init 0, no final XOR, covering the 33 bytes starting at sequence. Sequence wraps modulo 256. Unlike the old TX busy-drop path, local queue/UART drops consume sequence numbers, allowing the existing RX loss concealment to observe them. DMA overruns also advance sequence by the reported number of lost descriptors. After an I2S timeout, the exact number of absent samples is unknown; the error/drop counters record an event, not an exact count of all samples absent during the interruption.

## Clock compatibility and drift

The supplied RX `SystemClock_Config` selects HSI, no PLL, AHB/APB1 divide by 1. TIM6 therefore nominally receives 16 MHz. PSC=7 and ARR=119 give:

```text
Fs_RX = 16,000,000 / (8 × 120) = 16,666.6666667 samples/s
packet interval = 16 / Fs_RX = 960 us
BCLK = 64 × Fs = 1,066,666.6667 Hz
UART traffic = 36 × 10 × Fs / 16 = 375,000 bit/s (40.69% of 921600)
one packet on UART = 390.625 us
```

The ESP32-C3 default I2S source in the installed IDF is PLL_F160M. After standard initialization, `i2s_channel_tune_rate` sets MCLK to the nearest representable value of `Fs_RX × 256 × (1 + trim/1e6)`, before enabling capture. At zero trim the divider is 37 + 1/2, so the nominal rate can match RX exactly. An integer `sample_rate_hz=16667` alone is not the frequency guarantee.

The startup `clock estimate` is the driver's integer-Hz MCLK result divided by 256 (approximately 16666.664063 Hz at zero trim due to reporting truncation). It is calculated from the nominal source clock, not measured externally. The 5-second `rate` measures samples successfully read against `esp_timer`; it has block-boundary/scheduling uncertainty and is not an independent measurement of crystal accuracy.

HSI on STM32 and the ESP32 crystal are independent. Even perfect nominal divider matching cannot eliminate physical or temperature-dependent drift. RX has a 256-sample queue and starts/rebuffers at 64 samples. A 100 ppm mismatch changes occupancy by about 1.667 samples/s; 64 samples of margin disappear in about 38.4 seconds if TX is slower. Extra buffering only delays failure. At the old ESP32 test rate of 16 kHz the mismatch would be about 666.67 samples/s.

Calibration without changing RX:

1. Measure TX WS and the RX DAC trigger rate against the same instrument timebase. The DAC trigger can be inferred from its stable test tone or inspected through the existing debugger/timer registers; do not change RX firmware.
2. Set `trim_ppm = round((measured_RX_rate / measured_TX_rate - 1) × 1e6)`, starting from zero trim. When refining a nonzero trim, apply this ratio to the existing multiplier.
3. Rebuild/flash TX, check the achieved WS frequency, and observe RX queue occupancy/counters over at least 30–60 minutes and the expected temperature range.
4. TX slower → queue trends down/rebuffer events; TX faster → queue trends up/overflow events. Tune accordingly.

There is no reverse telemetry in the supplied optical link. A TX-only automatic feedback loop cannot know RX queue occupancy or its changing HSI frequency. A manual trim is provided; reliable operation over wide HSI drift may require a shared/external reference or RX feedback, neither of which is assumed here. Deliberately dropping packet sequence numbers does not solve drift because the RX generates replacement samples for small sequence gaps.

The supplied old TX main file does not contain its SAI peripheral clock selection (`HAL_SAI_MspInit`/RCCEx configuration). Its exact physical sample rate cannot be established from that file alone; RX timing is sufficient to choose the new target rate. The old test-tone table contains only zeroes, so the new real 1 kHz generator deliberately replaces that inactive placeholder.

The subsequently supplied RX `stm32g4xx_hal_msp.c` confirms `RCC_USART2CLKSOURCE_PCLK1`. Combined with the supplied RX main file, this gives a nominal 16 MHz USART2 clock. Oversampling 16 and prescaler 1 yield BRR=17, hence a nominal hardware receive baud of 16,000,000/17 = 941,176.47 despite the configured 921600. This is calculated using the [ST HAL divider formula](https://github.com/STMicroelectronics/stm32g4xx-hal-driver/blob/master/Inc/stm32g4xx_hal_uart.h); physical baud still follows the actual HSI frequency. ESP32 TX at 921600 is nominally 2.08% slower than that receiver baud. New TX stays configured at 921600 as required; verify framing/noise/CRC errors on RX under real clock and temperature variation. A debugger reading of USART2 BRR can confirm the running firmware. No baud change is made silently.

The RX MSP file also reveals a wiring discrepancy with the original task: USART2 RX is configured on **PA15 / AF7**, not PA3. USART2 TX is PA2; DAC output is PA4. UART RX DMA uses DMA1_Channel1, byte alignment, peripheral-to-memory, circular mode; DAC DMA uses DMA1_Channel3 in circular mode. If this MSP file matches the flashed RX firmware, connect the optical receiver's UART output to PA15. Verify the actual board wiring/firmware version before moving a connection, particularly if reception already works on PA3.

## Buffering and diagnostics

Six DMA descriptors each hold 16 stereo frames (128 bytes, 0.96 ms). The capture task processes one descriptor at priority 20; UART transmission runs separately at priority 19. A static four-packet queue copies packet contents, so producer buffers cannot be overwritten while the consumer uses them. All task/driver allocations occur at startup; there is no per-block allocation.

UART has no software TX ring; one complete packet is copied to the hardware FIFO after a bounded wait for the preceding transmission. FIFO capacity exceeds 36 bytes. Packets older than 5 ms at the transmitter are discarded. Capture/DMA buffering adds up to roughly 5.76 ms under a stall, separate from the software queue's age limit and RX buffering. Latency is bounded by these buffers; normal transmission starts as each 16-sample block arrives. I2S errors restart capture; unrecoverable setup/restart errors use ESP-IDF's checked error handler. UART timeouts/short writes are counted and the affected packet is dropped. Persistent faults require checking the USB report and wiring. An unplugged optical cable cannot be detected from a TX-only UART output.

Illustrative output (not an actual hardware measurement):

```text
I (...) audio_tx: I2S Philips 32-bit stereo, left mono; clock estimate 16666.664063 Hz, trim +0 ppm
I (...) audio_tx: UART1 TX GPIO10: 921600 8N1, 36 bytes/16 samples; tone=0
I (...) audio_tx: rate=16666.40 samples/s peak24=12000 gain=64.000x sent=5208 drop=0 i2s_err=0 uart_err=0 dma_ovf=0 queue_ovf=0 limiter=0 clips=0
```

`sent` counts complete packets copied into the UART FIFO, not receiver acknowledgements. Counters are cumulative 32-bit values and wrap naturally. `peak24` is the maximum microphone magnitude during the reporting interval; gain is the latest block value. Rate is successfully consumed microphone frames per second, including in tone mode. Limiter/clips counters describe microphone processing; tone bypasses its output.

## Software validation

Run the actual portable C processing/protocol modules on Windows:

```powershell
.\tests\run.ps1
# Or supply your own native C compiler:
.\tests\run.ps1 -Compiler 'C:\path\to\gcc.exe'
```

The tests check CRC `123456789 → F4`, independent polynomial division, every single-bit error in sequence/payload, 36-byte boundaries, little-endian extremes, 1024 sequence assignments including wrap, ESP32 PCM24 alignment/sign, right-channel rejection, full-scale limiter attack, release to 64x, and 100,000 blocks / 1.6 million samples against original STM32 arithmetic. They do not validate peripheral timing or audio hardware.

Validation on 2026-11-02: host C tests PASS; `idf.py build` PASS for microphone with diagnostics and for 1 kHz tone with periodic diagnostics disabled. The final source configuration and binary are microphone mode with diagnostics enabled. Application size: 0x317C0 (202,688 bytes), fitting the 1 MiB application partition with 81% free. Build logs: `build/microphone-build.log` and `build/tone-build.log`. ESP-IDF's full initial configuration reported upstream Kconfig/component-validation notices; the final incremental builds reported no C compiler warnings or errors in these modules. No physical flash or audio test was performed.

## Hardware acceptance checklist

1. Verify the table above, especially L/R grounded, the actual GPIO10 pad, optical module voltage and local common ground. Leave the existing STM32 RX firmware intact.
2. Build and flash TX only. Open native USB monitor on COM144. Confirm the expected startup configuration, no resets, approximately 16,667 consumed frames/s, and zero error/drop/overflow counters after startup.
3. Probe WS/BCLK: approximately 16.6667 kHz / 1.066667 MHz. Speak near the microphone; `peak24` should change. Verify the high 24 bits carry the signed microphone signal if using a logic analyzer.
4. Enable tone, rebuild and flash TX. Probe GPIO10 and decode 921600 8N1: `A5 5A`, 36-byte frames, advancing sequence, valid CRC, about 1041.67 packets/s. No text should appear in this binary stream. Check it at the optical receiver output as well.
5. Connect the existing STM32 RX. Listen at a comfortable initial volume for a steady approximately 1 kHz tone. Through its existing debugger, inspect `uartErrorCount`, `crcErrorCount`, `lostPacketCount`, `pcmQueueOverflowCount`, `pcmQueueUnderflowCount` and `dacRebufferCount` where available. Inspect queue occupancy trends; a TX log alone cannot prove RX acceptance.
6. Restore `AUDIO_TEST_TONE=0`, rebuild/flash TX and check intelligible speech, quiet input, normal voice and loud transients. Gain should reduce immediately on a large block peak and recover gradually; clips should stay zero. The conservative ±4096 ceiling matches the old sound level.
7. Run at least 30–60 minutes. Record RX underflow/overflow/rebuffer counters and queue occupancy at intervals, plus TX diagnostics. Check temperature variation. Calibrate TX trim if occupancy systematically drifts.
8. Disconnect/reconnect the optical link and power-cycle TX/RX separately. Confirm RX resynchronizes and resumes through its existing rebuffer behavior. TX counters cannot detect a disconnected fiber. Require stable operation, no ongoing losses/resets, and no increasing latency before accepting hardware compatibility.

Useful primary references: [ESP32-C3 I2S](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/i2s.html), [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html). Installed ESP-IDF v6.1 headers/driver sources were also inspected for clock tuning, GPIO routing and UART FIFO ownership.

## Files

- Changed: `main/main.c`, `main/CMakeLists.txt`, `sdkconfig` (USB console; generated compatibility values are normalized by IDF).
- Added: `main/audio_config.h`, `main/audio_capture.c/.h`, `main/audio_processing.c/.h`, `main/audio_protocol.c/.h`, `sdkconfig.defaults`, `tests/test_audio.c`, `tests/run.ps1`, this `README.md`.
