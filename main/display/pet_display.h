#ifndef PET_DISPLAY_H
#define PET_DISPLAY_H

#include "lcd_display.h"
#include "pet_face_profile.h"
#include "pet_tnt.h"
#include "pet_tnt_audio.h"
#include "pet_totem.h"
#include "pet_totem_audio.h"
#include "driver/i2c_master.h"

#ifdef GROK_CONTROLS
#include <functional>
#include <memory>
class PetControls;
#endif

#ifdef GROK_FIREWORKS
#include <atomic>
#include <memory>
namespace PetMedia {
struct FaceRequest;
struct TntRequest;
class TotemPack;
struct TotemRequest;
}  // namespace PetMedia
class PetFireworks;
#endif

// A procedural face that replaces the emoji/GIF renderer on this board.
class PetDisplay : public SpiLcdDisplay {
public:
    PetDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
               bool swap_xy);
    ~PetDisplay() override;

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetPetMood(const char* mood) override;
    void SetPetLook(int x_percent, int y_percent) override;
    std::string GetPetStatus() override;
#ifdef GROK_CONTROLS
    void SetWifiSetupHandler(std::function<void()> handler);
#endif
    void SetSharedI2cBus(i2c_master_bus_handle_t bus) { bus_ = bus; }

#ifdef GROK_FIREWORKS
    void QueueFaceReload() { face_reload_queued_.store(true); }
#endif

private:
    enum Mood { kNeutral, kHappy, kSad, kAngry, kSurprised, kThinking, kListening, kSleepy };

    static void TickThunk(lv_timer_t* timer);
    static void ScreenEventCb(lv_event_t* e);
    void HandlePoke(lv_point_t pt);
    void Tick();
    void Render(float entrance = 1.0f, PetTnt::Stage tnt_stage = PetTnt::Stage::Idle,
                float tnt_progress = 0.0f, const PetTotem::Pose* totem = nullptr,
                PetTotem::Stage totem_stage = PetTotem::Stage::Idle, float totem_progress = 0.0f);
    void StartTotem(int64_t now, bool aftermath);
    bool IsReviveDrag(lv_point_t point) const;
    void SyncColors();

    lv_obj_t* canvas_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    uint16_t* fb_ = nullptr;

    Mood mood_ = kNeutral;
    int64_t mood_until_ = 0;
    int64_t poke_until_ = 0;
    float poke_x_ = 0.0f;
    float poke_y_ = 0.0f;
    bool touch_down_ = false;
    bool pressed_ = false;
    bool long_handled_ = false;
    int64_t press_start_ = 0;
    int64_t last_click_ = 0;
    lv_point_t press_pt_ = {0, 0};
    lv_point_t press_origin_{};
    bool tnt_tap_eligible_ = false;
    bool totem_drag_ = false;
    bool arm_candidate_ = false;
    bool arm_armed_this_press_ = false;
    bool totem_armed_ = false;
    bool totem_after_tnt_ = false;
    bool arm_handled_ = false;
    PetTnt tnt_;
    PetTntAudio tnt_audio_;
    PetTotem totem_;
    PetTotemAudio totem_audio_;
#ifdef GROK_FIREWORKS
    void UpdateFaceProfile(int64_t now);
    void UpdateTotemPack(int64_t now);
    void UpdateTntPack(int64_t now);
    std::atomic<bool> face_reload_queued_{true};
    int64_t next_face_load_ = 0;
    std::shared_ptr<PetMedia::FaceRequest> face_request_;
    std::shared_ptr<PetMedia::TntRequest> tnt_request_;
    std::shared_ptr<const PetMedia::TntClip> tnt_clip_;
    int64_t next_tnt_preload_ = 5000000;
    std::unique_ptr<PetFireworks> fireworks_;
    bool firework_drag_ = false;
    bool firework_returning_ = false;
    bool IsFireworkDrag(lv_point_t point) const;
    std::shared_ptr<PetMedia::TotemPack> totem_pack_;
    std::shared_ptr<PetMedia::TotemRequest> totem_request_;
    int64_t next_totem_load_ = 0;
    int totem_load_attempts_ = 0;
#endif
    float spin_ = 0.0f;
    float spin_v_ = 0.0f;

    PetFaceProfile profile_;
    uint32_t last_bg_ = 0xFFFFFFFF;
    uint16_t bg_ = 0xFFFF;
    uint16_t face_ = 0x1082;
    int dirty_x0_ = -1;
    int dirty_y0_ = -1;
    int dirty_x1_ = -1;
    int dirty_y1_ = -1;

    float t_ = 0.0f;
    float breath_ = 0.0f;
    float gx_ = 0.0f, gy_ = 0.0f, gvx_ = 0.0f, gvy_ = 0.0f;
    float squash_ = 0.0f, squash_v_ = 0.0f;
    float hop_ = 0.0f, hop_v_ = 0.0f;
    // Eyes follow the face's vertical spring with a short clamp-limited lag.
    float eye_hop_ = 0.0f, eye_hop_v_ = 0.0f;
    float eye_ = 1.0f, eye_v_ = 0.0f;
    float blink_ = 1.0f;
    int64_t blink_next_ = 0;
    int64_t blink_start_ = 0;
    bool blinking_ = false;
    bool double_blink_ = false;
    int shake_score_ = 0;

    int64_t last_touch_ = 0;
    bool drowsy_ = false;
#ifdef GROK_CONTROLS
    std::unique_ptr<PetControls> controls_;
    std::function<void()> wifi_setup_handler_;
    bool discharging_ = false;
#endif
    int battery_level_ = -1;
    bool charging_ = false;
    bool low_batt_ = false;
    int64_t last_batt_poll_ = 0;
    int64_t last_batt_log_ = 0;
    bool rtc_ready_ = false;
    bool night_ = false;
    int rtc_hour_ = -1;
    int rtc_min_ = -1;
    int64_t last_rtc_poll_ = 0;
    uint32_t cur_mow_ = 0;
    uint32_t saved_mow_ = 0;
    bool greeted_ = false;
    int64_t last_save_ = 0;
    bool sntp_started_ = false;
    bool time_synced_ = false;
    bool safe_mode_ = false;
    bool boots_cleared_ = false;
    int64_t next_idle_ = 0;
    float look_x_ = 0.0f;
    float look_y_ = 0.0f;
    int64_t look_until_ = 0;

    i2c_master_bus_handle_t bus_ = nullptr;
    bool imu_ready_ = false;
    bool imu_tried_ = false;
    float tilt_x_ = 0.0f;
    float tilt_y_ = 0.0f;
    float imu_gyro_ = 0.0f;
    float imu_dev_ = 0.0f;
    int64_t dizzy_until_ = 0;
    int64_t last_imu_ = 0;
};

#endif  // PET_DISPLAY_H













