#pragma once

#include <array>
#include <cstdint>

namespace pet_fireworks {

// Pure, host-testable port of the approved v2 tracking-shot preview. All times
// are absolute show milliseconds: zero when the fireworks soundtrack starts.
// The 1 ms flight curves are integrated once in Prepare(); every query is a
// fixed-table lookup plus interpolation, so a display tick never re-integrates.
class Camera {
public:
    struct View {
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float zoom = 1.0f;
    };
    struct Point {
        float x = 0.0f;
        float y = 0.0f;
    };

    static constexpr int kSamples = 1031;
    static constexpr float kLaunchMs = 370.0f;
    static constexpr float kBrakeMs = 960.0f;
    static constexpr float kLockMs = 1020.0f;
    static constexpr float kBurstMs = 1400.0f;
    static constexpr float kShowMs = 3500.0f;
    static constexpr float kBurstExpansionMs = 500.0f;

    static constexpr float kOriginX = 79.0f;
    static constexpr float kOriginY = 115.0f;
    static constexpr float kBurstX = 85.0f;
    static constexpr float kBurstY = 54.0f;
    static constexpr float kRise = 61.0f;

    void Prepare(int width, int height);
    bool ready() const { return ready_; }

    // Lensed screen placement of the 160x160 source frame.
    View LensAt(float ms) const;
    Point RocketAt(float ms) const;    // source coordinates
    float ScaleAt(float ms) const;     // sprite/exhaust scale multiplier
    float OffsetAt(float ms) const;    // screen-space camera beat offset, px
    float ProgressAt(float ms) const;  // re-timed flight progress, 0..1
    float CameraAt(float ms) const;    // lagging camera height, source px
    float PanStrengthAt(float ms) const;

    static Point Project(float x, float y, const View& view);

    // Burst keeps the lens fixed and expands around the frozen source point.
    Point BurstAnchor() const;
    View BurstView(float zoom) const;
    float BurstStartZoom() const { return LensAt(kBurstMs).zoom; }
    float BurstZoomAt(float ms, float effect_limit, float material_limit) const;

private:
    float SampleAltitude(float ms) const;
    float SampleCamera(float ms) const;

    std::array<float, kSamples> altitude_{};
    std::array<float, kSamples> camera_{};
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;
};

}  // namespace pet_fireworks
