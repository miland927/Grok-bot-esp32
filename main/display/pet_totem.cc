#include "pet_totem.h"

#include "pet_totem_particles.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace {
using Stage = PetTotem::Stage;

constexpr int64_t kStageEndsUs[] = {400000, 1100000, 1800000, 2200000, 3500000};
constexpr float kStageStartS[] = {0.0f, 0.4f, 1.1f, 1.8f, 2.2f};
constexpr float kStageDurS[] = {0.4f, 0.7f, 0.7f, 0.4f, 1.3f};
constexpr float kBaseScale = 7.4f;
constexpr float kTau = 6.2831853f;
constexpr float kPi = 3.14159265f;

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float Smooth(float p) {
    p = Clamp01(p);
    return p * p * (3.0f - 2.0f * p);
}

float Pulse(float p, float start, float peak, float end) {
    return Smooth((p - start) / (peak - start)) * (1.0f - Smooth((p - peak) / (end - peak)));
}

// Unit step response of a damped spring. Starts at x0 at t=0 and settles on
// target; zeta < 1 gives the overshoot the revival beat needs.
float Spring(float t, float x0, float target, float omega, float zeta) {
    if (t <= 0.0f) {
        return x0;
    }
    const float wd = omega * std::sqrt(1.0f - zeta * zeta);
    const float decay = std::exp(-zeta * omega * t);
    return target +
           (x0 - target) * decay * (std::cos(wd * t) + (zeta * omega / wd) * std::sin(wd * t));
}

constexpr uint16_t Rgb(int red, int green, int blue) {
    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3);
}

uint16_t Blend565(uint16_t from, uint16_t to, int opacity) {
    const auto channel = [=](int shift, int mask) {
        const int a = (from >> shift) & mask;
        const int b = (to >> shift) & mask;
        return ((a * (64 - opacity) + b * opacity + 32) >> 6) << shift;
    };
    return channel(11, 31) | channel(5, 63) | channel(0, 31);
}

uint16_t Mix(uint16_t from, uint16_t to, float t) {
    return Blend565(from, to, std::lround(Clamp01(t) * 64.0f));
}

struct Canvas {
    uint16_t* pixels;
    int width;
    int height;
    pet_fireworks::Rect bounds;

    void Blend(int x, int y, uint16_t color, float alpha) const {
        if (x < bounds.x1 || x > bounds.x2 || y < bounds.y1 || y > bounds.y2 || alpha <= 0.0f) {
            return;
        }
        int opacity = std::lround(alpha * 64.0f);
        if (opacity <= 0) {
            return;
        }
        if (opacity > 64) {
            opacity = 64;
        }
        uint16_t& pixel = pixels[y * width + x];
        pixel = opacity == 64 ? color : Blend565(pixel, color, opacity);
    }

    void Rect(int x, int y, int w, int h, uint16_t color, float alpha = 1.0f) const {
        const int left = std::max(bounds.x1, x);
        const int right = std::min(bounds.x2 + 1, x + w);
        const int top = std::max(bounds.y1, y);
        const int bottom = std::min(bounds.y2 + 1, y + h);
        if (left >= right || top >= bottom || alpha <= 0.0f) {
            return;
        }
        int opacity = std::lround(alpha * 64.0f);
        if (opacity <= 0) {
            return;
        }
        if (opacity > 64) {
            opacity = 64;
        }
        for (int row = top; row < bottom; ++row) {
            auto* begin = pixels + row * width + left;
            auto* end = pixels + row * width + right;
            if (opacity == 64) {
                std::fill(begin, end, color);
            } else {
                for (auto* pixel = begin; pixel < end; ++pixel) {
                    *pixel = Blend565(*pixel, color, opacity);
                }
            }
        }
    }
};

