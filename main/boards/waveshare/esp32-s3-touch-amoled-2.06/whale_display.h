#pragma once

#include "application.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <array>
#include <cstring>
#include <functional>
#include <string>

// Board-local UI. All mutable state uses the same recursive lock as LVGL.
class WhaleDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    ~WhaleDisplay() override {
        DisplayLockGuard lock(this);
        if (state_timer_ != nullptr) {
            lv_timer_delete(state_timer_);
        }
        if (action_menu_ != nullptr) {
            lv_obj_delete(action_menu_);
        }
    }

    void SetTouchCallback(std::function<void()> callback) {
        DisplayLockGuard lock(this);
        on_touch_ = std::move(callback);
    }

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(emoji_box_, kAnimationViewport, kAnimationViewport);
        lv_image_set_pivot(emoji_image_, LV_PCT(50), LV_PCT(50));
        lv_image_set_scale(emoji_image_, kCharacterScale);
        lv_obj_set_ext_click_area(emoji_image_, kTouchPadding);
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            emoji_image_,
            [](lv_event_t* event) {
                auto self = static_cast<WhaleDisplay*>(lv_event_get_user_data(event));
                self->HandleTouch();
            },
            LV_EVENT_SHORT_CLICKED, this);
        lv_obj_add_event_cb(
            emoji_image_,
            [](lv_event_t* event) {
                auto self = static_cast<WhaleDisplay*>(lv_event_get_user_data(event));
                self->OpenActionMenu();
            },
            LV_EVENT_LONG_PRESSED, this);
        CreateActionMenu();
        state_timer_ = lv_timer_create(
            [](lv_timer_t* timer) {
                auto self = static_cast<WhaleDisplay*>(lv_timer_get_user_data(timer));
                self->RefreshState();
            },
            100, this);
        ESP_LOGI("WhaleUI", "Character scale: %u/256; viewport: %dpx", kCharacterScale,
                 kAnimationViewport);
        ESP_LOGI("WhaleUI", "Action library ready: 10 actions; long press opens menu");
    }

    void SetTheme(Theme* theme) override {
        DisplayLockGuard lock(this);
        rendered_emotion_.clear();
        failed_animation_.clear();
        SpiLcdDisplay::SetTheme(theme);
        UpdateMenuTheme();
    }

    void SetEmotion(const char* emotion) override {
        DisplayLockGuard lock(this);
        requested_emotion_ = emotion == nullptr ? "neutral" : emotion;
        RenderState();
    }

    void SetChatMessage(const char* role, const char* content) override {
        SpiLcdDisplay::SetChatMessage(role, content);
        DisplayLockGuard lock(this);
        if (role != nullptr && content != nullptr && content[0] != '\0') {
            if (std::strcmp(role, "user") == 0) {
                manual_action_ = -1;
                CloseActionMenu();
                waiting_for_reply_ = true;
                waiting_since_ = lv_tick_get();
            } else if (std::strcmp(role, "assistant") == 0) {
                waiting_for_reply_ = false;
            }
        }
        RenderState();
    }

