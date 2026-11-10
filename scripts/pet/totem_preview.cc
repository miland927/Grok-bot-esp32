// Host renderer for the totem revival. It compiles the firmware's pure totem
// module and draws the same pose with a stand-in for PetDisplay's face.
//
//   c++ -std=c++20 -O2 -I main/display main/display/pet_totem.cc \
//       scripts/pet/totem_preview.cc -o local-only/totem-preview/totem-preview.exe
//   totem-preview.exe                       # timeline checks
//   totem-preview.exe frames.rgb 60         # 240x240 RGB24 frames
//   totem-preview.exe frames.rgb 20 aftermath

#include "pet_totem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
constexpr int kSize = 240;
constexpr float kR = 88.0f;

constexpr float kEyeSpacing = 0.34f;
constexpr float kEyeWidth = 0.135f;
constexpr float kEyeHeight = 0.165f;
constexpr float kGazeX = 0.34f;
constexpr float kGazeY = 0.10f;

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "totem check failed: %s\n", what);
        ++failures;
    }
}

bool Near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }

uint16_t Gray(float value) {
    const int v = std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
    return static_cast<uint16_t>(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
}

// Mirrors PetDisplay::Render for the totem pose: dark theme, 240px canvas,
// 0xF7BE face, black background and the radius/eye constants in PetFaceProfile.
void DrawFace(uint16_t* pixels, const PetTotem::Pose& pose) {
    std::fill_n(pixels, kSize * kSize, 0);
    if (pose.face_alpha <= 0.0f) {
        return;
    }
    const float zoom = pose.zoom;
    const float cx = kSize * 0.5f + pose.face_x * zoom + pose.shake_x;
    const float cy = kSize * 0.5f + pose.face_y * zoom + pose.shake_y;
    const float r = kR * pose.face_scale * zoom;
    const float squash = std::clamp(pose.face_squash, -0.48f, 0.48f);
    const float rx = r * (1.0f + squash);
    const float ry = r * (1.0f - squash);
    if (rx <= 0.5f || ry <= 0.5f) {
        return;
    }
    const float gx = pose.gaze_x * kGazeX * rx;
    const float gy = pose.gaze_y * kGazeY * ry;
    const float eye_dx = kEyeSpacing * rx;
    const float eye_rx = kEyeWidth * rx;
    const float eye_ry = kEyeHeight * ry * pose.eye_open;
    const float ex0 = cx - eye_dx + gx;
    const float ex1 = cx + eye_dx + gx;
    const float ey = cy - 0.04f * ry + gy;
    const bool eyes = eye_ry > 0.5f;
    const float brightness = pose.face_alpha * (1.0f - std::clamp(pose.dark, 0.0f, 1.0f));

    const int x0 = std::max(0, static_cast<int>(std::floor(cx - rx)) - 2);
    const int x1 = std::min(kSize - 1, static_cast<int>(std::ceil(cx + rx)) + 2);
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - ry)) - 2);
    const int y1 = std::min(kSize - 1, static_cast<int>(std::ceil(cy + ry)) + 2);
    const float inv_rx = 1.0f / rx;
    const float inv_ry = 1.0f / ry;
    const float inv_soft = rx / 1.6f;
    const float inv_erx = eyes ? 1.0f / eye_rx : 0.0f;
    const float inv_ery = eyes ? 1.0f / eye_ry : 0.0f;
    const float inv_ecover = eyes ? eye_rx / 1.2f : 0.0f;
    for (int y = y0; y <= y1; ++y) {
        const float dy = (y - cy) * inv_ry;
        const float dy2 = dy * dy;
        for (int x = x0; x <= x1; ++x) {
            const float dx = (x - cx) * inv_rx;
            const float d2 = dx * dx + dy2;
            if (d2 > 1.05f) {
                continue;
            }
            float a = 1.0f;
            if (d2 >= 0.92f) {
                a = (1.0f - std::sqrt(d2)) * inv_soft + 0.5f;
                a = std::clamp(a, 0.0f, 1.0f);
            }
            float eye = 0.0f;
            if (eyes) {
                const float ax0 = (x - ex0) * inv_erx;
                const float ay = (y - ey) * inv_ery;
                const float q = ax0 * ax0 + ay * ay;
                if (q < 0.86f) {
                    eye = 1.0f;
                } else if (q < 1.12f) {
                    eye = std::clamp((1.0f - std::sqrt(q)) * inv_ecover + 0.5f, 0.0f, 1.0f);
                }
                if (eye < 1.0f) {
                    const float ax1 = (x - ex1) * inv_erx;
                    const float q1 = ax1 * ax1 + ay * ay;
                    if (q1 < 0.86f) {
                        eye = 1.0f;
                    } else if (q1 < 1.12f) {
                        const float e = std::clamp((1.0f - std::sqrt(q1)) * inv_ecover + 0.5f,
                                                   0.0f, 1.0f);
                        eye = std::max(eye, e);
                    }
                }
            }
            const float value = a * (1.0f - eye) * brightness;
            if (value <= 0.0f) {
                continue;
            }
            pixels[y * kSize + x] = Gray(value);
        }
    }
}

