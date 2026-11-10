#include "pet_tnt_audio.h"

#include "application.h"
#include "audio/audio_service.h"
#include "pet_media.h"
#include "pet_show_state.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

namespace {
constexpr char kTag[] = "PetTntAudio";
constexpr int64_t kPlaybackDrainUs = 320000;

bool CanPlay() {
    return CanRunPetShow(Application::GetInstance().GetDeviceState());
}

const char* Reason(pet_tnt_audio::Reject reason) {
    switch (reason) {
        case pet_tnt_audio::Reject::kMissing:
            return "missing asset";
        case pet_tnt_audio::Reject::kBusy:
            return "another TNT track";
        case pet_tnt_audio::Reject::kPlaybackBusy:
            return "playback busy";
        case pet_tnt_audio::Reject::kNotIdle:
            return "device busy";
        case pet_tnt_audio::Reject::kLate:
            return "past in-point";
        case pet_tnt_audio::Reject::kNone:
            break;
    }
    return "none";
}

struct TaskArgs {
    std::shared_ptr<pet_tnt_audio::Gate> gate;
    std::shared_ptr<const PetMedia::TntClip> clip;
    uint32_t show_id = 0;
    int64_t show_start_us = 0;
    int end_offset_ms = 0;
};

void Task(void* context) {
    auto* args = static_cast<TaskArgs*>(context);
    {
        std::unique_ptr<TaskArgs> holder(args);
        auto& audio = Application::GetInstance().GetAudioService();
        // Rapid taps can leave the first tap's chirp queued at show start.
        // Let it finish and skip the elapsed soundtrack samples.
        const bool was_busy = !audio.IsPlaybackIdle();
        const int64_t start_window_us = was_busy ? kPlaybackDrainUs
                                                  : pet_tnt_audio::kStartDeadlineUs;
        const int64_t deadline = args->show_start_us + start_window_us;
        while (was_busy && esp_timer_get_time() < deadline && !audio.IsPlaybackIdle() &&
               args->gate->Continue(args->show_id) && CanPlay()) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        const int64_t late_us = esp_timer_get_time() - args->show_start_us;
        const auto reject =
            pet_tnt_audio::Decide(true, false, audio.IsPlaybackIdle(), CanPlay(), late_us,
                                  start_window_us);
        if (reject != pet_tnt_audio::Reject::kNone) {
            ESP_LOGI(kTag, "TNT sound skipped: %s", Reason(reject));
        } else if (!args->gate->Continue(args->show_id)) {
            ESP_LOGI(kTag, "TNT sound cancelled before start");
        } else {
            ESP_LOGI(kTag, "TNT sound started (show %u)", static_cast<unsigned>(args->show_id));
            audio.PlaySound(
                args->clip->audio,
                [gate = args->gate, id = args->show_id] { return gate->Continue(id) && CanPlay(); },
                late_us > 0 ? static_cast<int>(late_us / 1000) : 0, args->end_offset_ms);
            ESP_LOGI(kTag, "TNT sound finished (show %u)", static_cast<unsigned>(args->show_id));
        }
        args->gate->Finish(args->show_id);
    }
    vTaskDelete(nullptr);
}
}  // namespace

PetTntAudio::PetTntAudio() : gate_(std::make_shared<pet_tnt_audio::Gate>()) {}

PetTntAudio::~PetTntAudio() { Cancel(); }

bool PetTntAudio::Start(uint32_t show_id, int64_t show_start_us,
                        std::shared_ptr<const PetMedia::TntClip> clip, int end_offset_ms) {
    if (!clip) {
        ESP_LOGI(kTag, "TNT sound skipped: missing asset");
        return false;
    }
    if (!gate_->Begin(show_id)) {
        ESP_LOGI(kTag, "TNT sound skipped: another TNT track");
        return false;
    }
    auto* args =
        new (std::nothrow) TaskArgs{gate_, std::move(clip), show_id, show_start_us, end_offset_ms};
    if (!args || xTaskCreate(Task, "pet_tnt_audio", 4096, args, 2, nullptr) != pdPASS) {
        delete args;
        gate_->Finish(show_id);
        ESP_LOGW(kTag, "Could not start TNT sound task");
        return false;
    }
    current_id_ = show_id;
    return true;
}

void PetTntAudio::Cancel() {
    if (current_id_ != 0) {
        gate_->Cancel(current_id_);
        current_id_ = 0;
    }
}

bool PetTntAudio::Active() const { return gate_->Running(); }