private:
    struct Action {
        const char* key;
        const char* label;
    };
    inline static constexpr std::array<Action, 10> kActions = {{{"w024", "待机呼吸"},
                                                                {"w019", "工作思考"},
                                                                {"w031", "开心跃动"},
                                                                {"w001", "东张西望"},
                                                                {"w006", "小憩沉眠"},
                                                                {"w027", "打盹醒来"},
                                                                {"w029", "傲娇生气"},
                                                                {"w030", "害羞"},
                                                                {"w035", "叹气"},
                                                                {"w043", "吓一跳"}}};
    struct ActionButtonContext {
        WhaleDisplay* display;
        size_t index;
    };
    static constexpr uint32_t kCharacterScale = 430;
    static constexpr int32_t kAnimationViewport = 272;
    static constexpr int32_t kTouchPadding = 24;
    static constexpr uint32_t kTouchDurationMs = 10000;
    static constexpr uint32_t kActionDurationMs = 10200;
    static constexpr uint32_t kReplyTimeoutMs = 30000;
    lv_timer_t* state_timer_ = nullptr;
    lv_obj_t* action_menu_ = nullptr;
    lv_obj_t* action_panel_ = nullptr;
    std::array<ActionButtonContext, kActions.size()> action_buttons_{};
    std::function<void()> on_touch_;
    std::string requested_emotion_ = "neutral";
    std::string rendered_emotion_;
    std::string failed_animation_;
    bool touch_active_ = false;
    bool waiting_for_reply_ = false;
    bool menu_open_ = false;
    int manual_action_ = -1;
    uint32_t touch_started_ = 0;
    uint32_t waiting_since_ = 0;
    uint32_t action_started_ = 0;
    unsigned touch_count_ = 0;

    void ApplyMenuFont(lv_obj_t* object, const lv_font_t* font) {
        lv_obj_set_style_text_font(object, font, 0);
        const auto count = lv_obj_get_child_count(object);
        for (uint32_t i = 0; i < count; ++i) {
            ApplyMenuFont(lv_obj_get_child(object, i), font);
        }
    }

    void UpdateMenuTheme() {
        if (action_panel_ == nullptr || current_theme_ == nullptr) {
            return;
        }
        auto theme = static_cast<LvglTheme*>(current_theme_);
        lv_obj_set_style_bg_color(action_panel_, theme->background_color(), 0);
        lv_obj_set_style_text_color(action_panel_, theme->text_color(), 0);
        ApplyMenuFont(action_panel_, theme->text_font()->font());
    }

    void CreateActionMenu() {
        action_menu_ = lv_obj_create(lv_obj_get_screen(emoji_box_));
        lv_obj_set_size(action_menu_, LV_PCT(100), LV_PCT(100));
        lv_obj_center(action_menu_);
        lv_obj_remove_flag(action_menu_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(action_menu_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(action_menu_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(action_menu_, 0, 0);
        lv_obj_set_style_pad_all(action_menu_, 0, 0);
        action_panel_ = lv_obj_create(action_menu_);
        lv_obj_set_size(action_panel_, LV_PCT(88), LV_PCT(86));
        lv_obj_center(action_panel_);
        lv_obj_remove_flag(action_panel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(action_panel_, 12, 0);
        lv_obj_set_flex_flow(action_panel_, LV_FLEX_FLOW_COLUMN);
        auto title = lv_label_create(action_panel_);
        lv_label_set_text(title, "选个动作");
        auto list = lv_obj_create(action_panel_);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_pad_all(list, 4, 0);
        for (size_t i = 0; i < kActions.size(); ++i) {
            action_buttons_[i] = {this, i};
            auto button = lv_button_create(list);
            lv_obj_set_size(button, LV_PCT(100), 52);
            auto label = lv_label_create(button);
            lv_label_set_text(label, kActions[i].label);
            lv_obj_center(label);
            lv_obj_add_event_cb(
                button,
                [](lv_event_t* event) {
                    auto context = static_cast<ActionButtonContext*>(lv_event_get_user_data(event));
                    context->display->BeginAction(context->index);
                },
                LV_EVENT_SHORT_CLICKED, &action_buttons_[i]);
        }
        auto close = lv_button_create(action_panel_);
        lv_obj_set_size(close, LV_PCT(100), 48);
        auto label = lv_label_create(close);
        lv_label_set_text(label, "关闭");
        lv_obj_center(label);
        lv_obj_add_event_cb(
            close,
            [](lv_event_t* event) {
                auto self = static_cast<WhaleDisplay*>(lv_event_get_user_data(event));
                self->CloseActionMenu();
                self->RenderState();
            },
            LV_EVENT_SHORT_CLICKED, this);
        UpdateMenuTheme();
        lv_obj_add_flag(action_menu_, LV_OBJ_FLAG_HIDDEN);
    }

    void OpenActionMenu() {
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle || waiting_for_reply_) {
            ShowNotification("对话结束后再选动作");
            return;
        }
        if (on_touch_) {
            on_touch_();
        }
        manual_action_ = -1;
        touch_active_ = false;
        menu_open_ = true;
        lv_obj_remove_flag(action_menu_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(action_menu_);
        ESP_LOGI("WhaleUI", "Action menu opened");
    }

    void CloseActionMenu() {
        if (!menu_open_) {
            return;
        }
        menu_open_ = false;
        lv_obj_add_flag(action_menu_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI("WhaleUI", "Action menu closed");
    }

    void BeginAction(size_t index) {
        if (index >= kActions.size() ||
            Application::GetInstance().GetDeviceState() != kDeviceStateIdle || waiting_for_reply_) {
            CloseActionMenu();
            return;
        }
        if (on_touch_) {
            on_touch_();
        }
        manual_action_ = static_cast<int>(index);
        action_started_ = lv_tick_get();
        touch_active_ = false;
        rendered_emotion_.clear();
        CloseActionMenu();
        ShowNotification(kActions[index].label, 2500);
        ESP_LOGI("WhaleUI", "Manual action: %s", kActions[index].key);
        RenderState();
    }

    void HandleTouch() {
        if (on_touch_) {
            on_touch_();
        }
        manual_action_ = -1;
        touch_active_ = true;
        touch_started_ = lv_tick_get();
        rendered_emotion_.clear();
        ++touch_count_;
        ESP_LOGI("WhaleUI", "Touch %u: happy animation", touch_count_);
        RenderState();
    }

    void RefreshState() {
        const auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle) {
            if (manual_action_ >= 0) {
                ESP_LOGI("WhaleUI", "Manual action interrupted by device state");
            }
            manual_action_ = -1;
            CloseActionMenu();
        }
        if (state == kDeviceStateIdle || state == kDeviceStateSpeaking ||
            state == kDeviceStateWifiConfiguring ||
            lv_tick_elaps(waiting_since_) >= kReplyTimeoutMs) {
            waiting_for_reply_ = false;
        }
        if (manual_action_ >= 0 && lv_tick_elaps(action_started_) >= kActionDurationMs) {
            manual_action_ = -1;
            ESP_LOGI("WhaleUI", "Manual action finished");
        }
        if (touch_active_ && lv_tick_elaps(touch_started_) >= kTouchDurationMs) {
            touch_active_ = false;
        }
        RenderState();
    }

    const char* EmotionAction() const {
        if (requested_emotion_ == "happy" || requested_emotion_ == "laughing" ||
            requested_emotion_ == "loving" || requested_emotion_ == "funny") {
            return "w031";
        }
        if (requested_emotion_ == "angry")
            return "w029";
        if (requested_emotion_ == "embarrassed")
            return "w030";
        if (requested_emotion_ == "sad" || requested_emotion_ == "crying")
            return "w035";
        if (requested_emotion_ == "surprised" || requested_emotion_ == "shocked")
            return "w043";
        if (requested_emotion_ == "sleepy")
            return "w006";
        return "w024";
    }

    void RenderState() {
        if (emoji_image_ == nullptr || menu_open_) {
            return;
        }
        const char* key = EmotionAction();
        if (waiting_for_reply_) {
            key = "w019";
        } else if (manual_action_ >= 0) {
            key = kActions[manual_action_].key;
        } else if (touch_active_) {
            key = "w031";
        } else if (requested_emotion_ == "thinking") {
            key = "w019";
        }
        auto theme = static_cast<LvglTheme*>(current_theme_);
        auto collection = theme == nullptr ? nullptr : theme->emoji_collection();
        if (collection == nullptr || collection->GetEmojiImage(key) == nullptr ||
            failed_animation_ == key) {
            key = "neutral";
        }
        if (rendered_emotion_ == key) {
            return;
        }
        SpiLcdDisplay::SetEmotion(key);
        auto image = collection == nullptr ? nullptr : collection->GetEmojiImage(key);
        if (image != nullptr && image->IsGif() &&
            (gif_controller_ == nullptr || !gif_controller_->IsLoaded())) {
            failed_animation_ = key;
            ESP_LOGW("WhaleUI", "Animation unavailable: %s; returning to neutral", key);
            key = "neutral";
            SpiLcdDisplay::SetEmotion(key);
        }
        rendered_emotion_ = key;
        ESP_LOGI("WhaleUI", "Animation: %s", key);
    }
};