void Trigger(PetTotem& totem, int64_t at, bool aftermath) { totem.Start(at, aftermath); }

void CheckTimeline() {
    constexpr int64_t start = 2000000;
    PetTotem totem;
    Trigger(totem, start, false);
    Check(totem.IsActive(), "active after start");
    constexpr PetTotem::Stage stages[] = {PetTotem::Stage::Death, PetTotem::Stage::Summon,
                                          PetTotem::Stage::Awaken, PetTotem::Stage::Burst,
                                          PetTotem::Stage::Revival};
    constexpr int64_t starts[] = {0, 400000, 1100000, 1800000, 2200000, 3500000};
    for (int i = 0; i < 5; ++i) {
        Check(totem.Update(start + starts[i], true) == stages[i], "stage start");
        Check(Near(totem.Progress(start + starts[i]), 0.0f), "stage progress zero");
        Check(totem.Update(start + starts[i + 1] - 1, true) == stages[i], "stage end");
    }
    Check(totem.Update(start + 3500000, true) == PetTotem::Stage::Idle, "idle after 3.5 s");
    Check(!totem.IsActive(), "inactive after 3.5 s");

    Trigger(totem, start, false);
    Check(totem.Update(start + 1000000, false) == PetTotem::Stage::Idle, "cancel when not idle");
    Check(!totem.IsActive(), "inactive after cancel");

    Trigger(totem, start, true);
    Check(totem.Aftermath(), "aftermath flag");
    Check(totem.Update(start + 1000, true) == PetTotem::Stage::Death, "aftermath starts on black");
    totem.Cancel();

    const auto death0 = PetTotem::Sample(PetTotem::Stage::Death, 0.0f, false);
    const auto death1 = PetTotem::Sample(PetTotem::Stage::Death, 1.0f, false);
    Check(death0.face_alpha == 1.0f, "standalone death starts on the face");
    Check(death1.eye_open == 0.0f, "eyes closed at death");
    Check(death1.dark > 0.80f, "death darkens");
    const auto dead_after = PetTotem::Sample(PetTotem::Stage::Death, 0.0f, true);
    Check(dead_after.face_alpha == 0.0f && dead_after.dark == 1.0f, "aftermath opens black");

    const auto summon0 = PetTotem::Sample(PetTotem::Stage::Summon, 0.0f, false);
    const auto summon1 = PetTotem::Sample(PetTotem::Stage::Summon, 1.0f, false);
    Check(summon0.totem_alpha == 0.0f, "totem invisible at summon start");
    Check(summon1.totem_alpha == 1.0f, "totem visible at summon end");
    Check(summon1.totem_scale > 7.30f, "totem near final size");
    Check(summon1.totem_scale < 7.60f, "summon settles");

    float last_zoom = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        const auto pose = PetTotem::Sample(PetTotem::Stage::Awaken, i / 20.0f, false);
        Check(pose.zoom >= last_zoom - 0.0001f, "awaken camera monotonic");
        last_zoom = pose.zoom;
    }
    Check(last_zoom > 1.12f, "awaken pushes in");

    const auto burst = PetTotem::Sample(PetTotem::Stage::Burst, 0.20f, false);
    Check(burst.flash > 0.80f, "burst flash");
    Check(burst.totem_alpha > 0.0f, "totem still visible in the flash");
    const auto burst_end = PetTotem::Sample(PetTotem::Stage::Burst, 1.0f, false);
    Check(burst_end.totem_alpha == 0.0f, "totem gone");
    Check(burst_end.ring_alpha == 0.0f, "ring gone");

    float max_scale = 0.0f;
    float min_gaze = 0.0f;
    float max_gaze = 0.0f;
    for (int i = 0; i <= 100; ++i) {
        const auto pose = PetTotem::Sample(PetTotem::Stage::Revival, i / 100.0f, false);
        max_scale = std::max(max_scale, pose.face_scale);
        min_gaze = std::min(min_gaze, pose.gaze_x);
        max_gaze = std::max(max_gaze, pose.gaze_x);
    }
    Check(max_scale > 1.08f, "revival jelly overshoot");
    Check(min_gaze < -0.30f && max_gaze > 0.30f, "revival looks left and right");
    const auto rev0 = PetTotem::Sample(PetTotem::Stage::Revival, 0.0f, false);
    const auto rev_eye = PetTotem::Sample(PetTotem::Stage::Revival, 0.62f, false);
    const auto rev_end = PetTotem::Sample(PetTotem::Stage::Revival, 1.0f, false);
    Check(rev0.face_alpha == 0.0f && rev0.eye_open == 0.0f, "revival starts dark and shut");
    Check(rev_eye.eye_open > 0.30f, "eyes snap open");
    Check(rev_end.face_alpha > 0.99f && Near(rev_end.face_scale, 1.0f, 0.01f), "revival settles");
    Check(Near(rev_end.gaze_x, 0.0f, 0.01f) && rev_end.eye_open > 0.95f, "revival idle face");
    Check(rev_end.zoom == 1.0f && rev_end.dark < 0.001f, "camera and light restored");

    Check(PetTotem::Bounds(PetTotem::Stage::Death, death0, kSize, kSize).x1 >
              PetTotem::Bounds(PetTotem::Stage::Death, death0, kSize, kSize).x2,
          "standalone death leaves the face bounds alone");
    const auto dead_bounds = PetTotem::Bounds(PetTotem::Stage::Death, dead_after, kSize, kSize);
    Check(dead_bounds.x1 == 0 && dead_bounds.x2 == kSize - 1, "aftermath clears the canvas");

    // Drawing smoke test: the summon frame must contain gold/green pixels.
    std::array<uint16_t, kSize * kSize> pixels{};
    const auto summon_pose = PetTotem::Sample(PetTotem::Stage::Summon, 0.8f, false);
    PetTotem::Draw(pixels.data(), kSize, kSize, PetTotem::Stage::Summon, 0.8f, summon_pose,
                   pet_totem::assets::kTexture);
    int lit = 0;
    int green = 0;
    for (const auto p : pixels) {
        if (p != 0) {
            ++lit;
        }
        if (((p >> 5) & 0x3F) > ((p >> 11) & 0x1F) + 4) {
            ++green;
        }
    }
    Check(lit > 1000, "totem draws");
    Check(green > 20, "totem keeps green accents");

    const auto burst_pose = PetTotem::Sample(PetTotem::Stage::Burst, 0.18f, false);
    std::array<uint16_t, kSize * kSize> flash_pixels{};
    PetTotem::Draw(flash_pixels.data(), kSize, kSize, PetTotem::Stage::Burst, 0.18f, burst_pose,
                   pet_totem::assets::kTexture);
    Check(flash_pixels[(kSize / 2) * kSize + kSize / 2] > 0xE000, "burst flash centre");
    Check(flash_pixels[0] == 0 && flash_pixels[kSize - 1] == 0, "burst flash stays round");

    const auto again = PetTotem::Sample(PetTotem::Stage::Revival, 0.37f, false);
    const auto twice = PetTotem::Sample(PetTotem::Stage::Revival, 0.37f, false);
    Check(std::memcmp(&again, &twice, sizeof(again)) == 0, "sampling is deterministic");
}

