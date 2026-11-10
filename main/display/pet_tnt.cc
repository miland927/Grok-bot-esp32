#include "pet_tnt.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "pet_tnt_texture.h"

namespace {
constexpr unsigned kTapCount = 5;
constexpr int64_t kTapGapUs = 500000;
constexpr int64_t kTapWindowUs = 1800000;
constexpr int64_t kStageEndsUs[] = {250000,  450000,  950000,  1500000, 2200000,
                                    2350000, 2550000, 3100000, 3500000};
constexpr float kChargeZoom[] = {1.0f, 1.15f, 1.37f, 1.70f};
constexpr float kCompression[] = {0.14f, 0.24f, 0.34f};
constexpr float kRebound[] = {0.05f, 0.10f, 0.14f};
constexpr float kShakePx[] = {0.7f, 2.0f, 4.5f};
constexpr float kChargeFlash[] = {0.20f, 0.37f, 0.58f};
constexpr float kBlastFlash = 0.98f;
constexpr float kTau = 6.2831853f;

float Smooth(float p) {
    p = std::clamp(p, 0.0f, 1.0f);
    return p * p * (3.0f - 2.0f * p);
}

float Pulse(float p, float start, float peak, float end) {
    return Smooth((p - start) / (peak - start)) * (1.0f - Smooth((p - peak) / (end - peak)));
}

constexpr uint16_t Rgb(int red, int green, int blue) {
    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3);
}

uint16_t Blend(uint16_t from, uint16_t to, int opacity) {
    const auto channel = [=](int shift, int mask) {
        const int a = (from >> shift) & mask;
        const int b = (to >> shift) & mask;
        return ((a * (64 - opacity) + b * opacity + 32) >> 6) << shift;
    };
    return channel(11, 31) | channel(5, 63) | channel(0, 31);
}

uint16_t Mix(uint16_t from, uint16_t to, float t) { return Blend(from, to, std::lround(t * 64)); }

struct Canvas {
    uint16_t* pixels;
    int width;
    int height;
    pet_fireworks::Rect bounds;

    void Rect(int x, int y, int w, int h, uint16_t color, float alpha = 1) const {
        const int left = std::max(bounds.x1, x);
        const int right = std::min(bounds.x2 + 1, x + w);
        const int opacity = std::lround(alpha * 64);
        if (left >= right || opacity == 0) {
            return;
        }
        for (int row = std::max(bounds.y1, y); row < std::min(bounds.y2 + 1, y + h); ++row) {
            auto* begin = pixels + row * width + left;
            auto* end = pixels + row * width + right;
            if (opacity == 64) {
                std::fill(begin, end, color);
            } else {
                for (auto* pixel = begin; pixel < end; ++pixel) {
                    *pixel = Blend(*pixel, color, opacity);
                }
            }
        }
    }
};

void DrawCrate(const Canvas& canvas, const PetTnt::Pose& pose, const uint16_t* texture) {
    constexpr int kTex = 16;
    const float size = 84.0f * pose.zoom;
    const float u = size * (1 + pose.squash * .48f) / kTex;
    const float v = size * (1 - pose.squash) / kTex;
    const int x = std::lround(canvas.width / 2 + pose.x - 8 * u);
    const int y = std::lround(canvas.height / 2 + pose.y - 8 * v + size * pose.squash * .35f);
    const auto tint = [&](uint16_t color) {
        return Mix(Mix(color, Rgb(75, 19, 29), pose.pressure * .23f), Rgb(255, 249, 222),
                   pose.flash);
    };
    for (int ty = 0; ty < kTex; ++ty) {
        const int py = std::lround(ty * v);
        for (int tx = 0; tx < kTex; ++tx) {
            const int px = std::lround(tx * u);
            canvas.Rect(x + px, y + py, std::lround((tx + 1) * u) - px,
                        std::lround((ty + 1) * v) - py,
                        tint(texture ? texture[ty * kTex + tx]
                                     : pet_tnt_texture::kSide[ty * kTex + tx]),
                        pose.crate_alpha);
        }
    }
}

void DrawBlast(const Canvas& canvas, float progress) {
    const int cx = canvas.width / 2;
    const int cy = canvas.height / 2 - 2;
    const float travel = 1.0f - (1.0f - progress) * (1.0f - progress);
    const float flash = kBlastFlash * (1.0f - Smooth((progress - .10f) / .32f));
    if (flash > 0) {
        for (int dy = -78; dy <= 78; dy += 4) {
            const int half = static_cast<int>(std::sqrt(78.0f * 78 - dy * dy));
            canvas.Rect(cx - half, cy + dy, 2 * half + 1, 4, Rgb(255, 250, 234), flash);
        }
    }
    const float ring = 24 + 83 * travel;
    for (int i = 0; i < 40; ++i) {
        const float angle = kTau * i / 40;
        const int x = cx + std::lround(std::cos(angle) * ring);
        const int y = cy + std::lround(std::sin(angle) * ring * .79f);
        canvas.Rect(x - 3, y - 2, 7, 5, Rgb(255, 223, 173), .9f * (1 - progress));
    }
}

