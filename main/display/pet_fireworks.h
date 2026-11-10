#pragma once

#include <lvgl.h>
#include <cstdint>
#include <memory>

#include "pet_fireworks_camera.h"
#include "pet_fireworks_layout.h"

namespace PetMedia {
class Clip;
struct Request;
}  // namespace PetMedia

class PetFireworks {
public:
    enum class Stage { Idle, LeadIn, Flight, Burst, Return };

    ~PetFireworks();
    bool Start(lv_obj_t* face, int64_t now);
    Stage Update(int64_t now);
    bool IsActive() const { return stage_ != Stage::Idle; }
    float ReturnProgress(int64_t now) const;
    bool IsCreeper() const { return effect_ == 0; }

private:
    // One LVGL invalidation area per streak plus the rocket cluster. LVGL's
    // default invalid-area buffer holds 32 entries; staying below it keeps a
    // flight tick local instead of falling back to a full-screen transfer.
    static constexpr int kStreakBudget = 28;

    void Finish();
    void DrawFlight(int64_t age);
    void DrawBurst(int64_t age);
    void ClearStreaks();
    void Invalidate(const pet_fireworks::Rect& rect);

    lv_obj_t* face_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    uint16_t* pixels_ = nullptr;
    pet_fireworks::Camera camera_;
    pet_fireworks::Rect previous_;
    pet_fireworks::Rect streaks_[kStreakBudget] = {};
    uint16_t* frame_ = nullptr;
    const PetMedia::Clip* playback_ = nullptr;
    std::shared_ptr<PetMedia::Request> sd_request_;
    std::shared_ptr<PetMedia::Clip> sd_clip_;
    int64_t stage_start_ = 0;
    int64_t next_frame_ = 0;
    Stage stage_ = Stage::Idle;
    uint8_t effect_ = 0;
};
