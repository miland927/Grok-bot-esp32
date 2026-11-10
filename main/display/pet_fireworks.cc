#include "pet_fireworks.h"

#include "application.h"
#include "pet_fireworks_assets.h"
#include "pet_media.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <string_view>

extern const uint8_t fallback_start[] asm("_binary_grok_fireworks_fallback_gra_start");
extern const uint8_t fallback_end[] asm("_binary_grok_fireworks_fallback_gra_end");

namespace {

using pet_fireworks::Clip;
using pet_fireworks::Empty;
using pet_fireworks::Rect;
using pet_fireworks::Union;
namespace assets = pet_fireworks::assets;

constexpr char kTag[] = "PetFireworks";
constexpr unsigned kEffects = PetMedia::kFireworkEffects.size();
constexpr int kSourceSize = PetMedia::kWidth;
constexpr int64_t kSdWaitUs = 450000;
constexpr int64_t kFrameUs = PetMedia::kFrameMs * 1000;
// Compact silhouettes need more magnification.
constexpr float kBurstZoom[kEffects] = {4.7f, 4.25f, 3.9f, 3.9f, 3.3938f, 3.9f, 3.3938f, 4.05f};
constexpr int64_t kLeadInUs = 130000;
constexpr int64_t kBurstUs = 1400000;
constexpr int64_t kShowUs = kFrameUs * PetMedia::kFrameCount;
constexpr int64_t kReturnUs = 400000;
// The face's scale/position entrance finishes 160 ms before the stage ends;
// the remaining time is used by the upward return bounce.
constexpr int64_t kReturnEntranceUs = 240000;
constexpr float kStreakMinStrength = 0.015f;
constexpr float kExhaustOnsetMs = 250.0f;
constexpr float kExhaustFadeMs = 1220.0f;
std::atomic<bool> audio_running{false};

constexpr uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

constexpr uint16_t kExhaust[4] = {Rgb565(0xff, 0xc4, 0x52), Rgb565(0xfa, 0x79, 0x31),
                                  Rgb565(0xbc, 0x38, 0x24), Rgb565(0x5d, 0x1b, 0x20)};

bool CanPlay() {
    auto state = Application::GetInstance().GetDeviceState();
    return state == kDeviceStateIdle || state == kDeviceStateStarting ||
           state == kDeviceStateWifiConfiguring;
}

const PetMedia::Clip* BuiltInFirework() {
    static PetMedia::Clip clip;
    static bool ready = false;
    if (!ready) {
        const size_t size =
            reinterpret_cast<uintptr_t>(fallback_end) - reinterpret_cast<uintptr_t>(fallback_start);
        ready = clip.Validate({fallback_start, size});
    }
    return ready ? &clip : nullptr;
}

struct FireworkSound {
    std::string_view track;
    std::shared_ptr<PetMedia::Clip> owner;
};

void StartAudio(std::string_view track, std::shared_ptr<PetMedia::Clip> owner) {
    if (audio_running.exchange(true)) {
        return;
    }
    auto* sound = new (std::nothrow) FireworkSound{track, std::move(owner)};
    if (sound &&
        xTaskCreate(
            [](void* context) {
                {
                    std::unique_ptr<FireworkSound> sound(static_cast<FireworkSound*>(context));
                    auto& audio = Application::GetInstance().GetAudioService();
                    if (CanPlay() && audio.IsPlaybackIdle()) {
                        audio.PlaySound(sound->track, CanPlay);
                    }
                }
                audio_running.store(false);
                vTaskDelete(nullptr);
            },
            "pet_fireworks", 4096, sound, 2, nullptr) == pdPASS) {
        return;
    }
    delete sound;
    audio_running.store(false);
    ESP_LOGW(kTag, "Could not start sound task");
}

float Clamp01(float value) { return std::min(1.0f, std::max(0.0f, value)); }

float SmoothStep(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

int RoundToInt(float value) { return static_cast<int>(std::lround(value)); }

Rect MakeRect(float left, float top, float right, float bottom) {
    if (right < left) {
        std::swap(left, right);
    }
    if (bottom < top) {
        std::swap(top, bottom);
    }
    return {RoundToInt(left), RoundToInt(top), RoundToInt(right), RoundToInt(bottom)};
}

void ClearRect(uint16_t* pixels, int stride, const Rect& rect) {
    if (Empty(rect)) {
        return;
    }
    const size_t bytes = static_cast<size_t>(rect.x2 - rect.x1 + 1) * sizeof(uint16_t);
    for (int y = rect.y1; y <= rect.y2; ++y) {
        std::memset(pixels + y * stride + rect.x1, 0, bytes);
    }
}

void FillSpan(uint16_t* pixels, int stride, int y, int x1, int x2, uint16_t color) {
    std::fill(pixels + y * stride + x1, pixels + y * stride + x2 + 1, color);
}

void FillTriangle(uint16_t* pixels, int stride, int width, int height, float ax, float ay, float bx,
                  float by, float cx, float cy, uint16_t color) {
    const int min_x = std::max(0, RoundToInt(std::min({ax, bx, cx})));
    const int max_x = std::min(width - 1, RoundToInt(std::max({ax, bx, cx})));
    const int min_y = std::max(0, RoundToInt(std::min({ay, by, cy})));
    const int max_y = std::min(height - 1, RoundToInt(std::max({ay, by, cy})));
    if (min_x > max_x || min_y > max_y) {
        return;
    }
    const float xs[3] = {ax, bx, cx};
    const float ys[3] = {ay, by, cy};
    for (int y = min_y; y <= max_y; ++y) {
        const float scan = static_cast<float>(y) + 0.5f;
        float crossings[3];
        int count = 0;
        for (int edge = 0; edge < 3; ++edge) {
            const float x1 = xs[edge];
            const float y1 = ys[edge];
            const float x2 = xs[(edge + 1) % 3];
            const float y2 = ys[(edge + 1) % 3];
            if ((y1 <= scan && y2 > scan) || (y2 <= scan && y1 > scan)) {
                crossings[count++] = x1 + (scan - y1) * (x2 - x1) / (y2 - y1);
            }
        }
        if (count < 2) {
            continue;
        }
        // At most three crossings: a tiny insertion sort avoids the GCC 15
        // array-bounds false positive emitted for std::sort on a short array.
        for (int i = 1; i < count; ++i) {
            const float value = crossings[i];
            int j = i - 1;
            while (j >= 0 && crossings[j] > value) {
                crossings[j + 1] = crossings[j];
                --j;
            }
            crossings[j + 1] = value;
        }
        for (int i = 0; i + 1 < count; i += 2) {
            const int x1 = std::max(min_x, RoundToInt(crossings[i]));
            const int x2 = std::min(max_x, RoundToInt(crossings[i + 1]));
            if (x1 <= x2) {
                FillSpan(pixels, stride, y, x1, x2, color);
            }
        }
    }
}

void DrawThickLine(uint16_t* pixels, int stride, int width, int height, float x1, float y1,
                   float x2, float y2, int thickness, uint16_t color) {
    const int steps = std::max(1, RoundToInt(std::fabs(y2 - y1)));
    const int half = std::max(0, thickness / 2);
    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const int x = RoundToInt(x1 + (x2 - x1) * t);
        const int y = RoundToInt(y1 + (y2 - y1) * t);
        if (y < 0 || y >= height) {
            continue;
        }
        const int left = std::max(0, x - half);
        const int right = std::min(width - 1, x + half);
        if (left <= right) {
            FillSpan(pixels, stride, y, left, right, color);
        }
    }
}

void DrawStreak(uint16_t* pixels, int stride, int width, int height, const assets::Streak& streak,
                float strength, float camera, float offset) {
    float y = std::fmod(streak.start + (camera * 8.0f + offset) * streak.depth,
                        static_cast<float>(height + 100));
    if (y < 0.0f) {
        y += static_cast<float>(height + 100);
    }
    y -= 50.0f;
    const float length = (12.0f + streak.depth * 46.0f) * strength;
    const int x = RoundToInt(streak.x);
    if (x < 0 || x >= width) {
        return;
    }
    for (int part = 0; part < 3; ++part) {
        const float top = y - length * (3.0f - part) / 3.0f;
        const float bottom = y - length * (2.0f - part) / 3.0f;
        const int light = RoundToInt(98.0f * streak.depth * strength * (part + 1) / 3.0f);
        const int blue = std::min(255, RoundToInt(light * 1.15f));
        const uint16_t color =
            Rgb565(static_cast<uint8_t>(std::min(255, light)),
                   static_cast<uint8_t>(std::min(255, light)), static_cast<uint8_t>(blue));
        const int y0 = std::max(0, RoundToInt(top));
        const int y1 = std::min(height - 1, RoundToInt(bottom));
        for (int py = y0; py <= y1; ++py) {
            pixels[py * stride + x] = color;
        }
    }
}

void DrawRocket(uint16_t* pixels, int stride, int width, int height, int left, int top,
                int sprite_width, int sprite_height) {
    static_assert(assets::kRocketWidth == 8 && assets::kRocketHeight == 13);
    for (int dy = 0; dy < sprite_height; ++dy) {
        const int y = top + dy;
        if (y < 0 || y >= height) {
            continue;
        }
        const int sy =
            std::min(assets::kRocketHeight - 1,
                     static_cast<int>((dy + 0.5f) * assets::kRocketHeight / sprite_height));
        const uint8_t mask = assets::kRocketMask[sy];
        for (int dx = 0; dx < sprite_width; ++dx) {
            const int sx =
                std::min(assets::kRocketWidth - 1,
                         static_cast<int>((dx + 0.5f) * assets::kRocketWidth / sprite_width));
            if ((mask & (1u << sx)) == 0) {
                continue;
            }
            const int x = left + dx;
            if (x >= 0 && x < width) {
                pixels[y * stride + x] = assets::kRocketPixels[sy][sx];
            }
        }
    }
}

}  // namespace

