#include "pet_fireworks_camera.h"

#include <algorithm>
#include <cmath>

namespace pet_fireworks {
namespace {

constexpr float kSpeedMarks[] = {0.0f, 0.16f, 0.36f, 0.58f, 0.86f, 1.0f};
constexpr float kSpeeds[] = {8.0f, 190.0f, 32.0f, 37.0f, 86.0f, 10.0f};
constexpr int kSpeedSegments = 5;
// Screen-space offsets on top of the tracked path, with unequally spaced beats.
constexpr float kCameraBeats[] = {470.0f, 603.0f, 748.0f, 827.0f, 912.0f, 960.0f};
constexpr float kCameraOffsets[] = {0.0f, -12.0f, 6.5f, -3.4f, 3.8f, 0.0f};
constexpr int kCameraBeatCount = 6;
constexpr float kHalfPi = 1.57079632679489661923f;

float Clamp01(float value) { return std::min(1.0f, std::max(0.0f, value)); }

float Mix(float a, float b, float t) { return a + (b - a) * t; }

float Smooth(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

float Travel(float altitude) {
    // The source generator's path is (79 + 6p, 115 - 61p^1.43).
    return std::pow(std::max(0.0f, altitude) / Camera::kRise, 1.0f / 1.43f);
}

float SpeedAt(float t) {
    // Equivalent to bisect_right(SPEED_MARKS, t) clamped to the last segment.
    int index = 1;
    while (index < kSpeedSegments && t >= kSpeedMarks[index]) {
        ++index;
    }
    const float start = kSpeedMarks[index - 1];
    const float end = kSpeedMarks[index];
    return Mix(kSpeeds[index - 1], kSpeeds[index], Smooth((t - start) / (end - start)));
}

float FlightTime(float ms) {
    // The rocket carries its momentum through the stop, then reaches the apex.
    const float u = Clamp01((ms - Camera::kLockMs) / (Camera::kBurstMs - Camera::kLockMs));
    return ms + (Camera::kBurstMs - Camera::kLockMs) * 0.85f * u * u * (1.0f - u);
}

float LensTime(float ms) {
    // Brake in 60 ms with continuous velocity, then keep the whole lens fixed.
    const float u = Clamp01((ms - Camera::kBrakeMs) / (Camera::kLockMs - Camera::kBrakeMs));
    return std::min(ms, Camera::kBrakeMs) +
           (Camera::kLockMs - Camera::kBrakeMs) * (u - u * u / 2.0f);
}

}  // namespace

void Camera::Prepare(int width, int height) {
    if (ready_ && width == width_ && height == height_) {
        return;
    }
    width_ = width;
    height_ = height;

    // Integrate the re-timed rise at the approved 1 ms step, then normalize.
    altitude_[0] = 0.0f;
    for (int i = 1; i < kSamples; ++i) {
        const float t = (static_cast<float>(i) - 0.5f) / (kBurstMs - kLaunchMs);
        altitude_[i] = altitude_[i - 1] + SpeedAt(t);
    }
    const float scale = kRise / altitude_[kSamples - 1];
    for (float& value : altitude_) {
        value *= scale;
    }

    // Lightly underdamped camera spring, one step per source millisecond.
    camera_[0] = 0.0f;
    float velocity = 0.0f;
    for (int i = 1; i < kSamples; ++i) {
        velocity += (126.0f * (altitude_[i] - camera_[i - 1]) - 11.0f * velocity) / 1000.0f;
        camera_[i] = camera_[i - 1] + velocity / 1000.0f;
    }
    ready_ = true;
}

float Camera::SampleAltitude(float ms) const {
    const float index = std::clamp(ms - kLaunchMs, 0.0f, static_cast<float>(kSamples - 1));
    const int lower = static_cast<int>(index);
    const float fraction = index - static_cast<float>(lower);
    const int upper = std::min(lower + 1, kSamples - 1);
    return Mix(altitude_[lower], altitude_[upper], fraction);
}

float Camera::SampleCamera(float ms) const {
    const float index = std::clamp(ms - kLaunchMs, 0.0f, static_cast<float>(kSamples - 1));
    const int lower = static_cast<int>(index);
    const float fraction = index - static_cast<float>(lower);
    const int upper = std::min(lower + 1, kSamples - 1);
    return Mix(camera_[lower], camera_[upper], fraction);
}

Camera::View Camera::LensAt(float ms) const {
    const float time = LensTime(ms);
    const float focus = SampleCamera(time);
    const float progress = Travel(SampleAltitude(time));
    // DrawFrame's zoom and center, tilted upward during the pursuit.
    const float zoom =
        4.0f - 1.12f * std::sin(Clamp01(time / kLaunchMs) * kHalfPi) - 0.75f * progress;
    const float center_y =
        height_ * 0.5f + 30.0f * (1.0f - progress) - 29.0f * Smooth((time - kLaunchMs) / 490.0f);
    const float origin_x = width_ * 0.5f - Mix(kOriginX, kBurstX, Travel(focus)) * zoom;
    const float origin_y = center_y - (kOriginY - focus) * zoom + OffsetAt(ms);
    return {origin_x, origin_y, zoom};
}

Camera::Point Camera::RocketAt(float ms) const {
    const float altitude = SampleAltitude(FlightTime(ms));
    return {Mix(kOriginX, kBurstX, Travel(altitude)), kOriginY - altitude};
}

float Camera::ScaleAt(float ms) const {
    const float progress = Travel(SampleAltitude(FlightTime(ms)));
    const float depth = Mix(1.0f, 0.79f, Smooth((ms - kLockMs) / (kBurstMs - kLockMs)));
    return (1.2f + 0.45f * std::sin(Clamp01(ms / kLaunchMs) * kHalfPi)) *
           (1.0f - 0.75f * progress) * depth;
}

float Camera::OffsetAt(float ms) const {
    if (ms <= kCameraBeats[0] || ms >= kCameraBeats[kCameraBeatCount - 1]) {
        return 0.0f;
    }
    int index = 1;
    while (index < kCameraBeatCount - 1 && ms >= kCameraBeats[index]) {
        ++index;
    }
    const float start = kCameraBeats[index - 1];
    const float end = kCameraBeats[index];
    return Mix(kCameraOffsets[index - 1], kCameraOffsets[index],
               Smooth((ms - start) / (end - start)));
}

float Camera::ProgressAt(float ms) const { return Travel(SampleAltitude(FlightTime(ms))); }

float Camera::CameraAt(float ms) const { return SampleCamera(LensTime(ms)); }

float Camera::PanStrengthAt(float ms) const {
    const float pan = (CameraAt(ms) - CameraAt(ms - 4.0f)) / 0.004f;
    return Clamp01(pan / 116.0f) * (1.0f - Smooth((ms - kBrakeMs) / (kLockMs - kBrakeMs)));
}

Camera::Point Camera::Project(float x, float y, const View& view) {
    return {view.origin_x + x * view.zoom, view.origin_y + y * view.zoom};
}

Camera::Point Camera::BurstAnchor() const { return Project(kBurstX, kBurstY, LensAt(kBurstMs)); }

Camera::View Camera::BurstView(float zoom) const {
    const Point anchor = BurstAnchor();
    return {anchor.x - kBurstX * zoom, anchor.y - kBurstY * zoom, zoom};
}

float Camera::BurstZoomAt(float ms, float effect_limit, float material_limit) const {
    const float end = material_limit > 0.0f ? std::min(effect_limit, material_limit) : effect_limit;
    const float u = Clamp01((ms - kBurstMs) / kBurstExpansionMs);
    return Mix(BurstStartZoom(), end, Smooth(u));
}

}  // namespace pet_fireworks
