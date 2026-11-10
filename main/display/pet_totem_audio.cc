#include "pet_totem_audio.h"

#include "application.h"
#include "audio/audio_service.h"
#include "pet_show_state.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <new>
#include <utility>

namespace {
constexpr char kTag[] = "PetTotemAudio";
constexpr size_t kMaxTrackBytes = 64 * 1024;

bool CanPlay() {
    return CanRunPetShow(Application::GetInstance().GetDeviceState());
}

const char* Reason(pet_totem_audio::Reject reason) {
    switch (reason) {
        case pet_totem_audio::Reject::kMissing:
            return "missing asset";
        case pet_totem_audio::Reject::kBusy:
            return "another totem track";
        case pet_totem_audio::Reject::kPlaybackBusy:
            return "playback busy";
        case pet_totem_audio::Reject::kNotIdle:
            return "device busy";
        case pet_totem_audio::Reject::kLate:
            return "past in-point";
        case pet_totem_audio::Reject::kNone:
            break;
    }
    return "none";
}

struct TaskArgs {
    std::shared_ptr<pet_totem_audio::Gate> gate;
    TotemAudioSource source;
    uint32_t show_id = 0;
    int64_t show_start_us = 0;
    bool link = false;
};

void Task(void* context) {
    auto* args = static_cast<TaskArgs*>(context);
    {
        std::unique_ptr<TaskArgs> holder(args);
        auto& audio = Application::GetInstance().GetAudioService();
        // Linked entry: the TNT tail may still own the playback queue. Wait in
        // the black hold for it to drain, then start with a sample offset.
        if (args->link) {
            const int64_t deadline = args->show_start_us + pet_totem_audio::kLinkWaitUs;
            while (esp_timer_get_time() < deadline && !audio.IsPlaybackIdle()) {
                if (!args->gate->Continue(args->show_id) || !CanPlay()) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        const int64_t now = esp_timer_get_time();
        const int64_t late_us = now - args->show_start_us;
        const int64_t accept_us =
            args->link ? pet_totem_audio::kLinkDeadlineUs : pet_totem_audio::kStartDeadlineUs;
        const auto reject = pet_totem_audio::Decide(true, false, audio.IsPlaybackIdle(), CanPlay(),
                                                    late_us, accept_us);
        if (reject != pet_totem_audio::Reject::kNone) {
            ESP_LOGI(kTag, "Totem sound skipped: %s", Reason(reject));
        } else if (!args->gate->Continue(args->show_id)) {
            ESP_LOGI(kTag, "Totem sound cancelled before start");
        } else {
            const int offset_ms =
                args->link ? pet_totem_audio::LinkOffsetMs(now, args->show_start_us) : 0;
            ESP_LOGI(kTag, "Totem sound started (show %u, %u bytes, offset %d ms)",
                     static_cast<unsigned>(args->show_id),
                     static_cast<unsigned>(args->source.ogg.size()), offset_ms);
            audio.PlaySound(
                args->source.ogg,
                [gate = args->gate, id = args->show_id] { return gate->Continue(id) && CanPlay(); },
                offset_ms);
            ESP_LOGI(kTag, "Totem sound finished (show %u)", static_cast<unsigned>(args->show_id));
        }
        args->gate->Finish(args->show_id);
    }
    vTaskDelete(nullptr);
}
}  // namespace

PetTotemAudio::PetTotemAudio() : gate_(std::make_shared<pet_totem_audio::Gate>()) {}

PetTotemAudio::~PetTotemAudio() { Cancel(); }

bool PetTotemAudio::Start(uint32_t show_id, int64_t show_start_us, TotemAudioSource source,
                          bool link) {
    if (source.ogg.size() <= 4 || source.ogg.size() > kMaxTrackBytes ||
        std::memcmp(source.ogg.data(), "OggS", 4) != 0) {
        ESP_LOGI(kTag, "Totem sound skipped: missing asset");
        return false;
    }
    if (!gate_->Begin(show_id)) {
        ESP_LOGI(kTag, "Totem sound skipped: another totem track");
        return false;
    }
    auto* args =
        new (std::nothrow) TaskArgs{gate_, std::move(source), show_id, show_start_us, link};
    if (!args || xTaskCreate(Task, "pet_totem_audio", 4096, args, 2, nullptr) != pdPASS) {
        delete args;
        gate_->Finish(show_id);
        ESP_LOGW(kTag, "Could not start totem sound task");
        return false;
    }
    current_id_ = show_id;
    return true;
}

void PetTotemAudio::Cancel() {
    if (current_id_ != 0) {
        gate_->Cancel(current_id_);
        current_id_ = 0;
    }
}

bool PetTotemAudio::Active() const { return gate_->Running(); }
