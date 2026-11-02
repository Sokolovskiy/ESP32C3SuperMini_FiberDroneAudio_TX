#include <math.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "audio_capture.h"
#include "audio_processing.h"
#include "audio_protocol.h"
#include "sdkconfig.h"

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "Audio firmware requires USB Serial/JTAG as the diagnostic console"
#endif

#define AUDIO_UART UART_NUM_1
static const char *TAG = "audio_tx";
static audio_capture_t capture;
typedef struct {
    atomic_uint samples, sent, dropped, i2s_errors, uart_errors, queue_overflows;
    atomic_uint interval_peak, gain_q8, limiter_blocks, clips;
} audio_stats_t;
static audio_stats_t stats;
typedef struct {
    int64_t created_us;
    uint8_t bytes[AUDIO_PACKET_BYTES];
} queued_packet_t;
static StaticQueue_t queue_control;
static uint8_t queue_storage[AUDIO_TX_QUEUE_PACKETS * sizeof(queued_packet_t)];
static QueueHandle_t packet_queue;

static void transport_task(void *argument)
{
    (void)argument;
    queued_packet_t packet;
    for (;;) {
        xQueueReceive(packet_queue, &packet, portMAX_DELAY);
        /* No UART software TX ring: at most one 36-byte packet in hardware.
         * uart_tx_chars copies the entire packet before this buffer is reused. */
        esp_err_t error = uart_wait_tx_done(AUDIO_UART, pdMS_TO_TICKS(AUDIO_UART_TIMEOUT_MS));
        if (error != ESP_OK) {
            atomic_fetch_add(&stats.uart_errors, 1);
            atomic_fetch_add(&stats.dropped, 1);
            continue;
        }
        if (esp_timer_get_time() - packet.created_us > AUDIO_MAX_PACKET_AGE_US) {
            atomic_fetch_add(&stats.dropped, 1);
            continue;
        }
        int count = uart_tx_chars(AUDIO_UART, (const char *)packet.bytes, AUDIO_PACKET_BYTES);
        if (count != AUDIO_PACKET_BYTES) {
            atomic_fetch_add(&stats.uart_errors, 1);
            atomic_fetch_add(&stats.dropped, 1);
        } else {
            atomic_fetch_add(&stats.sent, 1);
        }
    }
}

static void capture_task(void *argument)
{
    (void)argument;
    /* Start clocks only after startup logs and the UART consumer are ready. */
    ESP_ERROR_CHECK(i2s_channel_enable(capture.channel));
    audio_processor_t processor = {.gain_q8 = AUDIO_GAIN_Q8};
    uint32_t stereo[AUDIO_SAMPLES_PER_PACKET * 2U];
    int16_t pcm[AUDIO_SAMPLES_PER_PACKET];
    uint8_t sequence = 0;
    uint32_t previous_overflows = 0;
#if AUDIO_TEST_TONE
    double phase = 0;
    const double step = 6.283185307179586 * 1000.0 / capture.clock_rate_hz;
#endif
    for (;;) {
        size_t received = 0;
        esp_err_t error = i2s_channel_read(capture.channel, stereo, sizeof(stereo),
                                          &received, AUDIO_I2S_TIMEOUT_MS);
        uint32_t overflows = atomic_load(&capture.overflows);
        uint32_t lost = overflows - previous_overflows;
        previous_overflows = overflows;
        if (lost) {
            /* One DMA descriptor is exactly one protocol packet. */
            sequence = (uint8_t)(sequence + lost);
            atomic_fetch_add(&stats.dropped, lost);
        }
        if (error != ESP_OK || received != sizeof(stereo)) {
            atomic_fetch_add(&stats.i2s_errors, 1);
            atomic_fetch_add(&stats.dropped, 1);
            ++sequence;
            /* Clear a partial read and queued stale DMA data before restarting. */
            ESP_ERROR_CHECK(i2s_channel_disable(capture.channel));
            vTaskDelay(1);
            ESP_ERROR_CHECK(i2s_channel_enable(capture.channel));
            continue;
        }
        atomic_fetch_add(&stats.samples, AUDIO_SAMPLES_PER_PACKET);
        audio_process_block(&processor, stereo, pcm);
        uint32_t peak = atomic_load(&stats.interval_peak);
        while (peak < processor.peak24 &&
               !atomic_compare_exchange_weak(&stats.interval_peak, &peak, processor.peak24)) {}
        atomic_store(&stats.gain_q8, processor.gain_q8);
        atomic_store(&stats.limiter_blocks, processor.limiter_blocks);
        atomic_store(&stats.clips, processor.clips);
#if AUDIO_TEST_TONE
        /* I2S DMA remains the sole pacing source even with no microphone attached.
         * Test tone bypasses microphone gain and has a fixed PCM16 peak of 4096. */
        if (lost) phase = fmod(phase + step * lost * AUDIO_SAMPLES_PER_PACKET, 6.283185307179586);
        for (unsigned i = 0; i < AUDIO_SAMPLES_PER_PACKET; ++i) {
            pcm[i] = (int16_t)(sin(phase) * 4096.0 * VOLUME_PERCENT / 100.0);
            phase += step;
            if (phase >= 6.283185307179586) phase -= 6.283185307179586;
        }
#endif
        queued_packet_t packet = {.created_us = esp_timer_get_time()};
        /* Assign before enqueue so RX can observe locally dropped packets. */
        audio_packet_encode(packet.bytes, sequence++, pcm);
        if (xQueueSend(packet_queue, &packet, 0) != pdTRUE) {
            atomic_fetch_add(&stats.queue_overflows, 1);
            atomic_fetch_add(&stats.dropped, 1);
        }
    }
}

