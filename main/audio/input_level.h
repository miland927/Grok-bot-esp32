#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_input_level_update(const int16_t* data, int count);
float audio_input_level(void);
void audio_output_level_update(const int16_t* data, int count);
float audio_output_level(void);

#ifdef __cplusplus
}
#endif

