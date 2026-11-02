#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio_processing.h"
#include "audio_protocol.h"

/* Polynomial long division, independent of the firmware's shift-register loop. */
static uint8_t reference_crc(const uint8_t *bytes, size_t size)
{
    unsigned remainder = 0;
    for (size_t i = 0; i < size; ++i) {
        remainder ^= (unsigned)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (remainder & (0x8000U >> bit)) remainder ^= 0x107U << (7U - bit);
        }
        remainder <<= 8;
    }
    return (uint8_t)(remainder >> 8);
}

static uint32_t random_state = 12345;
static uint32_t random_word(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static void test_protocol(void)
{
    assert(audio_crc8((const uint8_t *)"123456789", 9) == 0xF4);
    assert(audio_crc8(NULL, 0) == 0);
    const int16_t pcm[16] = {-32768, 32767, -1, 0, 1, -256, 256, -4096,
                             4096, -12345, 12345, -2, 2, -255, 255, 0};
    struct { uint8_t before; uint8_t packet[36]; uint8_t after; } guarded;
    guarded.before = 0xCA;
    guarded.after = 0xFE;
    uint8_t sequence = 0;
    for (unsigned n = 0; n < 1024; ++n) {
        audio_packet_encode(guarded.packet, sequence++, pcm);
        assert(guarded.before == 0xCA && guarded.after == 0xFE);
        assert(guarded.packet[0] == 0xA5 && guarded.packet[1] == 0x5A);
        assert(guarded.packet[2] == (uint8_t)n);
        for (unsigned i = 0; i < 16; ++i) {
            uint16_t word = guarded.packet[3 + i * 2] | ((uint16_t)guarded.packet[4 + i * 2] << 8);
            assert((int16_t)word == pcm[i]);
        }
        assert(guarded.packet[3] == 0 && guarded.packet[4] == 0x80);
        assert(guarded.packet[5] == 0xFF && guarded.packet[6] == 0x7F);
        assert(guarded.packet[35] == reference_crc(guarded.packet + 2, 33));
        for (unsigned bit = 0; bit < 33 * 8; ++bit) {
            guarded.packet[2 + bit / 8] ^= 1U << (bit % 8);
            assert(guarded.packet[35] != audio_crc8(guarded.packet + 2, 33));
            guarded.packet[2 + bit / 8] ^= 1U << (bit % 8);
        }
    }
}

static void test_processing(void)
{
    assert(audio_pcm24_from_i2s(0x7FFFFF00) == 8388607);
    assert(audio_pcm24_from_i2s(0x80000000) == -8388608);
    assert(audio_pcm24_from_i2s(0xFFFFFF00) == -1);
    assert(audio_pcm24_from_i2s(0x000000FF) == 0);
    audio_processor_t state = {.gain_q8 = AUDIO_GAIN_Q8};
    uint32_t stereo[32] = {0};
    int16_t pcm[16];
    for (unsigned i = 0; i < 16; ++i) stereo[2 * i + 1] = 0x80000000;
    audio_process_block(&state, stereo, pcm);
    assert(state.peak24 == 0 && state.gain_q8 == AUDIO_GAIN_Q8);
    for (unsigned i = 0; i < 16; ++i) assert(pcm[i] == 0);
    stereo[0] = 0x80000000;
    stereo[2] = 0x7FFFFF00;
    audio_process_block(&state, stereo, pcm);
    assert(state.peak24 == 8388608 && state.gain_q8 == 32);
    assert(pcm[0] == -4096 && pcm[1] == 4095);
    memset(stereo, 0, sizeof(stereo));
    audio_process_block(&state, stereo, pcm);
    assert(state.gain_q8 == 36);

    /* Compare against the original STM32 arithmetic for 100,000 blocks.
     * ESP32 words must be shifted first, unlike STM32 right-aligned SAI. */
    uint32_t reference_gain = state.gain_q8;
    for (unsigned block = 0; block < 100000; ++block) {
        uint32_t peak = 0;
        int32_t source[16];
        for (unsigned i = 0; i < 16; ++i) {
            uint32_t raw = random_word() >> (block % 24);
            source[i] = (int32_t)(raw & 0xFFFFFF);
            if (source[i] & 0x800000) source[i] -= 0x1000000;
            stereo[i * 2] = ((uint32_t)source[i] << 8) | 0xA5;
            stereo[i * 2 + 1] = random_word();
            uint32_t magnitude = source[i] < 0 ? (uint32_t)-source[i] : (uint32_t)source[i];
            if (magnitude > peak) peak = magnitude;
        }
        uint32_t target = AUDIO_GAIN_Q8;
        if (peak && ((LIMITER_PEAK_24 << 8) / peak < target)) target = (LIMITER_PEAK_24 << 8) / peak;
        if (target < reference_gain) reference_gain = target;
        else if (reference_gain < target) {
            reference_gain += LIMITER_RELEASE_STEP_Q8;
            if (reference_gain > target) reference_gain = target;
        }
        audio_process_block(&state, stereo, pcm);
        assert(state.peak24 == peak && state.gain_q8 == reference_gain);
        for (unsigned i = 0; i < 16; ++i) {
            int32_t expected = (int32_t)(((int64_t)source[i] * reference_gain) >> 16);
            assert(pcm[i] == expected);
            assert(pcm[i] >= -4096 && pcm[i] <= 4096);
        }
    }
    memset(stereo, 0, sizeof(stereo));
    for (unsigned i = 0; i < 5000; ++i) audio_process_block(&state, stereo, pcm);
    assert(state.gain_q8 == AUDIO_GAIN_Q8 && state.clips == 0);
}

int main(void)
{
    test_protocol();
    test_processing();
    puts("PASS: CRC-8, 36-byte framing, PCM LE, sequence wrap, corruption detection,");
    puts("      PCM24 alignment/sign, channel selection, limiter attack/release,");
    puts("      full-scale extremes, 100000 reference blocks (1600000 samples).");
    return 0;
}