void DrawDebris(const Canvas& canvas, float blast, float fall) {
    constexpr uint16_t colors[] = {Rgb(255, 235, 166), Rgb(255, 162, 63), Rgb(213, 64, 38)};
    const float travel = 1 - (1 - blast) * (1 - blast);
    for (int i = 0; i < 24; ++i) {
        const float angle = i * 2.3999632f;
        const float radius = 9.0f + (47 + (i * 19) % 52) * travel;
        const int x = canvas.width / 2 + static_cast<int>(std::cos(angle) * radius);
        const int y =
            canvas.height / 2 - 2 + static_cast<int>(std::sin(angle) * radius + 60 * fall * fall);
        const int size = 2 + static_cast<int>((9 + i % 6) * (1.0f - blast));
        canvas.Rect(x - size / 2, y - size / 2, size, size, colors[i % 3],
                    (1 - .55f * blast) * (1 - fall));
    }
}

void DrawSmoke(const Canvas& canvas, float progress, float alpha, float kick) {
    struct Puff {
        float x, y, spread_x, spread_y, radius, fade_start, fade_end;
        int gray;
    };
    // Uneven velocities and lifetimes break up the cloud as it expands.
    constexpr Puff puffs[] = {{-14, 6, -56, 8, 15, .28f, .88f, 146},
                              {13, 7, 59, 5, 13, .38f, .97f, 166},
                              {-23, -8, -63, -23, 10, .49f, 1, 179},
                              {23, -10, 60, -27, 11, .53f, 1, 156},
                              {-7, -13, -27, -36, 18, .19f, .67f, 185},
                              {9, -16, 29, -46, 14, .21f, .74f, 170},
                              {1, 4, 4, -22, 23, .24f, .72f, 207},
                              {-2, 13, -15, 7, 12, .20f, .62f, 151}};
    const float spread = 1 - (1 - progress) * (1 - progress);
    for (const auto& puff : puffs) {
        const float opacity = alpha * (1 - Smooth((progress - puff.fade_start) /
                                                  (puff.fade_end - puff.fade_start)));
        if (opacity < 1.0f / 64) {
            continue;
        }
        const int x = canvas.width / 2 + std::lround(puff.x + puff.spread_x * spread);
        const int y = canvas.height / 2 + std::lround(puff.y + puff.spread_y * spread + kick);
        const int unit = std::max(1L, std::lround(puff.radius * (.52f + .54f * spread) / 4));
        const int half_widths[] = {1, 3, 4, 4, 4, 3, 1};
        for (int row = 0; row < 7; ++row) {
            const int half = half_widths[row] * unit;
            const int shade = puff.gray - row * 6;
            canvas.Rect(x - half, y + (row - 3) * unit, 2 * half, unit,
                        Rgb(shade, shade, shade), opacity);
        }
    }
}
}  // namespace

bool PetTnt::Tap(int64_t now) {
    if (IsActive()) {
        return false;
    }
    if (taps_ == 0 || now - last_tap_ >= kTapGapUs || now - first_tap_ > kTapWindowUs) {
        first_tap_ = now;
        taps_ = 0;
    }
    last_tap_ = now;
    if (++taps_ < kTapCount) {
        return false;
    }
    CancelTaps();
    ++show_id_;
    start_ = now;
    stage_ = Stage::Agitated;
    return true;
}

