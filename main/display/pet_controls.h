#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include <lvgl.h>

class PetControls {
public:
    PetControls(lv_obj_t* screen, std::function<bool()> can_open, std::function<void()> touch);
    ~PetControls();

    void SetWifiSetup(std::function<void()> setup);
    void Update(int64_t now, int battery, bool charging, bool discharging);
    bool IsOpen() const { return open_; }
    bool CoversFace() const;
    void Hide();

private:
    enum class Page { Server, Settings, Signals, Wifi };
    enum class Axis { None, Horizontal, Vertical };

    static void Event(lv_event_t* e);
    static void MoveHandle(void* object, int32_t y);
    void HandleEvent(lv_event_t* e);
    void Activate(lv_obj_t* obj, int64_t now);
    void Reveal();
    void SetOpen(bool open, Page page = Page::Settings);
    void Select(Page page);
    void SetConfirm(bool active);
    void RefreshStatus(int battery, bool charging, bool discharging);

    std::function<bool()> can_open_;
    std::function<void()> touch_;
    std::function<void()> wifi_setup_;
    lv_obj_t* hotspot_ = nullptr;
    lv_obj_t* drawer_ = nullptr;
    lv_obj_t* handle_ = nullptr;
    lv_obj_t* heading_ = nullptr;
    std::array<lv_obj_t*, 4> pages_{};
    std::array<lv_obj_t*, 3> tabs_{};
    lv_obj_t* battery_value_ = nullptr;
    lv_obj_t* battery_hint_ = nullptr;
    lv_obj_t* wifi_value_ = nullptr;
    lv_obj_t* wifi_hint_ = nullptr;
    lv_obj_t* wifi_tile_ = nullptr;
    lv_obj_t* wifi_entry_ = nullptr;
    lv_obj_t* wifi_back_ = nullptr;
    lv_obj_t* wifi_step_ = nullptr;
    lv_obj_t* wifi_ap_ = nullptr;
    lv_obj_t* wifi_url_ = nullptr;
    lv_obj_t* volume_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* brightness_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* pair_ = nullptr;
    lv_obj_t* pair_text_ = nullptr;
    lv_obj_t* confirm_ = nullptr;
    lv_obj_t* cancel_ = nullptr;
    lv_obj_t* accept_ = nullptr;
    lv_obj_t* signal_rssi_ = nullptr;
    lv_obj_t* mic_level_ = nullptr;
    Page page_ = Page::Settings;
    bool open_ = false;
    bool tracking_ = false;
    Axis axis_ = Axis::None;
    lv_point_t start_{};
    lv_point_t last_tap_{};
    int64_t press_time_ = 0;
    int64_t tap_time_ = 0;
    int64_t next_status_ = 0;
    int64_t next_mic_ = 0;
    int64_t wifi_pending_until_ = 0;
    bool wifi_start_failed_ = false;
};
