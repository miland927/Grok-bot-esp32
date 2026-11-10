#include "pet_display.h"
#ifdef GROK_CONTROLS
#include "pet_controls.h"
#include <utility>
#endif
#ifdef GROK_FIREWORKS
#include "pet_fireworks.h"
#include "pet_media.h"
#endif
#include "lvgl_theme.h"
#include "pet_face_geometry.h"
#include "pet_face_motion.h"
#include "pet_imu.h"
#include "pet_rtc.h"
#include "pet_show_state.h"
#include "audio/input_level.h"
#include "board.h"
#include "application.h"
#include "assets/lang_config.h"

#include <cmath>
#include <cstring>

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_netif.h>

#include <ctime>

static const char* TAG = "PetDisplay";

#define PET_W 240
#define PET_H 240

static constexpr int kTntTapSlopPx = 26;
static constexpr int64_t kTntTapHoldUs = 320000;
static constexpr int kTotemArmSlopPx = 20;

static bool WithinPressSlop(lv_point_t origin, lv_point_t point, int slop) {
    const int dx = point.x - origin.x;
    const int dy = point.y - origin.y;
    return dx * dx + dy * dy <= slop * slop;
}

static uint16_t g_lut[65];

static void BuildLut(uint16_t bg, uint16_t face) {
    int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    int fr = (face >> 11) & 0x1F, fg = (face >> 5) & 0x3F, fb = face & 0x1F;
    for (int i = 0; i <= 64; i++) {
        float v = i / 64.0f;
        int r = br + (int)((fr - br) * v);
        int g = bgc + (int)((fg - bgc) * v);
        int b = bb + (int)((fb - bb) * v);
        if (r < 0) r = 0; else if (r > 31) r = 31;
        if (g < 0) g = 0; else if (g > 63) g = 63;
        if (b < 0) b = 0; else if (b > 31) b = 31;
        g_lut[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

static bool NetworkUp() {
    esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return n != nullptr && esp_netif_is_netif_up(n);
}

static uint32_t LoadBoots() {
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("pet", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &v);
        nvs_close(h);
    }
    return v;
}

static void SaveBoots(uint32_t v) {
    nvs_handle_t h;
    if (nvs_open("pet", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "boots", v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint32_t LoadLastMow() {
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("pet", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "last_mow", &v);
        nvs_close(h);
    }
    return v;
}

static void SaveLastMow(uint32_t v) {
    nvs_handle_t h;
    if (nvs_open("pet", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "last_mow", v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static inline float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void Spring(float* pos, float* vel, float target, float dt, float k, float d) {
    float acc = (target - *pos) * k - *vel * d;
    *vel += acc * dt;
    *pos += *vel * dt;
}

PetDisplay::PetDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                       int width, int height, int offset_x, int offset_y, bool mirror_x,
                       bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

PetDisplay::~PetDisplay() {
#ifdef GROK_FIREWORKS
    fireworks_.reset();
#endif
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
#ifdef GROK_CONTROLS
    controls_.reset();
#endif
    if (fb_) {
        heap_caps_free(fb_);
        fb_ = nullptr;
    }
}

void PetDisplay::SyncColors() {
    lv_color_t bg = lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN);
    int lum = (bg.red * 30 + bg.green * 59 + bg.blue * 11) / 100;
    const uint16_t previous_face = face_;
    if (lum < 128) {
        bg_ = ((bg.red >> 3) << 11) | ((bg.green >> 2) << 5) | (bg.blue >> 3);
        face_ = profile_.color_dark;
    } else {
        bg_ = ((bg.red >> 3) << 11) | ((bg.green >> 2) << 5) | (bg.blue >> 3);
        face_ = profile_.color_light;
    }
    uint32_t packed = ((uint32_t)bg.red << 16) | ((uint32_t)bg.green << 8) | bg.blue;
    if (packed != last_bg_ || face_ != previous_face) {
        last_bg_ = packed;
        BuildLut(bg_, face_);
        ESP_LOGI(TAG, "bg %04x face %04x", bg_, face_);
        if (fb_) {
            for (int i = 0; i < PET_W * PET_H; i++) {
                fb_[i] = bg_;
            }
            dirty_x0_ = -1;
            dirty_y0_ = -1;
            dirty_x1_ = -1;
            dirty_y1_ = -1;
            lv_obj_invalidate(canvas_);
        }
    }
}

#ifdef GROK_CONTROLS
void PetDisplay::SetWifiSetupHandler(std::function<void()> handler) {
    DisplayLockGuard lock(this);
    wifi_setup_handler_ = std::move(handler);
    if (controls_) {
        controls_->SetWifiSetup(wifi_setup_handler_);
    }
}
#endif

void PetDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    /* the pet owns the screen: hide the status/top bars entirely */
    if (top_bar_ != nullptr) {
        lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    if (status_bar_ != nullptr) {
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    /* dark theme by default: white face on black */
    auto& theme_manager = LvglThemeManager::GetInstance();
    if (auto* dark = theme_manager.GetTheme("dark")) {
        SetTheme(dark);
    }

    DisplayLockGuard lock(this);

    SyncColors();

    fb_ = static_cast<uint16_t*>(heap_caps_malloc(PET_W * PET_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (!fb_) {
        ESP_LOGE(TAG, "no psram framebuffer");
        return;
    }
    for (int i = 0; i < PET_W * PET_H; i++) {
        fb_[i] = bg_;
    }

    if (emoji_box_) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }

    canvas_ = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas_, fb_, PET_W, PET_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas_);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(canvas_);
#ifdef GROK_FIREWORKS
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_PRESS_LOCK);
    fireworks_ = std::make_unique<PetFireworks>();
#endif

#ifdef GROK_CONTROLS
    controls_ = std::make_unique<PetControls>(
        lv_screen_active(),
        [this]() {
            if (tnt_.IsActive() || totem_.IsActive()) {
                return false;
            }
#ifdef GROK_FIREWORKS
            if (fireworks_ && fireworks_->IsActive()) {
                return false;
            }
#endif
            return true;
        },
        [this]() {
            tnt_.CancelTaps();
            last_click_ = 0;
            last_touch_ = esp_timer_get_time();
        });
    controls_->SetWifiSetup(wifi_setup_handler_);
#endif

    static const lv_event_code_t kCodes[] = {LV_EVENT_PRESSED, LV_EVENT_PRESSING,
                                             LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST};
    for (auto code : kCodes) {
        lv_obj_add_event_cb(canvas_, ScreenEventCb, code, this);
    }
    lv_obj_add_event_cb(lv_screen_active(), ScreenEventCb, LV_EVENT_PRESSED, this);

    int64_t now = esp_timer_get_time();
    blink_next_ = now + 2500000;
    last_touch_ = now;
    uint32_t boots = LoadBoots() + 1;
    SaveBoots(boots);
    safe_mode_ = boots > 3;
    ESP_LOGI(TAG, "boot #%u safe=%d", (unsigned)boots, (int)safe_mode_);

    saved_mow_ = LoadLastMow();
    if (saved_mow_ != 0) {
        ESP_LOGI(TAG, "remembered last seen at mow=%u", (unsigned)saved_mow_);
    }
    timer_ = lv_timer_create(TickThunk, 33, this);
    ESP_LOGI(TAG, "pet canvas %dx%d ready", PET_W, PET_H);
}

void PetDisplay::SetPetMood(const char* mood) {
    if (!mood) {
        return;
    }
    int64_t now = esp_timer_get_time();
    Mood m = kNeutral;
    if (strstr(mood, "happy") || strstr(mood, "joy")) {
        m = kHappy;
    } else if (strstr(mood, "sad")) {
        m = kSad;
    } else if (strstr(mood, "angry")) {
        m = kAngry;
    } else if (strstr(mood, "surprise")) {
        m = kSurprised;
    } else if (strstr(mood, "think") || strstr(mood, "curious")) {
        m = kThinking;
    } else if (strstr(mood, "sleep") || strstr(mood, "tired")) {
        m = kSleepy;
    } else if (strstr(mood, "listen")) {
        m = kListening;
    }
    mood_ = m;
    mood_until_ = (m == kSleepy) ? 0 : now + 10000000;
    if (m == kHappy || m == kSurprised) {
        squash_v_ -= 2.0f;
        hop_v_ -= 180.0f;
    }
    ESP_LOGI(TAG, "mcp mood %s", mood);
}

void PetDisplay::SetPetLook(int x_percent, int y_percent) {
    look_x_ = Clampf(x_percent / 100.0f, -1.0f, 1.0f);
    look_y_ = Clampf(y_percent / 100.0f, -1.0f, 1.0f);
    look_until_ = esp_timer_get_time() + 3000000;
    ESP_LOGI(TAG, "mcp look %d %d", x_percent, y_percent);
}

std::string PetDisplay::GetPetStatus() {
    const char* m = "neutral";
    switch (mood_) {
        case kHappy: m = "happy"; break;
        case kSad: m = "sad"; break;
        case kAngry: m = "angry"; break;
        case kSurprised: m = "surprised"; break;
        case kThinking: m = "thinking"; break;
        case kListening: m = "listening"; break;
        case kSleepy: m = "sleepy"; break;
        default: break;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "mood=%s battery=%d%% charging=%d time=%02d:%02d drowsy=%d night=%d safe=%d", m,
             battery_level_, (int)charging_, rtc_hour_, rtc_min_, (int)drowsy_, (int)night_,
             (int)safe_mode_);
    return std::string(buf);
}

/* The pet owns the whole screen: status and notification banners would cover the
   face, so they are logged instead of drawn. Chat subtitles still go through
   SetChatMessage and remain visible. */
void PetDisplay::SetStatus(const char* status) {
    ESP_LOGI(TAG, "status: %s", status ? status : "");
}

void PetDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)duration_ms;
    ESP_LOGI(TAG, "notify: %s", notification ? notification : "");
}

void PetDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    (void)duration_ms;
    ESP_LOGI(TAG, "notify: %s", notification.c_str());
}

void PetDisplay::SetChatMessage(const char* role, const char* content) {
    /* "system" carries boot/status lines (initialising, please wait, ...).
       Draw only real conversation text. */
    if (role != nullptr && std::strcmp(role, "system") == 0) {
        ESP_LOGI(TAG, "system: %s", content ? content : "");
        return;
    }
    SpiLcdDisplay::SetChatMessage(role, content);
}

void PetDisplay::SetEmotion(const char* emotion) {
    if (!emotion) {
        return;
    }
    Mood next = kNeutral;
    if (strstr(emotion, "happy") || strstr(emotion, "joy") || strstr(emotion, "laugh")) {
        next = kHappy;
    } else if (strstr(emotion, "sad") || strstr(emotion, "cry")) {
        next = kSad;
    } else if (strstr(emotion, "angry") || strstr(emotion, "mad")) {
        next = kAngry;
    } else if (strstr(emotion, "surprise") || strstr(emotion, "shock")) {
        next = kSurprised;
    } else if (strstr(emotion, "think") || strstr(emotion, "confus")) {
        next = kThinking;
    } else if (strstr(emotion, "listen")) {
        next = kListening;
    }

    mood_ = next;
    mood_until_ = esp_timer_get_time() + 2500000;
    if (next == kHappy || next == kSurprised) {
        squash_v_ -= 1.4f;
    }
}

void PetDisplay::HandlePoke(lv_point_t pt) {
    int64_t now = esp_timer_get_time();
    lv_area_t a;
    lv_obj_get_coords(canvas_, &a);
    float nx = (pt.x - (a.x1 + a.x2) * 0.5f) / (float)PET_W;
    float ny = (pt.y - (a.y1 + a.y2) * 0.5f) / (float)PET_H;
    poke_x_ = Clampf(nx * 2.0f, -1.0f, 1.0f);
    poke_y_ = Clampf(ny * 2.0f, -1.0f, 1.0f);
    poke_until_ = now + 1200000;
    squash_v_ -= 3.4f;
    hop_v_ -= 320.0f;
    mood_ = kHappy;
    mood_until_ = now + 1400000;
    last_touch_ = now;
    if (drowsy_) {
        drowsy_ = false;
        mood_ = kSurprised;
        mood_until_ = now + 900000;
        squash_v_ -= 2.5f;
        hop_v_ -= 260.0f;
        ESP_LOGI(TAG, "wake startle (touch)");
    }
    ESP_LOGI(TAG, "pat at %d,%d", pt.x, pt.y);

    /* a short chirp, but only when the device is not in a conversation */
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
    }
}

#ifdef GROK_FIREWORKS
bool PetDisplay::IsFireworkDrag(lv_point_t point) const {
    const int dx = point.x - press_origin_.x;
    const int dy = point.y - press_origin_.y;
    return firework_drag_ && dy >= 55 && dy >= 1.35f * fabsf(dx);
}
#endif

bool PetDisplay::IsReviveDrag(lv_point_t point) const {
    if (!totem_drag_) {
        return false;
    }
    const int dx = point.x - press_origin_.x;
    const int dy = point.y - press_origin_.y;
    return dy <= -55 && -dy >= 1.35f * fabsf(dx);
}

void PetDisplay::ScreenEventCb(lv_event_t* e) {
    auto* self = static_cast<PetDisplay*>(lv_event_get_user_data(e));
    if (!self || self->tnt_.IsActive() || self->totem_.IsActive()) {
        return;
    }
#ifdef GROK_FIREWORKS
    if (self->fireworks_ && self->fireworks_->IsActive()) {
        return;
    }
#endif
#ifdef GROK_CONTROLS
    if (self->controls_ && self->controls_->IsOpen()) {
        return;
    }
#endif
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    int64_t now = esp_timer_get_time();

    if (code == LV_EVENT_PRESSED) {
        self->pressed_ = true;
        self->long_handled_ = false;
        self->press_start_ = now;
        self->press_pt_ = p;
        self->press_origin_ = p;
        self->tnt_tap_eligible_ = indev && lv_event_get_target(e) == self->canvas_;
        self->totem_drag_ = self->tnt_tap_eligible_;
        self->arm_candidate_ = self->tnt_tap_eligible_ &&
                               CanRunPetShow(Application::GetInstance().GetDeviceState());
        self->arm_handled_ = false;
        self->arm_armed_this_press_ = false;
        if (!self->tnt_tap_eligible_) {
            self->tnt_.CancelTaps();
        }
#ifdef GROK_FIREWORKS
        self->firework_drag_ = self->tnt_tap_eligible_;
#endif
        self->squash_v_ -= 2.5f;
        self->last_touch_ = now;
        if (self->drowsy_) {
            self->drowsy_ = false;
            self->mood_ = PetDisplay::kSurprised;
            self->mood_until_ = now + 900000;
            self->squash_v_ -= 2.5f;
            self->hop_v_ -= 260.0f;
            ESP_LOGI(TAG, "wake startle (press)");
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (self->pressed_) {
            self->press_pt_ = p;
            if (self->tnt_tap_eligible_ &&
                (!indev || !WithinPressSlop(self->press_origin_, p, kTntTapSlopPx))) {
                self->tnt_tap_eligible_ = false;
                self->tnt_.CancelTaps();
            }
            if (self->arm_candidate_ &&
                (!indev || !WithinPressSlop(self->press_origin_, p, kTotemArmSlopPx))) {
                self->arm_candidate_ = false;
                if (self->arm_armed_this_press_) {
                    self->totem_armed_ = false;
                    self->arm_armed_this_press_ = false;
                }
            }
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!self->pressed_) {
            return;
        }
        self->pressed_ = false;
        int64_t held = now - self->press_start_;
        const bool can_run_show = CanRunPetShow(Application::GetInstance().GetDeviceState());
        const bool tnt_tap = code == LV_EVENT_RELEASED && self->tnt_tap_eligible_ && indev &&
                             !self->long_handled_ && held <= kTntTapHoldUs &&
                             WithinPressSlop(self->press_origin_, p, kTntTapSlopPx) &&
                             can_run_show;
        self->tnt_tap_eligible_ = false;
        if (!tnt_tap) {
            self->tnt_.CancelTaps();
        }
#ifdef GROK_FIREWORKS
        const bool launch = code == LV_EVENT_RELEASED && !self->long_handled_ &&
                            self->IsFireworkDrag(indev ? p : self->press_pt_);
        self->firework_drag_ = false;
        if (launch) {
            self->tnt_.CancelTaps();
            if (self->fireworks_ && self->fireworks_->Start(self->canvas_, now)) {
                self->poke_until_ = 0;
                self->last_click_ = 0;
                self->hop_v_ = -250.0f;
                self->squash_v_ = 2.6f;
            }
            return;
        }
#endif

        const bool launch_totem = code == LV_EVENT_RELEASED && !self->long_handled_ &&
                                  self->IsReviveDrag(p);
        if (code == LV_EVENT_PRESS_LOST && self->arm_armed_this_press_) {
            self->totem_armed_ = false;
        }
        self->arm_candidate_ = false;
        self->arm_armed_this_press_ = false;
        self->totem_drag_ = false;
        if (launch_totem) {
            self->tnt_.CancelTaps();
            self->StartTotem(now, false);
            return;
        }

        if (self->long_handled_) {
            self->mood_ = PetDisplay::kNeutral;
            self->mood_until_ = 0;
            ESP_LOGI(TAG, "long press end");
            return;
        }
        if (code != LV_EVENT_RELEASED || held >= 700000) {
            return;
        }

        if (tnt_tap && self->tnt_.Tap(now)) {
            ESP_LOGI(TAG, "TNT show started");
            if (self->totem_armed_) {
                self->totem_after_tnt_ = true;
                self->totem_armed_ = false;
                ESP_LOGI(TAG, "totem aftermath queued for this TNT");
            }
#ifdef GROK_FIREWORKS
            if (self->tnt_request_ && self->tnt_request_->done.load(std::memory_order_acquire)) {
                self->UpdateTntPack(now);
            }
            self->tnt_audio_.Start(self->tnt_.ShowId(), now, self->tnt_clip_,
                                   self->totem_after_tnt_ ? 3000 : 0);
#else
            self->tnt_audio_.Start(self->tnt_.ShowId(), now, {},
                                   self->totem_after_tnt_ ? 3000 : 0);
#endif
            self->last_click_ = self->poke_until_ = 0;
            self->spin_ = self->spin_v_ = 0;
            self->hop_ = self->hop_v_ = self->squash_ = self->squash_v_ = 0;
            self->blinking_ = false;
            self->blink_ = 1.0f;
            self->mood_ = kNeutral;
            self->mood_until_ = 0;
            return;
        }

#ifdef GROK_FIREWORKS
        // Prepare the card override without delaying the fifth tap or the soundtrack.
        if (tnt_tap && self->tnt_.PendingTaps() >= 3 &&
            !self->tnt_clip_ && !self->tnt_request_) {
            self->tnt_request_ = PetMedia::LoadTnt();
        }
#endif

        bool dbl = (now - self->last_click_) < 350000;
        self->last_click_ = now;
        if (dbl) {
            self->spin_v_ = 15.0f;
            self->mood_ = PetDisplay::kSurprised;
            self->mood_until_ = now + 800000;
            ESP_LOGI(TAG, "double tap -> spin");
        } else {
            self->HandlePoke(p);
        }
    }
}

void PetDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<PetDisplay*>(lv_timer_get_user_data(timer));
    if (self) {
        self->Tick();
    }
}

void PetDisplay::StartTotem(int64_t now, bool aftermath) {
    if (totem_.IsActive() || !CanRunPetShow(Application::GetInstance().GetDeviceState())) {
        return;
    }
    totem_.Start(now, aftermath);
    TotemAudioSource source;
#ifdef GROK_FIREWORKS
    if (totem_pack_ && totem_pack_->HasAudio()) {
        source.ogg = totem_pack_->Audio();
        source.keepalive = totem_pack_;
    }
#endif
    // The linked entry waits through the black hold for the TNT tail and
    // starts with a sample offset; the standalone entry starts immediately.
    totem_audio_.Start(totem_.ShowId(), now, source, aftermath);
    poke_until_ = look_until_ = 0;
    mood_ = kNeutral;
    mood_until_ = 0;
    hop_ = hop_v_ = squash_ = squash_v_ = 0.0f;
    spin_ = spin_v_ = 0.0f;
    eye_ = 1.0f;
    eye_v_ = 0.0f;
    eye_hop_ = eye_hop_v_ = 0.0f;
    gx_ = gy_ = gvx_ = gvy_ = 0.0f;
    blink_ = 1.0f;
    blinking_ = false;
    double_blink_ = false;
    last_touch_ = now;
    ESP_LOGI(TAG, "totem start aftermath=%d", static_cast<int>(aftermath));
}

#ifdef GROK_FIREWORKS
void PetDisplay::UpdateFaceProfile(int64_t now) {
    if (pressed_ || tnt_.IsActive() || totem_.IsActive() ||
        (fireworks_ && fireworks_->IsActive())) {
        return;
    }
    if (face_request_ && face_request_->done.load(std::memory_order_acquire)) {
        profile_ = face_request_->profile;
        ESP_LOGI(TAG, "Face profile: %s", face_request_->valid ? "microSD" : "defaults");
        face_request_.reset();
        SyncColors();
    }
    if (!face_request_ && now >= next_face_load_ && face_reload_queued_.exchange(false)) {
        face_request_ = PetMedia::LoadFaceProfile();
        if (!face_request_) {
            face_reload_queued_.store(true);
            next_face_load_ = now + 250000;
        }
    }
}

void PetDisplay::UpdateTotemPack(int64_t now) {
    // Let the face-profile read and the boot UI settle before taking the
    // shared SD worker; a pull-down in the first seconds keeps its old path.
    if (now < 5000000) {
        return;
    }
    if (totem_.IsActive() || tnt_.IsActive() || (fireworks_ && fireworks_->IsActive())) {
        return;
    }
    if (totem_request_ && totem_request_->done.load(std::memory_order_acquire)) {
        totem_pack_ = totem_request_->pack;
        if (totem_pack_) {
            const auto texture = totem_pack_->Texture();
            totem_.SetTexture(&texture);
        } else {
            totem_.SetTexture(nullptr);
        }
        ESP_LOGI(TAG, "Totem pack: %s", totem_pack_ ? "microSD" : "built-in");
        totem_request_.reset();
        return;
    }
    if (!totem_pack_ && !totem_request_ && totem_load_attempts_ < 6 &&
        now >= next_totem_load_) {
        totem_request_ = PetMedia::LoadTotem();
        ++totem_load_attempts_;
        if (!totem_request_) {
            next_totem_load_ = now + 500000;
        }
    }
}

void PetDisplay::UpdateTntPack(int64_t now) {
    if (tnt_request_ && tnt_request_->done.load(std::memory_order_acquire)) {
        tnt_clip_ = std::move(tnt_request_->clip);
        tnt_request_.reset();
        next_tnt_preload_ = 0;
        return;
    }
    // Warm the small PSRAM clip after boot. If the SD worker is occupied,
    // retry the preload later; a missing pack can still be retried on taps.
    if (next_tnt_preload_ && now >= next_tnt_preload_ && !pressed_ &&
        !tnt_request_ && !tnt_clip_) {
        tnt_request_ = PetMedia::LoadTnt();
        next_tnt_preload_ = tnt_request_ ? 0 : now + 250000;
    }
}
#endif

void PetDisplay::Tick() {
    int64_t now = esp_timer_get_time();
    static int64_t last = now;
    float dt = (now - last) / 1e6f;
    last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt <= 0.0f) dt = 0.033f;

#ifdef GROK_FIREWORKS
    UpdateFaceProfile(now);
    const auto firework_stage = fireworks_ ? fireworks_->Update(now) : PetFireworks::Stage::Idle;
    if (firework_stage == PetFireworks::Stage::Flight ||
        firework_stage == PetFireworks::Stage::Burst) {
#ifdef GROK_CONTROLS
        if (controls_) {
            controls_->Hide();
        }
#endif
        return;
    }
    const bool returning = firework_stage == PetFireworks::Stage::Return;
    if (returning && !firework_returning_) {
        // Keep the face's return entrance brief, then let one upward impulse
        // carry it past the rest position before the stage ends.
        hop_ = 0.0f;
        hop_v_ = -300.0f;
        squash_ = squash_v_ = 0.0f;
        blink_next_ = now + 230000;
        if (fireworks_->IsCreeper()) {
            mood_ = kSurprised;
            mood_until_ = now + 360000;
            look_x_ = 0.60f;
            look_y_ = -0.38f;
            look_until_ = now + 560000;
        }
    }
    firework_returning_ = returning;
#endif

#ifdef GROK_FIREWORKS
    UpdateTotemPack(now);
    if (!tnt_.IsActive() && !totem_.IsActive() && !returning) {
        UpdateTntPack(now);
    }
#endif

    const bool can_run_show = CanRunPetShow(Application::GetInstance().GetDeviceState());
    const bool was_tnt = tnt_.IsActive();
    auto tnt_stage = tnt_.Update(now, can_run_show);
    if (was_tnt && tnt_stage == PetTnt::Stage::Recover && totem_after_tnt_) {
        totem_after_tnt_ = false;
        tnt_.Abort();
        tnt_audio_.Cancel();
        StartTotem(now, true);
        tnt_stage = PetTnt::Stage::Idle;
    }
    if (was_tnt && tnt_stage == PetTnt::Stage::Idle) {
        totem_after_tnt_ = false;
        tnt_audio_.Cancel();
    }

    const bool was_totem = totem_.IsActive();
    const auto totem_stage = totem_.Update(now, can_run_show);
    if (totem_stage != PetTotem::Stage::Idle) {
#ifdef GROK_CONTROLS
        if (controls_) {
            controls_->Hide();
        }
#endif
        const float progress = totem_.Progress(now);
        const auto pose = PetTotem::Sample(totem_stage, progress, totem_.Aftermath());
        t_ += dt;
        breath_ += dt * ((drowsy_ || low_batt_ || night_) ? 0.8f : 1.15f);
        Render(1.0f, tnt_stage, 0.0f, &pose, totem_stage, progress);
        return;
    }
    if (was_totem) {
        totem_audio_.Cancel();
        blinking_ = false;
        blink_ = 1.0f;
        blink_next_ = now + 300000;
        t_ += dt;
        breath_ += dt * ((drowsy_ || low_batt_ || night_) ? 0.8f : 1.15f);
    }

    if (tnt_stage != PetTnt::Stage::Idle) {
#ifdef GROK_CONTROLS
        if (controls_) {
            controls_->Hide();
        }
#endif
        Render(1.0f, tnt_stage, tnt_.Progress(now));
        return;
    }
    if (was_tnt) {
        blinking_ = false;
        blink_ = 1.0f;
        blink_next_ = now + 300000;
    }

    if (mood_until_ && now > mood_until_) {
        mood_ = kNeutral;
        mood_until_ = 0;
    }

    {
        static bool was_down = false;
        lv_indev_t* ptr = nullptr;
        for (lv_indev_t* it = lv_indev_get_next(nullptr); it != nullptr; it = lv_indev_get_next(it)) {
            if (lv_indev_get_type(it) == LV_INDEV_TYPE_POINTER) {
                ptr = it;
                break;
            }
        }
        if (ptr) {
            bool down = lv_indev_get_state(ptr) == LV_INDEV_STATE_PRESSED;
            if (down != was_down) {
                was_down = down;
                lv_point_t q = {0, 0};
                lv_indev_get_point(ptr, &q);
                ESP_LOGI(TAG, "indev %d at %d,%d", (int)down, q.x, q.y);
            }
        }
    }

    /* follow theme changes (light/dark) without rebuilding the canvas */
    static int64_t last_sync = 0;
    if (now - last_sync > 500000) {
        last_sync = now;
        SyncColors();
    }



    /* IMU: pick up / shake / tilt */
    if (!imu_tried_ && bus_) {
        imu_tried_ = true;
        imu_ready_ = pet_imu_start(bus_);
        ESP_LOGI(TAG, "imu %s", imu_ready_ ? "ready" : "unavailable");
    }
    if (t_ > 1.5f && imu_ready_ && (now - last_imu_ > 60000)) {
        last_imu_ = now;
        {
            float raw_x = 0.0f, raw_y = 0.0f;
            pet_imu_read(&raw_x, &raw_y, &imu_gyro_, &imu_dev_);
            /* Follow gravity only while the device is steady. While it is being
               rotated the accelerometer is noisy, and chasing it makes the eyes
               swing. Holding the last tilt until motion stops reads as a head
               that settles, not as a shake. */
            if (imu_gyro_ < 120.0f) {
                tilt_x_ += (raw_x - tilt_x_) * 0.35f;
                tilt_y_ += (raw_y - tilt_y_) * 0.35f;
            }
        }
        if (imu_dev_ > 0.2f || imu_gyro_ > 60.0f) {
            last_touch_ = now;
            if (drowsy_) {
                drowsy_ = false;
                mood_ = kSurprised;
                mood_until_ = now + 900000;
                squash_v_ -= 2.5f;
                hop_v_ -= 260.0f;
                ESP_LOGI(TAG, "wake startle (motion)");
            }
        }
        if (now >= dizzy_until_ && imu_dev_ > 0.45f) {
            mood_ = kSurprised;
            mood_until_ = now + 900000;
            squash_v_ -= 2.2f;
            hop_v_ -= 200.0f;
            ESP_LOGI(TAG, "imu surprise dev=%.2f", imu_dev_);
        }
        /* A deliberate shake, not a casual tilt: require several consecutive
           samples above 600 dps (about 250 ms of strong rotation). A single
           flick of the wrist decays away without triggering the spin. */
        if (imu_gyro_ > 600.0f) {
            if (shake_score_ < 12) {
                shake_score_++;
            }
        } else if (shake_score_ > 0) {
            shake_score_--;
        }
        if (shake_score_ >= 4 && now >= dizzy_until_) {
            dizzy_until_ = now + 900000;
            spin_v_ = 8.0f;
            squash_v_ -= 1.5f;
            ESP_LOGI(TAG, "imu dizzy gyro=%.0f", imu_gyro_);
        }
    }

    /* idle life: doze off when nothing happens, small looks while awake */
    if (!drowsy_ && (now - last_touch_) > 120000000) {
        drowsy_ = true;
        ESP_LOGI(TAG, "drowsy (idle)");
    }
    if (next_idle_ == 0) {
        next_idle_ = now + 10000000;
    }
    if (!drowsy_ && now > next_idle_) {
        int action = (int)(esp_random() % 4);
        switch (action) {
            case 0:
                look_x_ = ((int)(esp_random() % 200) - 100) / 100.0f;
                look_y_ = ((int)(esp_random() % 140) - 70) / 100.0f;
                look_until_ = now + 1200000;
                ESP_LOGI(TAG, "idle look %.2f %.2f", look_x_, look_y_);
                break;
            case 1:
                double_blink_ = true;
                blink_next_ = now;
                ESP_LOGI(TAG, "idle double blink");
                break;
            case 2:
                hop_v_ -= 130.0f;
                ESP_LOGI(TAG, "idle hop");
                break;
            default:
                squash_v_ -= 1.0f;
                ESP_LOGI(TAG, "idle sigh");
                break;
        }
        next_idle_ = now + (int64_t)(12000000 + (esp_random() % 28000000));
    }

    /* gestures: hold to pet, drag to lead the gaze */
    if (pressed_) {
        int64_t held = now - press_start_;
#ifdef GROK_FIREWORKS
        const bool dragging_firework = IsFireworkDrag(press_pt_);
#else
        constexpr bool dragging_firework = false;
#endif
        const bool dragging_totem = IsReviveDrag(press_pt_);
        if (held > 700000 && !long_handled_ && !dragging_firework && !dragging_totem) {
            long_handled_ = true;
            mood_ = kSleepy;
            mood_until_ = 0;
            ESP_LOGI(TAG, "long press -> sleepy");
        }
        if (held > 1800000 && arm_candidate_ && !arm_handled_ && can_run_show) {
            arm_handled_ = true;
            totem_armed_ = true;
            arm_armed_this_press_ = true;
            ESP_LOGI(TAG, "totem armed for next TNT");
        }
        if (held > 200000) {
            lv_area_t a;
            lv_obj_get_coords(canvas_, &a);
            float nx = (press_pt_.x - (a.x1 + a.x2) * 0.5f) / (float)PET_W;
            float ny = (press_pt_.y - (a.y1 + a.y2) * 0.5f) / (float)PET_H;
            poke_x_ = Clampf(nx * 2.0f, -1.0f, 1.0f);
            poke_y_ = Clampf(ny * 2.0f, -1.0f, 1.0f);
            poke_until_ = now + 250000;
        }
    }

    /* spin after a double tap */
    if (spin_v_ != 0.0f || spin_ != 0.0f) {
        spin_ += spin_v_ * dt;
        spin_v_ *= (1.0f - 3.0f * dt);
        if (fabsf(spin_v_) < 0.4f) {
            spin_v_ = 0.0f;
            spin_ *= (1.0f - 6.0f * dt);
            if (fabsf(spin_) < 0.02f) {
                spin_ = 0.0f;
            }
        }
    }

    /* battery: a low cell makes the pet sluggish */
    if (now - last_batt_poll_ > 3000000) {
        last_batt_poll_ = now;
        int level = -1;
        bool chg = false;
        bool dis = false;
        const bool available = Board::GetInstance().GetBatteryLevel(level, chg, dis);
        battery_level_ = available && level >= 0 && level <= 100 ? level : -1;
        charging_ = available && chg;
#ifdef GROK_CONTROLS
        discharging_ = available && dis;
#endif
        const bool low = battery_level_ > 0 && battery_level_ < 20 && !charging_;
        if (low != low_batt_) {
            low_batt_ = low;
            ESP_LOGI(TAG, "battery %d%% charging=%d low=%d", battery_level_, (int)charging_,
                     (int)low);
        }
        if (now - last_batt_log_ > 60000000) {
            last_batt_log_ = now;
            ESP_LOGI(TAG, "battery %d%% charging=%d", battery_level_, (int)charging_);
        }
    }

    /* RTC: day/night rhythm */
    if (now - last_rtc_poll_ > 30000000) {
        last_rtc_poll_ = now;
        if (!rtc_ready_ && bus_) {
            rtc_ready_ = pet_rtc_start(bus_);
        }
        int h = 0;
        int m = 0;
        int wd = 0;
        if (rtc_ready_ && pet_rtc_read(&h, &m, &wd)) {
            bool first = (rtc_hour_ < 0);
            rtc_hour_ = h;
            rtc_min_ = m;
            cur_mow_ = (uint32_t)wd * 1440u + (uint32_t)h * 60u + (uint32_t)m;
            bool night = (h >= 23 || h < 7);
            if (night != night_ || first) {
                night_ = night;
                ESP_LOGI(TAG, "rtc %02d:%02d wd=%d mow=%u night=%d", h, m, wd, (unsigned)cur_mow_, (int)night);
            }
        } else if (rtc_hour_ < 0) {
            ESP_LOGI(TAG, "rtc unavailable");
            rtc_hour_ = -2;
        }
    }

    /* stability: a clean 30 s clears the crash counter */
    if (!boots_cleared_ && now > 30000000) {
        boots_cleared_ = true;
        SaveBoots(0);
        if (safe_mode_) {
            ESP_LOGI(TAG, "stable 30s, leaving safe mode");
            safe_mode_ = false;
        }
    }

    /* time sync: one NTP fetch, then write it into the RTC */
    if (!time_synced_ && !safe_mode_ && now > 25000000 && NetworkUp() &&
        Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        if (!sntp_started_) {
            setenv("TZ", "JST-9", 1);
            tzset();
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            sntp_started_ = true;
            ESP_LOGI(TAG, "sntp started");
        } else if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t epoch = time(nullptr);
            struct tm tm_local;
            localtime_r(&epoch, &tm_local);
            if (pet_rtc_set(tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                            tm_local.tm_wday, tm_local.tm_hour, tm_local.tm_min,
                            tm_local.tm_sec)) {
                time_synced_ = true;
                ESP_LOGI(TAG, "sntp synced %04d-%02d-%02d %02d:%02d:%02d -> rtc",
                         tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                         tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
            }
        }
    }

    /* memory: remember when someone last interacted */
    {
        static int64_t prev_touch = 0;
        if (last_touch_ != prev_touch) {
            prev_touch = last_touch_;
            if (cur_mow_ != 0) {
                if (!greeted_ && saved_mow_ != 0) {
                    greeted_ = true;
                    uint32_t delta = (cur_mow_ + 10080u - saved_mow_) % 10080u;
                    if (delta > 360u) {
                        mood_ = kHappy;
                        mood_until_ = now + 3000000;
                        squash_v_ -= 2.2f;
                        hop_v_ -= 220.0f;
                        ESP_LOGI(TAG, "welcome back after %u min", (unsigned)delta);
                    }
                }
                if (now - last_save_ > 300000000) {
                    last_save_ = now;
                    saved_mow_ = cur_mow_;
                    SaveLastMow(saved_mow_);
                    ESP_LOGI(TAG, "saved last seen mow=%u", (unsigned)saved_mow_);
                }
            }
        }
    }

    t_ += dt;
    breath_ += dt * ((drowsy_ || low_batt_ || night_) ? 0.8f : 1.15f);

    /* ---- priority ladder -------------------------------------------------
       1. touch (poke / drag / long press)
       2. dizzy
       3. explicit mood (emotion / MCP)
       4. speaking (TTS envelope)
       5. listening (your voice)
       6. drowsy
       7. idle wander + tilt
       ------------------------------------------------------------------- */
    float lvl = audio_input_level();
    float out_lvl = audio_output_level();
    const bool speaking = out_lvl > 0.02f;
    const bool listening = lvl > 0.02f;

    float tx = 0.55f * sinf(t_ * 0.31f) + 0.15f * sinf(t_ * 0.83f);
    float ty = 0.35f * sinf(t_ * 0.23f + 1.7f);
    float eye_target = 1.0f;

    /* 7: idle wander follows gravity a little */
    tx += Clampf(tilt_x_ * 1.0f, -0.9f, 0.9f);
    ty += Clampf(tilt_y_ * 1.0f, -0.9f, 0.9f);

    /* 3: explicit mood replaces the resting gaze */
    switch (mood_) {
        case kHappy:
            tx = 0.0f; ty = -0.05f; eye_target = 0.72f;
            break;
        case kSad:
            tx = -0.25f; ty = 0.45f; eye_target = 0.85f;
            break;
        case kAngry:
            tx = 0.0f; ty = 0.0f; eye_target = profile_.angry_eye_scale;
            break;
        case kSurprised:
            tx = 0.0f; ty = -0.1f; eye_target = 1.35f;
            break;
        case kThinking:
            tx = 0.5f; ty = -0.6f; eye_target = 0.9f;
            break;
        case kListening:
            tx = 0.0f; ty = -0.15f; eye_target = 1.1f;
            break;
        case kSleepy:
            tx = 0.0f; ty = 0.25f; eye_target = 0.34f;
            break;
        default:
            break;
    }

    /* 6: drowsy dims whatever expression is showing */
    if (drowsy_) {
        eye_target *= 0.45f;
    }
    if (low_batt_) {
        eye_target *= 0.6f;
    }
    if (night_ && !speaking && !listening) {
        eye_target *= 0.8f;
        ty += 0.08f;
    }

    /* 4/5: voice overlays the expression */
    if (speaking) {
        last_touch_ = now;
        if (drowsy_) {
            drowsy_ = false;
            ESP_LOGI(TAG, "wake (speech)");
        }
        eye_target += Clampf(out_lvl * 1.6f, 0.0f, 0.3f);
        if (mood_ == kNeutral || mood_ == kListening || mood_ == kThinking) {
            tx = 0.0f;
            ty = -0.10f;
        }
        static int64_t last_speak_log = 0;
        if (now - last_speak_log > 3000000) {
            last_speak_log = now;
            ESP_LOGI(TAG, "speak %.3f", out_lvl);
        }
    } else if (listening) {
        eye_target += Clampf(lvl * 1.2f, 0.0f, 0.25f);
        if (mood_ == kNeutral || mood_ == kListening) {
            tx = 0.0f;
            ty = -0.12f;
        }
        static int64_t last_voice_log = 0;
        if (now - last_voice_log > 3000000) {
            last_voice_log = now;
            ESP_LOGI(TAG, "voice %.3f", lvl);
        }
    }

    /* MCP look-at wins over the resting gaze but not over touch */
    if (!pressed_ && now < look_until_ && now >= poke_until_) {
        tx = look_x_;
        ty = look_y_;
    }

    /* 1: touch overrides everything else */
    if (pressed_ && long_handled_) {
        tx = 0.0f;
        ty = 0.25f;
        eye_target = 0.34f;
    } else if (now < poke_until_) {
        tx = poke_x_;
        ty = poke_y_;
        eye_target = 0.8f;
    }

    /* 2: dizzy is the highest priority */
    if (now < dizzy_until_) {
        tx = sinf(t_ * 8.0f) * 1.0f;
        ty = cosf(t_ * 6.0f) * 0.9f;
        eye_target = 0.50f;
    }
#ifdef GROK_FIREWORKS
    if (firework_stage == PetFireworks::Stage::LeadIn) {
        tx = 0.0f;
        ty = -0.92f;
        eye_target = 1.2f;
    }
    float pull = 0.0f;
    if (pressed_ && firework_drag_ && !long_handled_) {
        const float dx = fabsf(press_pt_.x - press_origin_.x);
        const float dy = press_pt_.y - press_origin_.y;
        pull = Clampf((dy - fmaxf(8.0f, 1.35f * dx)) / 108.0f, 0.0f, 1.0f);
    }
#else
    constexpr float pull = 0.0f;
#endif
    Spring(&gx_, &gvx_, Clampf(tx, -1.0f, 1.0f), dt, 42.0f, 9.5f);
    Spring(&gy_, &gvy_, Clampf(ty, -1.0f, 1.0f), dt, 42.0f, 9.5f);
    Spring(&squash_, &squash_v_, -0.30f * pull, dt, pet_face::kSquashStiffness,
           pet_face::kSquashDamping);
    Spring(&hop_, &hop_v_, 16.0f * pull, dt, 60.0f, 8.5f);
    if (hop_ < -36.0f) hop_ = -36.0f;
    if (hop_ > 36.0f) hop_ = 36.0f;
    Spring(&eye_hop_, &eye_hop_v_, hop_, dt, pet_face::kEyeLagStiffness, pet_face::kEyeLagDamping);
    if (eye_hop_ < -36.0f)
        eye_hop_ = -36.0f;
    if (eye_hop_ > 36.0f)
        eye_hop_ = 36.0f;
    Spring(&eye_, &eye_v_, eye_target, dt, 120.0f, 14.0f);
    if (eye_ < 0.3f) eye_ = 0.3f;
    if (eye_ > 1.6f) eye_ = 1.6f;

    if (!blinking_ && now > blink_next_ && mood_ != kSurprised) {
        blinking_ = true;
        blink_start_ = now;
    }
    if (blinking_) {
        float k = (float)(now - blink_start_) / static_cast<float>(profile_.blink_duration_us);
        if (k >= 1.0f) {
            blinking_ = false;
            blink_ = 1.0f;
            if (double_blink_) {
                double_blink_ = false;
                blink_next_ = now + 200000;
            } else {
                const uint32_t range = profile_.blink_max_us - profile_.blink_min_us;
                blink_next_ = now + profile_.blink_min_us + (range ? esp_random() % range : 0);
            }
        } else {
            blink_ = 1.0f - (1.0f - fabsf(1.0f - 2.0f * k)) * 0.96f;
        }
    }

#ifdef GROK_CONTROLS
    if (controls_) {
        controls_->Update(now, battery_level_, charging_, discharging_);
        if (controls_->CoversFace()) {
            return;
        }
    }
#endif

    int64_t render_start = esp_timer_get_time();
#ifdef GROK_FIREWORKS
    const float entrance = returning ? fireworks_->ReturnProgress(now) : 1.0f;
    Render(entrance);
    if (returning) {
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }
#else
    Render();
#endif
    static int64_t render_acc = 0;
    static int render_n = 0;
    static int64_t last_render_log = 0;
    render_acc += esp_timer_get_time() - render_start;
    render_n++;
    if (now - last_render_log > 5000000) {
        last_render_log = now;
        ESP_LOGI(TAG, "render %lld us", (long long)(render_acc / (render_n > 0 ? render_n : 1)));
        render_acc = 0;
        render_n = 0;
    }
}

void PetDisplay::Render(float entrance, PetTnt::Stage tnt_stage, float tnt_progress,
                        const PetTotem::Pose* totem, PetTotem::Stage totem_stage,
                        float totem_progress) {
    if (!fb_) {
        return;
    }

    const bool totem_mode = totem != nullptr;
    const PetTotem::Pose tp = totem_mode ? *totem : PetTotem::Pose{};
    const float cam = totem_mode ? tp.zoom : 1.0f;
    const auto tnt_pose = PetTnt::Sample(tnt_stage, tnt_progress);

    int cx = PET_W / 2 + static_cast<int>(roundf(tnt_pose.x));
    if (!totem_mode && mood_ == kAngry && profile_.angry_shake_px > 0.0f) {
        constexpr float kTau = 6.283185307f;
        cx += static_cast<int>(roundf(profile_.angry_shake_px *
                                      sinf(t_ * profile_.angry_shake_hz * kTau)));
    }
    if (totem_mode) {
        cx += static_cast<int>(roundf(tp.shake_x + tp.face_x * cam));
    }
    const float remaining = 1.0f - entrance;
    const float settle = remaining * remaining * remaining;
    const int cy = totem_mode
                       ? PET_H / 2 + static_cast<int>(roundf(tp.face_y * cam + tp.shake_y))
                       : PET_H / 2 + static_cast<int>(hop_ + 65.0f * settle + tnt_pose.y);
    const float R = totem_mode
                        ? 88.0f * tp.face_scale * cam
                        : 88.0f * (1.0f + 0.018f * sinf(breath_)) *
                              (1.0f - 0.60f * settle) * tnt_pose.face_scale;
    const float sq =
        Clampf((totem_mode ? tp.face_squash : squash_) + tnt_pose.squash, -0.48f, 0.48f);
    const float rx = fminf(R * (1.0f + sq), PET_W / 2 - 4.0f);
    // Constrain the actual shape (including spring overshoot) before the eyes
    // and dirty bounds are computed. Keep the face center, soften the vertical
    // stretch, and leave a two-pixel anti-aliasing margin at both edges.
    float ry = R * (1.0f - sq);
    ry = pet_face::ConstrainVerticalRadius(static_cast<float>(cy), ry);
    const float es = totem_mode ? 1.0f : eye_;
    const float eye_open = totem_mode ? tp.eye_open : blink_;

    const float eye_dx = profile_.eye_spacing * rx;
    const float eye_rx = profile_.eye_width * rx * es;
    const float eye_ry = profile_.eye_height * ry * eye_open * es * tnt_pose.eye_height;
    const float eye_lag = (!totem_mode && tnt_stage == PetTnt::Stage::Idle)
                              ? Clampf((eye_hop_ - hop_) * pet_face::kEyeLagScale,
                                       -pet_face::kEyeLagClamp, pet_face::kEyeLagClamp)
                              : 0.0f;
    const float gaze_x = totem_mode ? tp.gaze_x : gx_;
    const float gaze_y = totem_mode ? tp.gaze_y : gy_;
    const float base_y = cy - 0.04f * ry + gaze_y * profile_.gaze_y * ry + eye_lag;
    const float spin_c = totem_mode ? 1.0f : cosf(spin_);
    const float spin_s = totem_mode ? 0.0f : sinf(spin_);
    const float ex0 = cx - eye_dx * spin_c + gaze_x * profile_.gaze_x * rx;
    const float ey0 = base_y - eye_dx * spin_s;
    const float ex1 = cx + eye_dx * spin_c + gaze_x * profile_.gaze_x * rx;
    const float ey1 = base_y + eye_dx * spin_s;

    const float inv_rx = 1.0f / rx;
    const float inv_ry = 1.0f / ry;
    const float inv_soft = rx / 1.6f;
    const float inv_erx = (eye_rx > 0.01f) ? (1.0f / eye_rx) : 0.0f;
    const float inv_ery = (eye_ry > 0.01f) ? (1.0f / eye_ry) : 0.0f;
    const float inv_ecover = (eye_rx > 0.01f) ? (eye_rx / 1.2f) : 0.0f;
    const bool eyes = eye_ry > 0.5f;
    const float face_alpha = totem_mode ? tp.face_alpha : tnt_pose.face_alpha;

    int x0 = (int)(cx - rx) - 2, x1 = (int)(cx + rx) + 2;
    int y0 = (int)(cy - ry) - 2, y1 = (int)(cy + ry) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PET_W - 1) x1 = PET_W - 1;
    if (y1 > PET_H - 1) y1 = PET_H - 1;

    /* one pass per affected row: build the line in fast RAM, then blit it */
    const auto bounds = PetTnt::Bounds(tnt_stage, tnt_pose, PET_W, PET_H);
    const auto totem_bounds =
        totem_mode ? PetTotem::Bounds(totem_stage, tp, PET_W, PET_H) : pet_fireworks::Rect{};
    const auto face_bounds =
        face_alpha > 0.0f ? pet_fireworks::Rect{x0, y0, x1, y1} : pet_fireworks::Rect{};
    const auto footprint =
        pet_fireworks::Union(pet_fireworks::Union(face_bounds, bounds), totem_bounds);
    int ux0 = footprint.x1, ux1 = footprint.x2, uy0 = footprint.y1, uy1 = footprint.y2;
    if (dirty_x0_ >= 0) {
        if (dirty_x0_ < ux0) ux0 = dirty_x0_;
        if (dirty_x1_ > ux1) ux1 = dirty_x1_;
        if (dirty_y0_ < uy0) uy0 = dirty_y0_;
        if (dirty_y1_ > uy1) uy1 = dirty_y1_;
    }
    if (ux0 < 0) ux0 = 0;
    if (uy0 < 0) uy0 = 0;
    if (ux1 > PET_W - 1) ux1 = PET_W - 1;
    if (uy1 > PET_H - 1) uy1 = PET_H - 1;

    /* eye bounding boxes so most pixels skip the ellipse maths */
    int ex0lo = 0, ex0hi = -1, ey0lo = 0, ey0hi = -1;
    int ex1lo = 0, ex1hi = -1, ey1lo = 0, ey1hi = -1;
    if (eyes) {
        const float mx = eye_rx * 1.06f;
        const float my = eye_ry * 1.06f;
        ex0lo = (int)(ex0 - mx);
        ex0hi = (int)(ex0 + mx);
        ex1lo = (int)(ex1 - mx);
        ex1hi = (int)(ex1 + mx);
        ey0lo = (int)(ey0 - my);
        ey0hi = (int)(ey0 + my);
        ey1lo = (int)(ey1 - my);
        ey1hi = (int)(ey1 + my);
    }

    // The totem show always opens on black, even in the light theme, and
    // darkens the face through its own LUT instead of the theme LUT.
    uint16_t totem_lut[65];
    const uint16_t* lut = g_lut;
    uint16_t clear = bg_;
    if (totem_mode) {
        const int fr = (face_ >> 11) & 0x1F;
        const int fg = (face_ >> 5) & 0x3F;
        const int fb = face_ & 0x1F;
        const float dim = 1.0f - Clampf(tp.dark, 0.0f, 1.0f);
        for (int i = 0; i <= 64; ++i) {
            const float k = (i / 64.0f) * dim;
            const int r = static_cast<int>(std::lround(fr * k));
            const int g = static_cast<int>(std::lround(fg * k));
            const int b = static_cast<int>(std::lround(fb * k));
            totem_lut[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        }
        lut = totem_lut;
        clear = 0;
    }

    uint16_t line[PET_W];

    for (int y = uy0; y <= uy1; y++) {
        for (int x = ux0; x <= ux1; x++) {
            line[x] = clear;
        }

        if (face_alpha > 0.0f && y >= y0 && y <= y1) {
            float dy = ((float)y - (float)cy) * inv_ry;
            float dy2 = dy * dy;
            float ay0 = ((float)y - ey0) * inv_ery;
            float ay2_0 = ay0 * ay0;
            float ay1 = ((float)y - ey1) * inv_ery;
            float ay2_1 = ay1 * ay1;
            const bool row0 = (y >= ey0lo && y <= ey0hi);
            const bool row1 = (y >= ey1lo && y <= ey1hi);

            const float inv_rx_sq = inv_rx * inv_rx;
            for (int x = x0; x <= x1; x++) {
                const int dx = x - cx;
                float d2 = dx * dx * inv_rx_sq + dy2;
                if (d2 > 1.05f) {
                    continue;
                }

                float a;
                if (d2 < 0.92f) {
                    a = 1.0f;
                } else {
                    a = (1.0f - sqrtf(d2)) * inv_soft + 0.5f;
                    if (a < 0.0f) continue;
                    if (a > 1.0f) a = 1.0f;
                }

                float eye = 0.0f;
                if (row0 && x >= ex0lo && x <= ex0hi) {
                    float ax = ((float)x - ex0) * inv_erx;
                    float q = ax * ax + ay2_0;
                    if (q < 0.86f) {
                        eye = 1.0f;
                    } else if (q < 1.12f) {
                        eye = (1.0f - sqrtf(q)) * inv_ecover + 0.5f;
                        if (eye < 0.0f) eye = 0.0f;
                        else if (eye > 1.0f) eye = 1.0f;
                    }
                }
                if (eye < 1.0f && row1 && x >= ex1lo && x <= ex1hi) {
                    float ax = ((float)x - ex1) * inv_erx;
                    float q = ax * ax + ay2_1;
                    if (q < 0.86f) {
                        eye = 1.0f;
                    } else if (q < 1.12f) {
                        float e2 = (1.0f - sqrtf(q)) * inv_ecover + 0.5f;
                        if (e2 < 0.0f) e2 = 0.0f;
                        else if (e2 > 1.0f) e2 = 1.0f;
                        if (e2 > eye) eye = e2;
                    }
                }

                float v = a * (1.0f - eye) * face_alpha;
                if (v <= 0.0f) {
                    continue;
                }
                int idx = (int)(v * 64.0f);
                if (idx < 0) idx = 0; else if (idx > 64) idx = 64;
                line[x] = lut[idx];
            }
        }

        memcpy(&fb_[y * PET_W + ux0], &line[ux0], (ux1 - ux0 + 1) * sizeof(uint16_t));
    }

    if (!pet_fireworks::Empty(bounds)) {
        const uint16_t* texture = nullptr;
#ifdef GROK_FIREWORKS
        if (tnt_clip_) {
            texture = tnt_clip_->texture.data();
        }
#endif
        PetTnt::Draw(fb_, PET_W, PET_H, tnt_stage, tnt_progress, tnt_pose, texture);
    }
    if (totem_mode) {
        PetTotem::Draw(fb_, PET_W, PET_H, totem_stage, totem_progress, tp, totem_.Texture());
    }
    dirty_x0_ = footprint.x1;
    dirty_y0_ = footprint.y1;
    dirty_x1_ = footprint.x2;
    dirty_y1_ = footprint.y2;
    lv_area_t coords;
    lv_obj_get_coords(canvas_, &coords);
    lv_area_t dirty = {coords.x1 + ux0, coords.y1 + uy0, coords.x1 + ux1, coords.y1 + uy1};
    lv_obj_invalidate_area(canvas_, &dirty);
}




























