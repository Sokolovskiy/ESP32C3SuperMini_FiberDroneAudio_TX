#include "audio_capture.h"
#include "audio_config.h"
#include "esp_attr.h"
#include "esp_check.h"

static bool IRAM_ATTR capture_overflow(i2s_chan_handle_t channel,
                                      i2s_event_data_t *event, void *context)
{
    (void)channel;
    (void)event;
    audio_capture_t *capture = context;
    atomic_fetch_add_explicit(&capture->overflows, 1, memory_order_relaxed);
    return false;
}

void audio_capture_init(audio_capture_t *capture)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = AUDIO_DMA_DESCRIPTORS;
    channel.dma_frame_num = AUDIO_SAMPLES_PER_PACKET;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, NULL, &capture->channel));
    /* Preserve the microphone-tested Philips, 32-bit stereo configuration. */
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(
            (AUDIO_RX_CLOCK_HZ + AUDIO_RX_CLOCK_DIVISOR / 2U) / AUDIO_RX_CLOCK_DIVISOR),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_BCLK_GPIO,
            .ws = AUDIO_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_DIN_GPIO,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(capture->channel, &config));
    /* 160 MHz / 37.5 / 256 = 16666.6667 Hz at zero trim.
     * The driver selects the nearest supported fractional divider. */
    int64_t numerator = (int64_t)AUDIO_RX_CLOCK_HZ * AUDIO_MCLK_MULTIPLE *
                        (1000000 + AUDIO_RATE_TRIM_PPM);
    int64_t denominator = (int64_t)AUDIO_RX_CLOCK_DIVISOR * 1000000;
    i2s_tuning_config_t tune = {
        .tune_mode = I2S_TUNING_MODE_SET,
        .tune_mclk_val = (int32_t)((numerator + denominator / 2) / denominator),
        .min_delta_mclk = -100000,
        .max_delta_mclk = 100000,
    };
    i2s_tuning_info_t info;
    ESP_ERROR_CHECK(i2s_channel_tune_rate(capture->channel, &tune, &info));
    capture->clock_rate_hz = (double)info.curr_mclk_hz / AUDIO_MCLK_MULTIPLE;
    i2s_event_callbacks_t callbacks = {.on_recv_q_ovf = capture_overflow};
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(capture->channel, &callbacks, capture));
}
