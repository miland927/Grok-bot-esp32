#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

struct PetFaceProfile {
    static constexpr size_t kMaxBytes = 2048;

    uint16_t color_dark = 0xF7BE;
    uint16_t color_light = 0x1082;
    float eye_width = 0.135f;
    float eye_height = 0.165f;
    float eye_spacing = 0.34f;
    uint32_t blink_duration_us = 140000;
    uint32_t blink_min_us = 1500000;
    uint32_t blink_max_us = 5000000;
    float gaze_x = 0.34f;
    float gaze_y = 0.10f;
    float angry_eye_scale = 0.55f;
    float angry_shake_px = 0.0f;
    float angry_shake_hz = 12.0f;

    // The caller supplies a NUL terminator after the view. Failure leaves out intact.
    static bool Parse(std::string_view json, PetFaceProfile& out);
};
