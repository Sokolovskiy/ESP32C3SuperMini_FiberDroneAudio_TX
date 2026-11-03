#pragma once

/* RX HSI / (TIM6 PSC+1) / (TIM6 ARR+1). Calibrate against the actual RX. */
#define AUDIO_RX_CLOCK_HZ 16000000U
#define AUDIO_RX_CLOCK_DIVISOR 960U
#ifndef AUDIO_RATE_TRIM_PPM
#define AUDIO_RATE_TRIM_PPM 0
#endif
#ifndef AUDIO_TEST_TONE
#define AUDIO_TEST_TONE 0
#endif
#ifndef AUDIO_DIAGNOSTICS
#define AUDIO_DIAGNOSTICS 1
#endif
#define AUDIO_REPORT_MS 5000U
#define AUDIO_GAIN 64U
#define AUDIO_GAIN_Q8 (AUDIO_GAIN * 256U)
#define VOLUME_PERCENT 100U
#define LIMITER_PEAK_24 0x00100000U
#define LIMITER_RELEASE_STEP_Q8 4U

#define AUDIO_SAMPLES_PER_PACKET 16U
#define AUDIO_PACKET_BYTES 36U
#define AUDIO_DMA_DESCRIPTORS 6U
#define AUDIO_TX_QUEUE_PACKETS 4U
#define AUDIO_MAX_PACKET_AGE_US 5000
#define AUDIO_I2S_TIMEOUT_MS 100U
#define AUDIO_UART_TIMEOUT_MS 20U
#define AUDIO_I2S_BCLK_GPIO 4
#define AUDIO_I2S_WS_GPIO 5
#define AUDIO_I2S_DIN_GPIO 6
#define AUDIO_UART_TX_GPIO 10
/* Match the unchanged STM32 RX: nominal 16 MHz / BRR 17.
 * Confirmed with a clean 1 kHz tone and intelligible microphone audio. */
#define AUDIO_UART_BAUD 941176
#define AUDIO_MCLK_MULTIPLE 256U

_Static_assert(VOLUME_PERCENT <= 100U, "Volume must be 0..100");
_Static_assert(AUDIO_GAIN > 0U && AUDIO_GAIN <= 256U, "Unsupported gain");
_Static_assert(AUDIO_RATE_TRIM_PPM >= -20000 && AUDIO_RATE_TRIM_PPM <= 20000,
               "Trim must be within +/-2 percent");
