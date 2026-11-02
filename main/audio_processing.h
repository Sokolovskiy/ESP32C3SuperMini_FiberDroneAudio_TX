#pragma once
#include <stdint.h>
#include "audio_config.h"

typedef struct {
    uint32_t gain_q8;
    uint32_t peak24;
    uint32_t limiter_blocks;
    uint32_t clips;
} audio_processor_t;

/* ESP32 Philips stereo words contain left-aligned signed PCM24. */
int32_t audio_pcm24_from_i2s(uint32_t word);
void audio_process_block(audio_processor_t *state,
                         const uint32_t stereo[AUDIO_SAMPLES_PER_PACKET * 2U],
                         int16_t pcm[AUDIO_SAMPLES_PER_PACKET]);