void DrawTotem(const Canvas& canvas, const PetTotem::Pose& pose,
               const pet_totem::Texture& texture) {
    if (pose.totem_alpha <= 0.0f || pose.totem_scale <= 0.0f || texture.width <= 0 ||
        texture.height <= 0) {
        return;
    }
    const float scale = pose.totem_scale * pose.zoom;
    if (scale <= 0.0f) {
        return;
    }
    const float cx = canvas.width * 0.5f + pose.shake_x;
    const float cy = canvas.height * 0.5f + pose.totem_y * pose.zoom + pose.shake_y;

    // Soft gold-to-green halo behind the sprite.
    const float halo_radius = 9.2f * scale;
    const int halo = static_cast<int>(std::ceil(halo_radius));
    const int hx0 = std::max(canvas.bounds.x1, static_cast<int>(cx) - halo);
    const int hx1 = std::min(canvas.bounds.x2, static_cast<int>(cx) + halo);
    const int hy0 = std::max(canvas.bounds.y1, static_cast<int>(cy) - halo);
    const int hy1 = std::min(canvas.bounds.y2, static_cast<int>(cy) + halo);
    const float inv = 1.0f / (halo_radius * halo_radius);
    for (int y = hy0; y <= hy1; ++y) {
        for (int x = hx0; x <= hx1; ++x) {
            const float dx = x - cx;
            const float dy = y - cy;
            const float d2 = (dx * dx + dy * dy) * inv;
            if (d2 >= 1.0f) {
                continue;
            }
            const float falloff = 1.0f - d2;
            const float alpha = falloff * falloff * 0.30f * pose.totem_glow;
            canvas.Blend(x, y, Mix(Rgb(255, 215, 94), Rgb(87, 217, 106), d2 * 0.9f), alpha);
        }
    }

    const float c = std::cos(pose.totem_rot);
    const float s = std::sin(pose.totem_rot);
    const float half_w = texture.width * 0.5f;
    const float half_h = texture.height * 0.5f;
    const float extent_x = (std::fabs(c) * texture.width + std::fabs(s) * texture.height) * 0.5f;
    const float extent_y = (std::fabs(s) * texture.width + std::fabs(c) * texture.height) * 0.5f;
    const int x0 = std::max(canvas.bounds.x1, static_cast<int>(std::floor(cx - extent_x * scale)));
    const int x1 = std::min(canvas.bounds.x2, static_cast<int>(std::ceil(cx + extent_x * scale)));
    const int y0 = std::max(canvas.bounds.y1, static_cast<int>(std::floor(cy - extent_y * scale)));
    const int y1 = std::min(canvas.bounds.y2, static_cast<int>(std::ceil(cy + extent_y * scale)));
    const float inv_scale = 1.0f / scale;
    const uint16_t tint = Rgb(255, 239, 191);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = (x - cx) * inv_scale;
            const float dy = (y - cy) * inv_scale;
            const float sx = c * dx + s * dy + half_w;
            const float sy = -s * dx + c * dy + half_h;
            if (sx < 0.0f || sy < 0.0f || sx >= texture.width || sy >= texture.height) {
                continue;
            }
            const int ix = static_cast<int>(sx);
            const int iy = static_cast<int>(sy);
            const int bit = ix & 7;
            if ((texture.mask[iy * texture.mask_stride + (ix >> 3)] & (0x80 >> bit)) == 0) {
                continue;
            }
            const uint16_t color =
                Mix(texture.pixels[iy * texture.width + ix], tint, pose.totem_glow * 0.18f);
            canvas.Blend(x, y, color, pose.totem_alpha);
        }
    }

    // The green eyes are the only saturated part of the sprite, so a small
    // emissive dot sells the "the totem is awake" beat without redrawing it.
    if (pose.totem_glow > 0.30f) {
        constexpr float kEyeU[] = {6.0f, 10.0f};
        constexpr float kEyeV = 5.5f;
        const float eye_alpha = 0.25f + 0.55f * pose.totem_glow;
        for (float u : kEyeU) {
            const float ux = (u / 16.0f * texture.width - half_w) * scale;
            const float uy = (kEyeV / 16.0f * texture.height - half_h) * scale;
            const int ex = std::lround(cx + c * ux - s * uy);
            const int ey = std::lround(cy + s * ux + c * uy);
            canvas.Rect(ex - 2, ey - 2, 5, 5, Rgb(156, 247, 168), eye_alpha);
        }
    }
}

