#include "pet_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string_view>
#include <utility>

#include <esp_timer.h>
#include <wifi_manager.h>

#include "application.h"
#include "audio/audio_codec.h"
#include "audio/input_level.h"
#include "backlight.h"
#include "board.h"

LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

namespace {
constexpr int kWidth = 390;
constexpr int kHeight = 478;
constexpr int kOpenY = 12;
constexpr int kHandleY = 12;
constexpr int kHandleTravel = 60;
constexpr int kGestureSlop = 8;
constexpr int kSwipe = 36;
constexpr uint32_t kWhite = 0xf5f5f3;
constexpr uint32_t kMuted = 0xb0b0b2;
constexpr uint32_t kSurface = 0x111112;
constexpr uint32_t kTile = 0x181819;
constexpr uint32_t kStroke = 0x303032;

lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color,
              int radius = 0, bool border = false) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    if (border) {
        lv_obj_set_style_border_color(obj, lv_color_hex(kStroke), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
    }
    return obj;
}

lv_obj_t* Text(lv_obj_t* parent, int x, int y, const char* text, uint32_t color = kWhite,
               const lv_font_t* font = &font_noto_sans_basic_20_4) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* Button(lv_obj_t* parent, int x, int y, int w, int h, const char* text) {
    auto* button = Box(parent, x, y, w, h, kTile, 12, true);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x343436), LV_STATE_PRESSED);
    lv_obj_center(Text(button, 0, 0, text));
    return button;
}

lv_obj_t* Slider(lv_obj_t* parent, int y, const char* title, lv_obj_t*& value, int minimum) {
    Text(parent, 12, y, title, kMuted);
    value = Text(parent, 0, y, "--");
    lv_obj_set_width(value, 64);
    lv_obj_set_x(value, 274);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    auto* slider = lv_slider_create(parent);
    lv_obj_remove_style_all(slider);
    lv_obj_set_pos(slider, 18, y + 29);
    lv_obj_set_size(slider, 318, 4);
    lv_obj_set_ext_click_area(slider, 13);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x424244), LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kWhite), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kWhite), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_slider_set_range(slider, minimum, 100);
    return slider;
}

void Visibility(lv_obj_t* obj, bool show) {
    if (show) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void Label(lv_obj_t* label, const char* value) {
    if (std::string_view(lv_label_get_text(label)) != value) {
        lv_label_set_text(label, value);
    }
}
}  // namespace