PetFireworks::~PetFireworks() { Finish(); }

bool PetFireworks::Start(lv_obj_t* face, int64_t now) {
    auto& audio = Application::GetInstance().GetAudioService();
    if (IsActive() || !CanPlay() || audio_running.load() || !audio.IsPlaybackIdle()) {
        return false;
    }

    frame_ = static_cast<uint16_t*>(heap_caps_malloc(
        PetMedia::kWidth * PetMedia::kHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (!frame_) {
        ESP_LOGW(kTag, "Could not allocate fireworks frame");
        return false;
    }

    auto* screen = lv_obj_get_screen(face);
    auto* display = lv_obj_get_display(screen);
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    const auto stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    pixels_ = static_cast<uint16_t*>(heap_caps_calloc(height, stride, MALLOC_CAP_SPIRAM));
    if (!pixels_) {
        ESP_LOGW(kTag, "Could not allocate fireworks canvas");
        Finish();
        return false;
    }

    canvas_ = lv_canvas_create(screen);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_set_buffer(canvas_, pixels_, width, height, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas_);
    camera_.Prepare(width, height);
    previous_ = {};
    for (auto& streak : streaks_) {
        streak = {};
    }

    face_ = face;
    sd_request_ = PetMedia::LoadFirework(esp_random() % 100);
    stage_ = Stage::LeadIn;
    stage_start_ = now;
    next_frame_ = 0;
    return true;
}

PetFireworks::Stage PetFireworks::Update(int64_t now) {
    if (!IsActive()) {
        return stage_;
    }
    if (!CanPlay()) {
        Finish();
        return stage_;
    }

    auto age = now - stage_start_;
    if (stage_ == Stage::LeadIn && age >= kLeadInUs) {
        unsigned selected = PetMedia::kCreeper;
        if (sd_request_) {
            if (sd_request_->done.load(std::memory_order_acquire)) {
                selected = sd_request_->effect;
                sd_clip_ = std::move(sd_request_->clip);
            } else if (age < kSdWaitUs) {
                return stage_;
            } else {
                ESP_LOGW(kTag, "SD load deadline reached; using built-in firework");
            }
            sd_request_.reset();
        }
        playback_ = sd_clip_ ? sd_clip_.get() : BuiltInFirework();
        if (!playback_) {
            ESP_LOGE(kTag, "Invalid built-in fireworks clip");
            Finish();
            return stage_;
        }
        effect_ = sd_clip_ ? selected : PetMedia::kCreeper;
        stage_ = Stage::Flight;
        stage_start_ = now;
        age = 0;
        lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
        StartAudio(playback_->Audio(), sd_clip_);
        ESP_LOGI(kTag, "Firework %u started (%s) [%s]", static_cast<unsigned>(effect_ + 1),
                 sd_clip_ ? "microSD" : "built-in", PetMedia::kFireworkNames[effect_]);
    }

    if (stage_ == Stage::Flight) {
        if (age >= kBurstUs) {
            ClearStreaks();
            if (age >= kShowUs) {
                lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
                stage_ = Stage::Return;
                stage_start_ = now;
            } else {
                const size_t index = age / kFrameUs;
                playback_->DecodeFrame(index, frame_);
                next_frame_ = (index + 1) * kFrameUs;
                stage_ = Stage::Burst;
                DrawBurst(age);
            }
        } else {
            DrawFlight(age);
        }
    }

    if (stage_ == Stage::Burst) {
        if (age < kShowUs && age >= next_frame_) {
            const size_t index = age / kFrameUs;
            playback_->DecodeFrame(index, frame_);
            next_frame_ = (index + 1) * kFrameUs;
            DrawBurst(age);
        }
        if (age >= kShowUs) {
            lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
            stage_ = Stage::Return;
            stage_start_ = now;
        }
    }

    if (stage_ == Stage::Return && now - stage_start_ >= kReturnUs) {
        Finish();
    }
    return stage_;
}

float PetFireworks::ReturnProgress(int64_t now) const {
    return stage_ == Stage::Return
               ? std::clamp((now - stage_start_) / float(kReturnEntranceUs), 0.0f, 1.0f)
               : 1.0f;
}

void PetFireworks::DrawFlight(int64_t age) {
    static_assert(kStreakBudget <= assets::kStreakCount);
    auto* draw = lv_canvas_get_draw_buf(canvas_);
    const int width = draw->header.w;
    const int height = draw->header.h;
    const int stride = draw->header.stride / sizeof(uint16_t);
    const float ms = age * 0.001f;

    const pet_fireworks::Camera::View view = camera_.LensAt(ms);
    const pet_fireworks::Camera::Point rocket = camera_.RocketAt(ms);
    const pet_fireworks::Camera::Point screen =
        pet_fireworks::Camera::Project(rocket.x, rocket.y, view);
    const float scale = camera_.ScaleAt(ms);
    const float progress = camera_.ProgressAt(ms);
    const float offset = camera_.OffsetAt(ms);
    const float camera = camera_.CameraAt(ms);
    const float streak_strength = camera_.PanStrengthAt(ms);

    // Approximate sprite footprint. The preview pastes the cropped 8x13 PNG at
    // (x - w*0.31, y - h*0.52) after nearest-neighbour scaling.
    const int sprite_height = std::max(1, RoundToInt(37.0f * scale * view.zoom));
    const int sprite_width =
        std::max(1, RoundToInt(sprite_height * static_cast<float>(assets::kRocketWidth) /
                               static_cast<float>(assets::kRocketHeight)));
    const int sprite_left = RoundToInt(screen.x - sprite_width * 0.31f);
    const int sprite_top = RoundToInt(screen.y - sprite_height * 0.52f);
    const Rect sprite_rect = Clip(
        {sprite_left, sprite_top, sprite_left + sprite_width - 1, sprite_top + sprite_height - 1},
        width, height);

    // Exhaust fades in after ignition and out before the burst.
    const float exhaust_strength =
        SmoothStep((ms - kExhaustOnsetMs) / 205.0f) *
        (1.0f - SmoothStep((ms - kExhaustFadeMs) / (kBurstUs * 0.001f - kExhaustFadeMs)));
    Rect exhaust_rect{};
    Rect trail_rect{};
    float nozzle_x = 0.0f;
    float nozzle_y = 0.0f;
    float flame = 0.0f;
    const float scale_zoom = scale * view.zoom;
    if (exhaust_strength > 0.0f) {
        nozzle_x = screen.x - 3.0f * scale_zoom;
        nozzle_y = screen.y + 10.0f * scale_zoom;
        flame = (9.0f + 3.0f * std::sin(ms / 23.0f)) * scale_zoom * exhaust_strength;
        const float ax = nozzle_x - 2.1f * scale_zoom;
        const float bx = nozzle_x + 2.0f * scale_zoom;
        const float tx = nozzle_x - flame * 0.36f;
        exhaust_rect = Clip(MakeRect(std::min({ax, bx, tx}) - 1.0f, nozzle_y - 1.0f,
                                     std::max({ax, bx, tx}) + 1.0f, nozzle_y + flame + 1.0f),
                            width, height);
        for (int j = 12; j >= 1; --j) {
            const float prior = ms - j * 19.0f;
            if (prior < kExhaustOnsetMs) {
                continue;
            }
            const pet_fireworks::Camera::Point predecessor = camera_.RocketAt(prior);
            const float trail_scale = camera_.ScaleAt(prior);
            const float trail_x =
                predecessor.x + std::sin(j * 1.6f + ms * 0.027f) * j * 0.42f - 3.0f * trail_scale;
            const float trail_y = predecessor.y + 12.0f * trail_scale + j * (1.0f + progress);
            const pet_fireworks::Camera::Point point =
                pet_fireworks::Camera::Project(trail_x, trail_y, view);
            const float radius =
                std::max(0.5f, view.zoom * scale * (2.6f - 0.20f * j) * exhaust_strength);
            trail_rect = Union(trail_rect, MakeRect(point.x - radius, point.y - radius,
                                                    point.x + radius, point.y + radius));
        }
        trail_rect = Clip(trail_rect, width, height);
    }

    // Speed lines: a seeded table on top of the camera pan. The preview uses 43
    // lines; this firmware budgets one invalidation area per line and keeps 28.
    Rect streak_current[kStreakBudget];
    Rect streak_dirty[kStreakBudget];
    const bool streak_on = streak_strength >= kStreakMinStrength;
    for (int i = 0; i < kStreakBudget; ++i) {
        streak_current[i] = {};
        if (streak_on) {
            const assets::Streak& streak = assets::kStreaks[i];
            float y = std::fmod(streak.start + (camera * 8.0f + offset) * streak.depth,
                                static_cast<float>(height + 100));
            if (y < 0.0f) {
                y += static_cast<float>(height + 100);
            }
            y -= 50.0f;
            const float length = (12.0f + streak.depth * 46.0f) * streak_strength;
            streak_current[i] =
                Clip(MakeRect(streak.x - 1.0f, y - length - 1.0f, streak.x + 1.0f, y + 1.0f), width,
                     height);
        }
        streak_dirty[i] = Union(streaks_[i], streak_current[i]);
    }

    const Rect cluster = Union(Union(sprite_rect, exhaust_rect), trail_rect);
    const Rect cluster_dirty = Union(previous_, cluster);

    ClearRect(pixels_, stride, cluster_dirty);
    for (const Rect& rect : streak_dirty) {
        ClearRect(pixels_, stride, rect);
    }

    if (streak_on) {
        for (int i = 0; i < kStreakBudget; ++i) {
            DrawStreak(pixels_, stride, width, height, assets::kStreaks[i], streak_strength, camera,
                       offset);
        }
    }
    if (exhaust_strength > 0.0f) {
        FillTriangle(pixels_, stride, width, height, nozzle_x - 2.1f * scale_zoom, nozzle_y,
                     nozzle_x + 2.0f * scale_zoom, nozzle_y + 1.0f, nozzle_x - flame * 0.36f,
                     nozzle_y + flame, kExhaust[1]);
        DrawThickLine(pixels_, stride, width, height, nozzle_x, nozzle_y, nozzle_x - flame * 0.16f,
                      nozzle_y + flame * 0.55f, std::max(1, RoundToInt(1.3f * scale_zoom)),
                      kExhaust[0]);
        for (int j = 12; j >= 1; --j) {
            const float prior = ms - j * 19.0f;
            if (prior < kExhaustOnsetMs) {
                continue;
            }
            const pet_fireworks::Camera::Point predecessor = camera_.RocketAt(prior);
            const float trail_scale = camera_.ScaleAt(prior);
            const float trail_x =
                predecessor.x + std::sin(j * 1.6f + ms * 0.027f) * j * 0.42f - 3.0f * trail_scale;
            const float trail_y = predecessor.y + 12.0f * trail_scale + j * (1.0f + progress);
            const pet_fireworks::Camera::Point point =
                pet_fireworks::Camera::Project(trail_x, trail_y, view);
            const float radius =
                std::max(0.5f, view.zoom * scale * (2.6f - 0.20f * j) * exhaust_strength);
            const Rect particle = Clip(
                MakeRect(point.x - radius, point.y - radius, point.x + radius, point.y + radius),
                width, height);
            if (!Empty(particle)) {
                for (int y = particle.y1; y <= particle.y2; ++y) {
                    FillSpan(pixels_, stride, y, particle.x1, particle.x2,
                             kExhaust[std::min(3, j / 3)]);
                }
            }
        }
    }
    DrawRocket(pixels_, stride, width, height, sprite_left, sprite_top, sprite_width,
               sprite_height);

    previous_ = cluster;
    for (int i = 0; i < kStreakBudget; ++i) {
        streaks_[i] = streak_current[i];
    }
    Invalidate(cluster_dirty);
    for (const Rect& rect : streak_dirty) {
        Invalidate(rect);
    }
}

void PetFireworks::DrawBurst(int64_t age) {
    auto* draw = lv_canvas_get_draw_buf(canvas_);
    const int width = draw->header.w;
    const int height = draw->header.h;
    const int stride = draw->header.stride / sizeof(uint16_t);
    const float ms = age * 0.001f;
    const float zoom = camera_.BurstZoomAt(ms, kBurstZoom[effect_], assets::kBurstFitZoom[effect_]);
    const pet_fireworks::Camera::View view = camera_.BurstView(zoom);
    const int origin_x = RoundToInt(view.origin_x * 256.0f);
    const int origin_y = RoundToInt(view.origin_y * 256.0f);
    const int step = RoundToInt(view.zoom * 256.0f);

    // The GIF has a transparent or black background. Find the colored footprint so
    // a close-up does not force a full panel transfer on every frame.
    lv_area_t source = {kSourceSize, kSourceSize, -1, -1};
    for (int32_t y = 0; y < kSourceSize; ++y) {
        const uint16_t* row = frame_ + y * kSourceSize;
        for (int32_t x = 0; x < kSourceSize; ++x) {
            if (row[x] != 0) {
                source.x1 = std::min(source.x1, x);
                source.x2 = std::max(source.x2, x);
                source.y1 = std::min(source.y1, y);
                source.y2 = y;
            }
        }
    }
    const auto edge = [step](int origin, int source_pixel) -> int {
        return (origin + source_pixel * step) >> 8;
    };
    Rect current{};
    if (source.x1 <= source.x2) {
        current = {std::max(0, edge(origin_x, source.x1)), std::max(0, edge(origin_y, source.y1)),
                   std::min(width - 1, edge(origin_x, source.x2 + 1) - 1),
                   std::min(height - 1, edge(origin_y, source.y2 + 1) - 1)};
    }
    const Rect dirty = Union(previous_, current);
    if (Empty(dirty)) {
        previous_ = current;
        return;
    }
    ClearRect(pixels_, stride, dirty);

    if (!Empty(current)) {
        for (int sy = source.y1; sy <= source.y2; ++sy) {
            const int top = std::max(current.y1, edge(origin_y, sy));
            const int bottom = std::min(current.y2 + 1, edge(origin_y, sy + 1));
            if (top >= bottom) {
                continue;
            }
            auto* first = pixels_ + top * stride;
            const uint16_t* src = frame_ + sy * kSourceSize + source.x1;
            for (int sx = source.x1; sx <= source.x2; ++sx, ++src) {
                const int left = std::max(current.x1, edge(origin_x, sx));
                const int right = std::min(current.x2 + 1, edge(origin_x, sx + 1));
                if (left < right && *src != 0) {
                    std::fill(first + left, first + right, *src);
                }
            }
            const size_t bytes = (current.x2 - current.x1 + 1) * sizeof(uint16_t);
            for (int y = top + 1; y < bottom; ++y) {
                std::memcpy(pixels_ + y * stride + current.x1, first + current.x1, bytes);
            }
        }
    }
    previous_ = current;
    Invalidate(dirty);
}

void PetFireworks::ClearStreaks() {
    if (!canvas_) {
        return;
    }
    auto* draw = lv_canvas_get_draw_buf(canvas_);
    const int width = draw->header.w;
    const int height = draw->header.h;
    const int stride = draw->header.stride / sizeof(uint16_t);
    for (Rect& rect : streaks_) {
        if (Empty(rect)) {
            continue;
        }
        const Rect clipped = Clip(rect, width, height);
        if (!Empty(clipped)) {
            ClearRect(pixels_, stride, clipped);
            Invalidate(clipped);
        }
        rect = {};
    }
}

void PetFireworks::Invalidate(const Rect& rect) {
    if (Empty(rect) || !canvas_) {
        return;
    }
    lv_area_t area = {rect.x1, rect.y1, rect.x2, rect.y2};
    lv_area_t coords;
    lv_obj_get_coords(canvas_, &coords);
    lv_area_move(&area, coords.x1, coords.y1);
    lv_obj_invalidate_area(canvas_, &area);
}

void PetFireworks::Finish() {
    sd_request_.reset();
    playback_ = nullptr;
    sd_clip_.reset();
    if (canvas_) {
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
    }
    heap_caps_free(pixels_);
    pixels_ = nullptr;
    heap_caps_free(frame_);
    frame_ = nullptr;
    previous_ = {};
    for (auto& streak : streaks_) {
        streak = {};
    }
    if (face_) {
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_HIDDEN);
        face_ = nullptr;
    }
    stage_ = Stage::Idle;
}
