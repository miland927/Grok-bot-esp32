#pragma once

#include <cstdint>

#include "pet_fireworks_layout.h"

class PetTnt {
public:
    enum class Stage {
        Idle,
        Agitated,
        Morph,
        Charge1,
        Charge2,
        Charge3,
        Hold,
        Blast,
        Smoke,
        Recover
    };

    struct Pose {
        float face_alpha = 0;
        float face_scale = 1;
        float eye_height = 1;
        float crate_alpha = 0;
        float zoom = 1;
        float squash = 0;
        float x = 0;
        float y = 0;
        float flash = 0;
        float pressure = 0;
    };

    bool Tap(int64_t now);
    void CancelTaps() { taps_ = 0; }
    // Stop the current show without a face-return beat, used when the TNT
    // aftermath hands the stage to the totem.
    void Abort() {
        start_ = -1;
        stage_ = Stage::Idle;
    }
    Stage Update(int64_t now, bool idle);
    bool IsActive() const { return start_ >= 0; }
    unsigned PendingTaps() const { return taps_; }
    uint32_t ShowId() const { return show_id_; }
    float Progress(int64_t now) const;
    static Pose Sample(Stage stage, float progress);
    static pet_fireworks::Rect Bounds(Stage stage, const Pose& pose, int width, int height);
    static void Draw(uint16_t* pixels, int width, int height, Stage stage, float progress,
                     const Pose& pose, const uint16_t* texture = nullptr);

private:
    int64_t start_ = -1;
    int64_t first_tap_ = 0;
    int64_t last_tap_ = 0;
    unsigned taps_ = 0;
    uint32_t show_id_ = 0;
    Stage stage_ = Stage::Idle;
};
