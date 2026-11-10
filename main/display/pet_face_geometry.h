#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pet_face {

// The face is drawn into a fixed 240x240 RGB565 canvas. The ellipse's
// anti-aliased edge extends to sqrt(1.05) * radius, so the radius limit must
// account for that before leaving a two-pixel background margin.
inline constexpr float kCanvasHeight = 240.0f;
inline constexpr float kEdgeMargin = 2.0f;
inline constexpr float kAntiAliasExtent = 1.025f;
inline constexpr float kSoftKnee = 6.0f;
inline constexpr float kMinRadius = 0.5f;

// Keep the visible vertical radius inside the canvas while preserving the
// actual face center. Above the limit the radius converges smoothly instead
// of clipping (the soft knee avoids a visible kink in the squash spring).
inline float ConstrainVerticalRadius(float center_y, float radius_y) {
    const float top_limit = (center_y - kEdgeMargin) / kAntiAliasExtent;
    const float bottom_limit = (kCanvasHeight - 1.0f - kEdgeMargin - center_y) / kAntiAliasExtent;
    const float limit = std::min(top_limit, bottom_limit);
    if (limit <= 0.0f || radius_y <= limit) {
        return limit <= 0.0f ? kMinRadius : radius_y;
    }
    const float over = radius_y - limit;
    const float constrained = limit - kSoftKnee * (1.0f - std::exp(-over / kSoftKnee));
    return std::max(kMinRadius, constrained);
}

}  // namespace pet_face