void DrawOrbit(const Canvas& canvas, const PetTotem::Pose& pose) {
    const float t = pose.time;
    if (t < 0.44f || t >= 1.87f) {
        return;
    }
    const float cx = canvas.width * 0.5f + pose.shake_x;
    const float cy = canvas.height * 0.5f + pose.shake_y;
    const float energy = Clamp01((t - 1.10f) / 0.70f);
    const float contract = Smooth((t - 1.80f) / 0.06f);
    for (const auto& p : pet_totem::particles::kOrbit) {
        const float birth = 0.46f + p.spawn;
        if (t < birth) {
            continue;
        }
        const float age = t - birth;
        const float fade = Smooth(age / 0.16f);
        const float theta = p.angle + p.omega * age * (1.0f + 0.85f * energy);
        const float radius = p.radius * (1.0f - 0.05f * energy) * (1.0f - contract);
        const int x = std::lround(cx + std::cos(theta) * radius);
        const int y = std::lround(cy + std::sin(theta) * radius * 0.88f -
                                  p.lift * age * (0.35f + 0.65f * energy) * (1.0f - contract));
        const float alpha = 0.70f * fade * (1.0f - 0.55f * contract);
        const int size =
            std::max(1, static_cast<int>(std::lround(p.size * (1.0f - 0.4f * contract))));
        canvas.Rect(x - size / 2, y - size / 2, size, size, p.color, alpha);
    }
}

void DrawBurst(const Canvas& canvas, const PetTotem::Pose& pose) {
    const float t = pose.time;
    if (t < 1.84f) {
        return;
    }
    const float cx = canvas.width * 0.5f + pose.shake_x;
    const float cy = canvas.height * 0.5f + pose.shake_y;
    if (pose.ring_alpha > 0.0f) {
        const float radius = 20.0f + 122.0f * pose.ring;
        for (int pass = -1; pass <= 1; ++pass) {
            const float r = radius + pass * 4.0f;
            const float alpha = pose.ring_alpha * (pass == 0 ? 0.9f : 0.35f);
            for (int i = 0; i < 128; ++i) {
                const float angle = kTau * i / 128.0f;
                const int x = std::lround(cx + std::cos(angle) * r);
                const int y = std::lround(cy + std::sin(angle) * r * 0.94f);
                const uint16_t color = (i & 1) != 0 ? Rgb(255, 215, 94) : Rgb(87, 217, 106);
                canvas.Rect(x - 3, y - 3, 6, 6, color, alpha);
            }
        }
    }
    if (t < 1.86f) {
        return;
    }
    for (const auto& p : pet_totem::particles::kBurst) {
        const float age = t - 1.86f - p.delay;
        if (age <= 0.0f || age >= p.life) {
            continue;
        }
        const float radius = p.speed * age * (1.0f - p.drag * age / p.life);
        const int x = std::lround(cx + std::cos(p.angle) * radius);
        const int y = std::lround(cy + std::sin(p.angle) * radius * 0.88f - 10.0f * age);
        const float alpha = 0.95f * std::pow(1.0f - age / p.life, 1.2f);
        const int size =
            std::max(1, static_cast<int>(std::lround(p.size * (1.0f - 0.45f * age / p.life))));
        canvas.Rect(x - size / 2, y - size / 2, size, size, p.color, alpha);
    }
}

void DrawEmbers(const Canvas& canvas, const PetTotem::Pose& pose) {
    const float t = pose.time;
    if (t < 2.20f) {
        return;
    }
    const float cx = canvas.width * 0.5f + pose.shake_x;
    const float cy = canvas.height * 0.5f + pose.shake_y;
    for (const auto& e : pet_totem::particles::kEmber) {
        const float age = t - (2.20f + e.delay);
        if (age <= 0.0f || age >= e.life) {
            continue;
        }
        const int x = std::lround(cx + std::cos(e.angle) * e.radius);
        const int y = std::lround(cy + std::sin(e.angle) * e.radius * 0.95f - 16.0f * age);
        const float alpha = 0.55f * std::sin(kPi * age / e.life);
        const int size = std::max(1, static_cast<int>(std::lround(e.size)));
        canvas.Rect(x - size / 2, y - size / 2, size, size, e.color, alpha);
    }
}
}  // namespace

void PetTotem::Start(int64_t now, bool aftermath) {
    ++show_id_;
    start_ = now;
    stage_ = Stage::Death;
    aftermath_ = aftermath;
}

void PetTotem::Cancel() {
    start_ = -1;
    stage_ = Stage::Idle;
}

