#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include "driver/i2s_std.h"

typedef struct {
    i2s_chan_handle_t channel;
    atomic_uint overflows;
    double clock_rate_hz;
} audio_capture_t;

/* Allocates DMA once and tunes the nominal RX rate; leaves the channel READY. */
void audio_capture_init(audio_capture_t *capture);