void DrawFrame(uint16_t* pixels, const PetTotem::Pose& pose, PetTotem::Stage stage, float progress) {
    DrawFace(pixels, pose);
    PetTotem::Draw(pixels, kSize, kSize, stage, progress, pose, pet_totem::assets::kTexture);
}
}  // namespace

int main(int argc, char** argv) {
    CheckTimeline();
    if (failures != 0) {
        std::fprintf(stderr, "%d totem timeline check(s) failed\n", failures);
        return 1;
    }
    if (argc == 1) {
        std::puts("totem timeline checks passed");
        return 0;
    }
    if (argc < 3) {
        return 2;
    }
    const int fps = std::atoi(argv[2]);
    if (fps != 20 && fps != 60) {
        return 2;
    }
    const bool aftermath = argc > 3 && std::strcmp(argv[3], "aftermath") == 0;
    std::ofstream output(argv[1], std::ios::binary);
    if (!output) {
        return 1;
    }
    std::array<uint16_t, kSize * kSize> pixels{};
    std::array<unsigned char, kSize * kSize * 3> rgb{};
    constexpr int64_t start = 2000000;
    PetTotem totem;
    Trigger(totem, start, aftermath);
    for (int frame = 0; frame < fps * 35 / 10; ++frame) {
        const int64_t now = start + frame * 1000000LL / fps;
        const auto stage = totem.Update(now, true);
        const float progress = totem.Progress(now);
        const auto pose = PetTotem::Sample(stage, progress, aftermath);
        DrawFrame(pixels.data(), pose, stage, progress);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const auto c = pixels[i];
            rgb[i * 3] = static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31);
            rgb[i * 3 + 1] = static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63);
            rgb[i * 3 + 2] = static_cast<unsigned char>((c & 31) * 255 / 31);
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    }
    return output ? 0 : 1;
}