PetTotem::Stage PetTotem::Update(int64_t now, bool idle) {
    if (!IsActive()) {
        return Stage::Idle;
    }
    const int64_t age = now - start_;
    if (!idle || age >= kStageEndsUs[std::size(kStageEndsUs) - 1]) {
        start_ = -1;
        stage_ = Stage::Idle;
        return stage_;
    }
    const auto end = std::upper_bound(std::begin(kStageEndsUs), std::end(kStageEndsUs), age);
    stage_ = static_cast<Stage>(1 + (end - std::begin(kStageEndsUs)));
    return stage_;
}

float PetTotem::Progress(int64_t now) const {
    if (!IsActive()) {
        return 1.0f;
    }
    const auto index = static_cast<unsigned>(stage_) - 1;
    const auto begin = index == 0 ? 0 : kStageEndsUs[index - 1];
    return std::clamp(static_cast<float>(now - start_ - begin) / (kStageEndsUs[index] - begin),
                      0.0f, 1.0f);
}

PetTotem::Pose PetTotem::Sample(Stage stage, float progress, bool aftermath) {
    const float p = Clamp01(progress);
    Pose pose;
    pose.aftermath = aftermath;
    const int index = static_cast<int>(stage) - 1;
    if (index < 0 || index >= static_cast<int>(std::size(kStageStartS))) {
        return pose;
    }
    pose.time = kStageStartS[index] + p * kStageDurS[index];

    switch (stage) {
        case Stage::Idle:
            break;
        case Stage::Death:
            pose.eye_open = 0.0f;
            if (aftermath) {
                pose.face_alpha = 0.0f;
                pose.dark = 1.0f;
            } else {
                pose.face_alpha = 1.0f;
                pose.face_scale = 1.0f - 0.035f * Smooth(p);
                pose.face_squash = 0.12f * Smooth(p / 0.55f);
                pose.face_y = 10.0f * Smooth(p / 0.50f);
                pose.eye_open = 1.0f - Smooth(p / 0.28f);
                pose.dark = 0.90f * Smooth((p - 0.02f) / 0.55f);
            }
            break;
        case Stage::Summon:
            pose.face_alpha = aftermath ? 0.0f : 1.0f - Smooth(p / 0.12f);
            pose.face_scale = 0.965f;
            pose.face_squash = 0.12f;
            pose.face_y = 10.0f;
            pose.eye_open = 0.0f;
            pose.dark = aftermath ? 1.0f : 0.90f - 0.06f * Smooth(p);
            pose.totem_scale = Spring(p * kStageDurS[1], 0.9f, kBaseScale, 24.0f, 0.55f);
            pose.totem_alpha = Smooth(p / 0.10f);
            pose.totem_rot = -0.22f + 0.24f * Smooth(p / 0.65f) +
                             0.05f * std::sin(p * 9.0f) * std::exp(-3.2f * p);
            pose.totem_y = 10.0f * (1.0f - Smooth(p / 0.45f));
            pose.totem_glow = 0.15f + 0.35f * Smooth(p);
            pose.flash = 0.18f * Pulse(p, 0.0f, 0.05f, 0.18f);
            break;
        case Stage::Awaken: {
            const float t = p * kStageDurS[2];
            pose.face_alpha = 0.0f;
            pose.dark = 0.74f;
            pose.totem_scale = kBaseScale + 0.20f * Smooth(p);
            pose.totem_alpha = 1.0f;
            pose.totem_rot = 0.025f * std::sin(t * 5.0f + 0.6f);
            pose.totem_y = -3.5f * std::sin(t * 5.0f);
            pose.totem_glow = 0.50f + 0.50f * Smooth(p);
            pose.zoom = 1.0f + 0.13f * Smooth(p);
            break;
        }
        case Stage::Burst:
            pose.face_alpha = 0.0f;
            pose.dark = 0.72f;
            pose.totem_scale = (kBaseScale + 0.15f) - 0.18f * Smooth(p / 0.15f) +
                               2.90f * Smooth((p - 0.15f) / 0.28f);
            pose.totem_alpha = 1.0f - Smooth((p - 0.16f) / 0.20f);
            pose.totem_rot = 0.03f - 0.09f * Smooth((p - 0.15f) / 0.55f);
            pose.totem_glow = 1.0f;
            pose.zoom = 1.16f - 0.16f * Smooth((p - 0.15f) / 0.45f);
            pose.flash = p < 0.16f ? 0.0f : 0.98f * (1.0f - Smooth((p - 0.25f) / 0.18f));
            pose.ring = Smooth((p - 0.17f) / 0.72f);
            pose.ring_alpha = p < 0.17f ? 0.0f : std::pow(1.0f - pose.ring, 1.3f);
            {
                const float amp = 6.0f * (1.0f - Smooth((p - 0.15f) / 0.30f));
                pose.shake_x = amp * std::sin(p * 95.0f);
                pose.shake_y = 0.65f * amp * std::sin(p * 117.0f + 1.3f);
            }
            break;
        case Stage::Revival: {
            const float t = p * kStageDurS[4];
            const float spring_t = std::max(0.0f, t - 0.06f);
            pose.face_alpha = Smooth(p / 0.10f);
            pose.face_scale = Spring(spring_t, 0.38f, 1.0f, 15.5f, 0.34f);
            pose.face_squash = Spring(spring_t, 0.36f, 0.0f, 14.0f, 0.42f);
            pose.face_y = Spring(spring_t, 14.0f, 0.0f, 14.0f, 0.55f);
            const float eye_t = (p - 0.44f) * kStageDurS[4];
            pose.eye_open = eye_t > 0.0f ? Spring(eye_t, 0.0f, 1.0f, 38.0f, 0.50f) : 0.0f;
            pose.eye_open = std::clamp(pose.eye_open, 0.0f, 1.4f);
            pose.gaze_x = -0.55f * Pulse(p, 0.52f, 0.555f, 0.61f) +
                          0.52f * Pulse(p, 0.61f, 0.645f, 0.69f) -
                          0.12f * Pulse(p, 0.72f, 0.74f, 0.78f);
            pose.gaze_y = -0.10f * Pulse(p, 0.44f, 0.49f, 0.57f);
            pose.face_y -= 4.0f * Pulse(p, 0.44f, 0.49f, 0.57f);
            pose.dark = 0.72f * (1.0f - Smooth(p / 0.16f));
            break;
        }
        default:
            break;
    }
    return pose;
}

