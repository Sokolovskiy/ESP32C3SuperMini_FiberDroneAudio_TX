#pragma once
#include <stddef.h>
#include <stdint.h>
#include "audio_config.h"

uint8_t audio_crc8(const uint8_t *data, size_t length);
void audio_packet_encode(uint8_t packet[AUDIO_PACKET_BYTES], uint8_t sequence,
                         const int16_t pcm[AUDIO_SAMPLES_PER_PACKET]);
