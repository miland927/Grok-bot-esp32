#pragma once

namespace pet_face {

// Soft-body feel for touch: a lightly damped squash/stretch spring gives one
// visible overshoot and a second, smaller reversal before settling. The hop
// spring stays as before so the fireworks return overshoot remains 10-16 px.
inline constexpr float kSquashStiffness = 160.0f;
inline constexpr float kSquashDamping = 8.5f;

// The eyes follow the face with a slight, clamped lag (secondary motion).
inline constexpr float kEyeLagStiffness = 45.0f;
inline constexpr float kEyeLagDamping = 9.5f;
inline constexpr float kEyeLagScale = 0.40f;
inline constexpr float kEyeLagClamp = 4.0f;

}  // namespace pet_face
