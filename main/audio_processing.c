#include "audio_processing.h"

int32_t audio_pcm24_from_i2s(uint32_t word)
{
    uint32_t value = word >> 8;
    /* Explicit sign extension without relying on signed right shift. */
    return (value & 0x800000U) ? (int32_t)value - 0x1000000 : (int32_t)value;
}

void audio_process_block(audio_processor_t *state,
                         const uint32_t stereo[AUDIO_SAMPLES_PER_PACKET * 2U],
                         int16_t pcm[AUDIO_SAMPLES_PER_PACKET])
{
    uint32_t peak = 0;
    for (unsigned i = 0; i < AUDIO_SAMPLES_PER_PACKET; ++i) {
        int32_t sample = audio_pcm24_from_i2s(stereo[i * 2U]);
        uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
        if (magnitude > peak) peak = magnitude;
    }
    state->peak24 = peak;
    uint32_t target = AUDIO_GAIN_Q8;
    if (peak) {
        uint32_t safe = (uint32_t)(((uint64_t)LIMITER_PEAK_24 * 256U) / peak);
        if (safe < target) target = safe;
    }
    if (target < state->gain_q8) state->gain_q8 = target;
    else if (state->gain_q8 < target) {
        state->gain_q8 += LIMITER_RELEASE_STEP_Q8;
        if (state->gain_q8 > target) state->gain_q8 = target;
    }
    if (state->gain_q8 < AUDIO_GAIN_Q8) ++state->limiter_blocks;
    for (unsigned i = 0; i < AUDIO_SAMPLES_PER_PACKET; ++i) {
        int64_t scaled = (int64_t)audio_pcm24_from_i2s(stereo[i * 2U]) * state->gain_q8;
        /* Floor division matches the STM32 arithmetic shift, including negatives. */
        int32_t value = scaled >= 0 ? (int32_t)(scaled / 65536) :
                                     (int32_t)-((-scaled + 65535) / 65536);
        if (value > 32767) { value = 32767; ++state->clips; }
        else if (value < -32768) { value = -32768; ++state->clips; }
        pcm[i] = (int16_t)(value * (int32_t)VOLUME_PERCENT / 100);
    }
}
