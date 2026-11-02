#include "input_level.h"

#include <math.h>

/* Smoothed microphone level in 0..1, shared with the display layer.
   Only reads the PCM that is already being processed; it never changes the
   audio path, so AEC and wake word behaviour stay untouched. */

static volatile float s_level = 0.0f;

void audio_input_level_update(const int16_t* data, int count) {
    if (!data || count <= 0) {
        return;
    }

    float acc = 0.0f;
    for (int i = 0; i < count; i++) {
        float v = data[i] / 32768.0f;
        acc += v * v;
    }
    float rms = sqrtf(acc / (float)count);

    float lvl = s_level;
    lvl = (rms > lvl) ? (lvl * 0.35f + rms * 0.65f) : (lvl * 0.92f + rms * 0.08f);
    if (lvl > 1.0f) {
        lvl = 1.0f;
    }
    s_level = lvl;
}

float audio_input_level(void) {
    return s_level;
}

static volatile float s_out_level = 0.0f;

void audio_output_level_update(const int16_t* data, int count) {
    if (!data || count <= 0) {
        return;
    }

    float acc = 0.0f;
    for (int i = 0; i < count; i++) {
        float v = data[i] / 32768.0f;
        acc += v * v;
    }
    float rms = sqrtf(acc / (float)count);

    /* speech envelope: quick to rise, quick to fall */
    float lvl = s_out_level;
    lvl = (rms > lvl) ? (lvl * 0.2f + rms * 0.8f) : (lvl * 0.8f + rms * 0.2f);
    if (lvl > 1.0f) {
        lvl = 1.0f;
    }
    s_out_level = lvl;
}

float audio_output_level(void) {
    return s_out_level;
}
