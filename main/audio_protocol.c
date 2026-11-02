#include "audio_protocol.h"

_Static_assert(AUDIO_PACKET_BYTES == 3U + AUDIO_SAMPLES_PER_PACKET * 2U + 1U,
               "STM32 RX packet size must remain unchanged");

uint8_t audio_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? 0x07U : 0U));
    }
    return crc;
}

void audio_packet_encode(uint8_t packet[AUDIO_PACKET_BYTES], uint8_t sequence,
                         const int16_t pcm[AUDIO_SAMPLES_PER_PACKET])
{
    packet[0] = 0xA5;
    packet[1] = 0x5A;
    packet[2] = sequence;
    for (unsigned i = 0; i < AUDIO_SAMPLES_PER_PACKET; ++i) {
        uint16_t raw = (uint16_t)pcm[i];
        packet[3U + i * 2U] = (uint8_t)raw;
        packet[4U + i * 2U] = (uint8_t)(raw >> 8);
    }
    packet[AUDIO_PACKET_BYTES - 1U] = audio_crc8(packet + 2, 33);
}