void app_main(void)
{
    const uart_config_t uart = {
        .baud_rate = AUDIO_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(AUDIO_UART_TX_GPIO) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(uart_param_config(AUDIO_UART, &uart));
    ESP_ERROR_CHECK(uart_set_pin(AUDIO_UART, AUDIO_UART_TX_GPIO, UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(AUDIO_UART, 256, 0, 0, NULL, 0));
    packet_queue = xQueueCreateStatic(AUDIO_TX_QUEUE_PACKETS, sizeof(queued_packet_t),
                                      queue_storage, &queue_control);
    configASSERT(packet_queue);
    audio_capture_init(&capture);
    ESP_LOGI(TAG, "I2S Philips 32-bit stereo, left mono; clock estimate %.6f Hz, trim %+d ppm",
             capture.clock_rate_hz, AUDIO_RATE_TRIM_PPM);
    ESP_LOGI(TAG, "UART1 TX GPIO%d: %d 8N1, 36 bytes/16 samples; tone=%d",
             AUDIO_UART_TX_GPIO, AUDIO_UART_BAUD, AUDIO_TEST_TONE);
    BaseType_t result = xTaskCreate(transport_task, "audio_uart", 3072, NULL, 19, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    result = xTaskCreate(capture_task, "audio_capture", 4096, NULL, 20, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#if AUDIO_DIAGNOSTICS
    uint32_t previous_samples = 0;
    int64_t previous_us = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(AUDIO_REPORT_MS));
        int64_t now = esp_timer_get_time();
        uint32_t samples = atomic_load(&stats.samples);
        double rate = (double)(uint32_t)(samples - previous_samples) * 1000000.0 / (now - previous_us);
        previous_samples = samples;
        previous_us = now;
        ESP_LOGI(TAG, "rate=%.2f samples/s peak24=%u gain=%.3fx sent=%u drop=%u "
                      "i2s_err=%u uart_err=%u dma_ovf=%u queue_ovf=%u limiter=%u clips=%u",
                 rate, atomic_exchange(&stats.interval_peak, 0),
                 atomic_load(&stats.gain_q8) / 256.0, atomic_load(&stats.sent),
                 atomic_load(&stats.dropped), atomic_load(&stats.i2s_errors),
                 atomic_load(&stats.uart_errors), atomic_load(&capture.overflows),
                 atomic_load(&stats.queue_overflows), atomic_load(&stats.limiter_blocks),
                 atomic_load(&stats.clips));
    }
#endif
}