PetControls::PetControls(lv_obj_t* screen, std::function<bool()> can_open,
                         std::function<void()> touch)
    : can_open_(std::move(can_open)), touch_(std::move(touch)) {
    hotspot_ = Box(screen, 0, 0, 410, 70, 0);
    lv_obj_set_style_bg_opa(hotspot_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hotspot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hotspot_, LV_OBJ_FLAG_PRESS_LOCK);

    drawer_ = Box(screen, 10, kOpenY, kWidth, kHeight, kSurface, 30, true);
    Visibility(drawer_, false);
    auto* content = Box(drawer_, 17, 43, 354, 410, kSurface);
    Text(content, 0, 0, "GROKBOT / CONTROL", kMuted, &lv_font_montserrat_14);
    heading_ = Text(content, 0, 20, "设定", kWhite, &font_noto_sans_basic_30_4);

    for (auto*& page : pages_) {
        page = Box(content, 0, 65, 354, 288, kSurface);
    }
    auto* settings = pages_[static_cast<unsigned>(Page::Settings)];
    auto* battery = Box(settings, 0, 0, 173, 103, kTile, 16, true);
    Text(battery, 12, 9, "电量", kMuted);
    battery_value_ = Text(battery, 12, 34, "--", kWhite, &font_noto_sans_basic_30_4);
    battery_hint_ = Text(battery, 12, 70, "--", kMuted);
    auto* wifi = wifi_tile_ = Box(settings, 181, 0, 173, 103, kTile, 16, true);
    Text(wifi, 12, 9, "Wi-Fi", kMuted);
    wifi_value_ = Text(wifi, 12, 34, "--", kWhite, &font_noto_sans_basic_30_4);
    wifi_hint_ = Text(wifi, 12, 70, "--", kMuted);
    lv_obj_set_width(wifi_hint_, 149);
    lv_label_set_long_mode(wifi_hint_, LV_LABEL_LONG_MODE_DOTS);

    auto* sliders = Box(settings, 0, 112, 354, 117, kTile, 16, true);
    volume_ = Slider(sliders, 7, "音量", volume_value_, 0);
    Box(sliders, 12, 57, 330, 1, kStroke);
    brightness_ = Slider(sliders, 62, "Display", brightness_value_, 5);

    wifi_entry_ = Button(settings, 0, 238, 354, 45, "Wi-Fi Setup");
    auto* wifi_page = pages_[static_cast<unsigned>(Page::Wifi)];
    auto* wifi_help = Box(wifi_page, 0, 0, 354, 186, kTile, 16, true);
    wifi_step_ = Text(wifi_help, 13, 12, "Set up using your phone");
    wifi_ap_ = Text(wifi_help, 13, 65, "", kMuted);
    wifi_url_ = Text(wifi_help, 13, 111, "", kMuted);
    for (auto* label : {wifi_step_, wifi_ap_, wifi_url_}) {
        lv_obj_set_width(label, 326);
    }
    wifi_back_ = Button(wifi_page, 0, 205, 95, 46, "Back");
    pair_ = Button(wifi_page, 107, 205, 247, 46, "重新配网");
    pair_text_ = lv_obj_get_child(pair_, 0);

    auto* server = pages_[static_cast<unsigned>(Page::Server)];
    auto* server_tile = Box(server, 0, 0, 354, 119, kTile, 16, true);
    Text(server_tile, 13, 11, "SERVER", kMuted, &lv_font_montserrat_14);
    Text(server_tile, 13, 46, "No endpoint configured");

    auto* signals = pages_[static_cast<unsigned>(Page::Signals)];
    auto* signal_tile = Box(signals, 0, 0, 354, 128, kTile, 16, true);
    Text(signal_tile, 13, 12, "Mic level", kMuted);
    mic_level_ = lv_bar_create(signal_tile);
    lv_obj_remove_style_all(mic_level_);
    lv_obj_set_pos(mic_level_, 14, 50);
    lv_obj_set_size(mic_level_, 324, 7);
    lv_obj_set_style_bg_color(mic_level_, lv_color_hex(kStroke), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mic_level_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(mic_level_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mic_level_, lv_color_hex(kWhite), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mic_level_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(mic_level_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    Text(signal_tile, 13, 79, "Wi-Fi RSSI", kMuted);
    signal_rssi_ = Text(signal_tile, 218, 79, "--");

    const char* names[] = {"Server", "设定", "Signals"};
    for (unsigned i = 0; i < tabs_.size(); ++i) {
        tabs_[i] = Button(content, i * 120, 361, 114, 39, names[i]);
        lv_obj_set_style_bg_color(tabs_[i], lv_color_hex(kWhite), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(tabs_[i], 0), lv_color_hex(kSurface),
                                    LV_STATE_CHECKED);
    }

    handle_ = Box(screen, 147, kHandleY, 116, 40, kSurface);
    lv_obj_set_style_bg_opa(handle_, LV_OPA_TRANSP, 0);
    lv_obj_set_ext_click_area(handle_, 32);
    Box(handle_, 37, 17, 42, 4, 0xaaaaaa, 4);
    Visibility(handle_, false);

    confirm_ = Box(drawer_, 0, 0, kWidth, kHeight, 0);
    lv_obj_add_flag(confirm_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(confirm_, LV_OPA_70, 0);
    auto* dialog = Box(confirm_, 33, 133, 324, 174, kTile, 22, true);
    Text(dialog, 16, 15, "重新配网", kWhite, &font_noto_sans_basic_30_4);
    Text(dialog, 16, 63, "Disconnect Wi-Fi and start AP?", kMuted);
    cancel_ = Button(dialog, 16, 112, 126, 42, "Cancel");
    accept_ = Button(dialog, 150, 112, 156, 42, "Start setup");
    Visibility(confirm_, false);

    for (auto* item : {hotspot_, handle_, drawer_, wifi_tile_, wifi_entry_, wifi_back_,
                       pair_, tabs_[0], tabs_[1], tabs_[2]}) {
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(item, Event, LV_EVENT_ALL, this);
    }
    for (auto* item : {volume_, brightness_}) {
        lv_obj_add_event_cb(item, Event, LV_EVENT_ALL, this);
    }
    for (auto* item : {cancel_, accept_}) {
        lv_obj_add_event_cb(item, Event, LV_EVENT_CLICKED, this);
    }
    Select(Page::Settings);
}

PetControls::~PetControls() {
    lv_anim_delete(handle_, MoveHandle);
    lv_obj_delete(handle_);
    lv_obj_delete(drawer_);
    lv_obj_delete(hotspot_);
}

void PetControls::SetWifiSetup(std::function<void()> setup) {
    wifi_setup_ = std::move(setup);
}

void PetControls::MoveHandle(void* object, int32_t y) {
    lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
}

bool PetControls::CoversFace() const {
    return open_ || (tracking_ && axis_ == Axis::Vertical);
}

void PetControls::Hide() {
    if (lv_obj_has_flag(handle_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_anim_delete(handle_, MoveHandle);
    open_ = tracking_ = false;
    axis_ = Axis::None;
    tap_time_ = 0;
    SetConfirm(false);
    Visibility(handle_, false);
    Visibility(drawer_, false);
    Visibility(hotspot_, true);
    lv_obj_set_y(handle_, kHandleY);
}

void PetControls::Reveal() {
    Visibility(hotspot_, false);
    Visibility(handle_, true);
}

void PetControls::Select(Page page) {
    page_ = page;
    constexpr const char* titles[] = {"Server", "设定", "Signals", "Wi-Fi Setup"};
    const auto selected = static_cast<unsigned>(page);
    Label(heading_, titles[selected]);
    for (unsigned i = 0; i < pages_.size(); ++i) {
        Visibility(pages_[i], i == selected);
    }
    const auto selected_tab = page == Page::Wifi ? static_cast<unsigned>(Page::Settings) : selected;
    for (unsigned i = 0; i < tabs_.size(); ++i) {
        if (i == selected_tab) {
            lv_obj_add_state(tabs_[i], LV_STATE_CHECKED);
            lv_obj_add_state(lv_obj_get_child(tabs_[i], 0), LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(tabs_[i], LV_STATE_CHECKED);
            lv_obj_remove_state(lv_obj_get_child(tabs_[i], 0), LV_STATE_CHECKED);
        }
    }
    next_status_ = 0;
}

void PetControls::SetOpen(bool open, Page page) {
    const bool entering = open && !open_;
    open_ = open;
    Visibility(drawer_, open);
    if (open) {
        Reveal();
        Select(page);
        if (entering) {
            auto& board = Board::GetInstance();
            if (auto* codec = board.GetAudioCodec()) {
                lv_slider_set_value(volume_, codec->output_volume(), LV_ANIM_OFF);
            }
            if (auto* light = board.GetBacklight()) {
                lv_slider_set_value(brightness_, light->brightness(), LV_ANIM_OFF);
            }
            lv_label_set_text_fmt(volume_value_, "%d%%", int(lv_slider_get_value(volume_)));
            lv_label_set_text_fmt(brightness_value_, "%d%%", int(lv_slider_get_value(brightness_)));
        }
    } else {
        SetConfirm(false);
    }
    const int32_t from = lv_obj_get_y_aligned(handle_);
    lv_anim_delete(handle_, MoveHandle);
    if (from == kHandleY) {
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, handle_);
    lv_anim_set_exec_cb(&anim, MoveHandle);
    lv_anim_set_values(&anim, from, kHandleY);
    lv_anim_set_duration(&anim, 120);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void PetControls::SetConfirm(bool active) {
    Visibility(confirm_, active);
    if (active) {
        lv_obj_move_foreground(confirm_);
    }
}

void PetControls::Event(lv_event_t* e) {
    static_cast<PetControls*>(lv_event_get_user_data(e))->HandleEvent(e);
}

void PetControls::HandleEvent(lv_event_t* e) {
    auto* obj = lv_event_get_target_obj(e);
    const auto code = lv_event_get_code(e);
    if (obj == volume_ || obj == brightness_) {
        const int value = lv_slider_get_value(obj);
        if (code == LV_EVENT_VALUE_CHANGED) {
            lv_label_set_text_fmt(obj == volume_ ? volume_value_ : brightness_value_, "%d%%", value);
        } else if (code == LV_EVENT_RELEASED) {
            touch_();
            const bool volume = obj == volume_;
            Application::GetInstance().Schedule([volume, value]() {
                auto& board = Board::GetInstance();
                if (volume) {
                    if (auto* codec = board.GetAudioCodec(); codec && codec->output_volume() != value) {
                        codec->SetOutputVolume(value);
                    }
                } else if (auto* light = board.GetBacklight(); light && light->brightness() != value) {
                    light->SetBrightness(value, true);
                }
            });
        }
        return;
    }

    const auto now = esp_timer_get_time();
    if (obj == cancel_ || obj == accept_) {
        if (code == LV_EVENT_CLICKED && open_) {
            Activate(obj, now);
        }
        return;
    }

    auto* indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
        return;
    }
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        if (!can_open_() || !lv_obj_has_flag(confirm_, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        touch_();
        tracking_ = true;
        start_ = point;
        press_time_ = now;
        axis_ = Axis::None;
        if (obj == handle_) {
            lv_anim_delete(handle_, MoveHandle);
        }
        return;
    }
    if (!tracking_) {
        return;
    }
    const int dx = point.x - start_.x;
    const int dy = point.y - start_.y;
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (axis_ == Axis::None) {
            if (std::abs(dx) >= kGestureSlop && std::abs(dx) * 4 > std::abs(dy) * 5) {
                axis_ = Axis::Horizontal;
            } else if (std::abs(dy) >= kGestureSlop && std::abs(dy) * 4 > std::abs(dx) * 5) {
                axis_ = Axis::Vertical;
            }
        }
        if (code == LV_EVENT_PRESSING && obj == handle_ && axis_ == Axis::Vertical) {
            const int travel = open_ ? std::clamp(dy, -kHandleY, 0)
                                     : std::clamp(dy, 0, kHandleTravel);
            const int y = kHandleY + travel;
            if (lv_obj_get_y_aligned(handle_) != y) {
                lv_obj_set_y(handle_, y);
            }
        }
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    tracking_ = false;
    const auto axis = axis_;
    axis_ = Axis::None;
    const bool released = code == LV_EVENT_RELEASED;
    const bool tapped = released && axis == Axis::None &&
                        std::abs(dx) < kGestureSlop && std::abs(dy) < kGestureSlop;
    if (obj == hotspot_) {
        const bool short_tap = tapped && now - press_time_ <= 240000;
        if (short_tap && tap_time_ && now - tap_time_ <= 330000 &&
            std::abs(point.x - last_tap_.x) < 36) {
            tap_time_ = 0;
            Reveal();
        } else {
            last_tap_ = point;
            tap_time_ = short_tap ? now : 0;
        }
        return;
    }
    if (released && axis == Axis::Horizontal && std::abs(dx) >= kSwipe) {
        const auto current = open_ && page_ != Page::Wifi ? page_ : Page::Settings;
        const auto target = std::clamp(static_cast<int>(current) + (dx < 0 ? -1 : 1),
                                       static_cast<int>(Page::Server),
                                       static_cast<int>(Page::Signals));
        SetOpen(true, static_cast<Page>(target));
    } else if ((obj == handle_ && tapped) ||
               (released && axis == Axis::Vertical && (open_ ? dy <= -kSwipe : dy >= kSwipe))) {
        SetOpen(!open_, Page::Settings);
    } else {
        if (obj == handle_) {
            SetOpen(open_, page_);
        }
        if (open_ && tapped) {
            Activate(obj, now);
        }
    }
}

void PetControls::Activate(lv_obj_t* obj, int64_t now) {
    if (obj == cancel_ || obj == accept_) {
        touch_();
        SetConfirm(false);
        if (obj == accept_ && wifi_setup_ && !wifi_pending_until_ &&
            !WifiManager::GetInstance().IsConfigMode()) {
            wifi_pending_until_ = now + 8000000;
            wifi_start_failed_ = false;
            next_status_ = 0;
            wifi_setup_();
        }
    } else if (obj == wifi_tile_ || obj == wifi_entry_) {
        Select(Page::Wifi);
    } else if (obj == wifi_back_) {
        Select(Page::Settings);
    } else if (obj == pair_ && wifi_setup_ && !wifi_pending_until_ &&
               !WifiManager::GetInstance().IsConfigMode()) {
        SetConfirm(true);
    } else {
        for (unsigned i = 0; i < tabs_.size(); ++i) {
            if (obj == tabs_[i]) {
                Select(static_cast<Page>(i));
                break;
            }
        }
    }
}

void PetControls::RefreshStatus(int battery, bool charging, bool discharging) {
    char text[64];
    if (battery >= 0 && battery <= 100) {
        std::snprintf(text, sizeof(text), "%d%%", battery);
        Label(battery_value_, text);
        Label(battery_hint_, charging ? "充电中" : discharging ? "Battery power" : "Not charging");
    } else {
        Label(battery_value_, "--");
        Label(battery_hint_, "No battery data");
    }

    auto& wifi = WifiManager::GetInstance();
    const bool setup = Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring;
    if (wifi.IsInitialized() && wifi.IsConnected()) {
        const int rssi = wifi.GetRssi();
        std::snprintf(text, sizeof(text), "%d dBm", rssi);
        Label(wifi_value_, text);
        Label(signal_rssi_, text);
        Label(wifi_hint_, wifi.GetSsid().c_str());
    } else {
        Label(wifi_value_, "--");
        Label(signal_rssi_, "--");
        Label(wifi_hint_, setup ? "配网中" : "无连接");
    }
    if (page_ != Page::Wifi) {
        return;
    }
    const auto ap = wifi.GetApSsid();
    const auto url = wifi.GetApWebUrl();
    if (!ap.empty() && !url.empty()) {
        wifi_pending_until_ = 0;
        wifi_start_failed_ = false;
        Label(wifi_step_, "1. Join this hotspot on your phone");
        Label(wifi_ap_, ap.c_str());
        Label(wifi_url_, ("2. Open " + url).c_str());
        Label(pair_text_, "Hotspot active");
    } else if (wifi_pending_until_) {
        Label(wifi_step_, "Starting phone hotspot...");
        Label(wifi_ap_, "");
        Label(wifi_url_, "");
        Label(pair_text_, "Starting...");
    } else {
        Label(wifi_step_, wifi_start_failed_ ? "Hotspot unavailable. Try again."
                                           : "Set up using your phone");
        Label(wifi_ap_, "Start the hotspot below.");
        Label(wifi_url_, "Enter your Wi-Fi in the phone browser.");
        Label(pair_text_, "重新配网");
    }
}

void PetControls::Update(int64_t now, int battery, bool charging, bool discharging) {
    if (!can_open_()) {
        Hide();
        return;
    }
    if (!open_) {
        return;
    }
    if (wifi_pending_until_ && now >= wifi_pending_until_) {
        wifi_pending_until_ = 0;
        wifi_start_failed_ = true;
        next_status_ = 0;
    }
    if (now >= next_status_) {
        next_status_ = now + 1000000;
        RefreshStatus(battery, charging, discharging);
    }
    if (page_ == Page::Signals && now >= next_mic_) {
        next_mic_ = now + 100000;
        const int level = static_cast<int>(std::clamp(audio_input_level(), 0.0f, 1.0f) * 100.0f);
        lv_bar_set_value(mic_level_, level, LV_ANIM_OFF);
    }
}