PetTnt::Stage PetTnt::Update(int64_t now, bool idle) {
    if (!idle) {
        CancelTaps();
    }
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

float PetTnt::Progress(int64_t now) const {
    if (!IsActive()) {
        return 1.0f;
    }
    const auto index = static_cast<unsigned>(stage_) - 1;
    const auto begin = index == 0 ? 0 : kStageEndsUs[index - 1];
    return std::clamp(static_cast<float>(now - start_ - begin) / (kStageEndsUs[index] - begin),
                      0.0f, 1.0f);
}

PetTnt::Pose PetTnt::Sample(Stage stage, float progress) {
    const float p = std::clamp(progress, 0.0f, 1.0f);
    Pose pose;
    switch (stage) {
        case Stage::Idle:
            pose.face_alpha = 1;
            break;
        case Stage::Agitated:
            pose.face_alpha = 1;
            pose.eye_height = 1 - .51f * Smooth(p / .65f);
            pose.face_scale = 1 - .08f * Smooth(p);
            pose.x = 2.6f * Pulse(p, .18f, .35f, .78f) - 1.8f * Pulse(p, .52f, .70f, 1);
            pose.squash = .06f * Smooth(p);
            break;
        case Stage::Morph:
            pose.face_alpha = 1 - Smooth((p - .12f) / .75f);
            pose.face_scale = .92f - .27f * Smooth(p);
            pose.eye_height = .49f;
            pose.squash = .06f - .26f * Pulse(p, 0, .36f, .95f);
            pose.crate_alpha = Smooth((p - .20f) / .75f);
            pose.zoom = .79f + .21f * Smooth(p);
            pose.flash = .56f * Pulse(p, .20f, .51f, .93f);
            break;
        case Stage::Charge1:
        case Stage::Charge2:
        case Stage::Charge3: {
            const int beat = static_cast<int>(stage) - static_cast<int>(Stage::Charge1);
            pose.crate_alpha = 1;
            const float push = Smooth(p);
            pose.zoom = kChargeZoom[beat] + (kChargeZoom[beat + 1] - kChargeZoom[beat]) * push;
            pose.pressure = (beat + push) / 3;
            const float compress_at = .34f - beat * .055f;
            pose.squash = kCompression[beat] * Pulse(p, 0, compress_at, .53f) -
                          kRebound[beat] * Pulse(p, .40f, .60f - beat * .035f, .88f);
            pose.flash = kChargeFlash[beat] * Pulse(p, .40f, .56f, .77f);
            const float shake = kShakePx[beat] * Pulse(p, .31f, .73f, 1);
            const float angle = kTau * (1.25f * p + (1 + beat * 1.6f) * p * p);
            pose.x = shake * std::sin(angle);
            pose.y = shake * .54f * std::sin(angle * 1.73f);
            if (beat == 2) {
                pose.flash += .22f * Pulse(p, .69f, .80f, .94f);
            }
            break;
        }
        case Stage::Hold:
            pose.crate_alpha = 1;
            pose.zoom = kChargeZoom[3];
            pose.squash = .115f * Smooth(p / .52f);
            pose.pressure = 1;
            break;
        case Stage::Recover:
            pose.face_alpha = Smooth(p / .47f);
            pose.face_scale = .65f + .35f * Smooth(p / .72f) + .045f * Pulse(p, .32f, .65f, 1);
            pose.eye_height = .58f + .42f * Smooth((p - .25f) / .75f);
            pose.y = 18 * (1 - Smooth(p / .63f)) - 7 * Pulse(p, .30f, .63f, 1);
            break;
        default:
            break;
    }
    return pose;
}

pet_fireworks::Rect PetTnt::Bounds(Stage stage, const Pose& pose, int width, int height) {
    if (stage == Stage::Idle || stage == Stage::Agitated) {
        return {};
    }
    const int cx = width / 2;
    const int cy = height / 2;
    if (stage == Stage::Blast || stage == Stage::Smoke || stage == Stage::Recover) {
        return pet_fireworks::Clip({cx - 111, cy - 111, cx + 111, cy + 111}, width, height);
    }
    const float size = 84 * pose.zoom;
    const float half_x = size * (1 + pose.squash * .48f) / 2;
    const float half_y = size * (1 - pose.squash) / 2;
    const float x = cx + pose.x;
    const float y = cy + pose.y + size * pose.squash * .35f;
    return pet_fireworks::Clip(
        {static_cast<int>(std::floor(x - half_x)) - 1,
         static_cast<int>(std::floor(y - half_y)) - 1,
         static_cast<int>(std::ceil(x + half_x)) + 1, static_cast<int>(std::ceil(y + half_y)) + 1},
        width, height);
}

void PetTnt::Draw(uint16_t* pixels, int width, int height, Stage stage, float p, const Pose& pose,
                  const uint16_t* texture) {
    const Canvas canvas{pixels, width, height, Bounds(stage, pose, width, height)};
    if (pose.crate_alpha > 0) {
        DrawCrate(canvas, pose, texture);
    } else if (stage == Stage::Blast) {
        DrawSmoke(canvas, .19f * p, .72f * Smooth(p / .67f), 0);
        DrawBlast(canvas, p);
        DrawDebris(canvas, p, 0);
    } else if (stage == Stage::Smoke) {
        DrawSmoke(canvas, .19f + .61f * p, .72f - .33f * Smooth(p), 7 * Pulse(p, 0, .15f, .62f));
        DrawDebris(canvas, 1, p);
    } else if (stage == Stage::Recover) {
        DrawSmoke(canvas, .80f + .20f * p, .39f * (1 - Smooth(p)), 0);
    }
}