pet_fireworks::Rect PetTotem::Bounds(Stage stage, const Pose& pose, int width, int height) {
    if (stage == Stage::Idle) {
        return {};
    }
    if (stage == Stage::Death && !pose.aftermath) {
        return {};
    }
    return pet_fireworks::Clip({0, 0, width - 1, height - 1}, width, height);
}

void PetTotem::Draw(uint16_t* pixels, int width, int height, Stage stage, float /*progress*/,
                    const Pose& pose, const pet_totem::Texture& texture) {
    if (stage == Stage::Idle || pixels == nullptr) {
        return;
    }
    Canvas canvas{pixels, width, height, Bounds(stage, pose, width, height)};
    if (pet_fireworks::Empty(canvas.bounds)) {
        return;
    }
    if (pose.totem_alpha > 0.0f && pose.totem_scale > 0.0f) {
        DrawTotem(canvas, pose, texture);
    }
    if (pose.time >= 0.44f && pose.time < 1.87f) {
        DrawOrbit(canvas, pose);
    }
    if (pose.time >= 1.84f) {
        DrawBurst(canvas, pose);
    }
    if (pose.time >= 2.20f) {
        DrawEmbers(canvas, pose);
    }
    if (pose.flash > 0.0f) {
        const float fx = width * 0.5f;
        const float fy = height * 0.5f;
        const float inv = 1.0f / (118.0f * 118.0f);
        for (int y = canvas.bounds.y1; y <= canvas.bounds.y2; ++y) {
            for (int x = canvas.bounds.x1; x <= canvas.bounds.x2; ++x) {
                const float dx = x - fx;
                const float dy = y - fy;
                const float d2 = (dx * dx + dy * dy) * inv;
                if (d2 >= 1.0f) {
                    continue;
                }
                const float edge = 1.0f - d2;
                const float alpha = pose.flash * edge;
                canvas.Blend(x, y, Mix(Rgb(255, 246, 220), Rgb(156, 239, 168), d2 * 0.55f), alpha);
            }
        }
    }
}
