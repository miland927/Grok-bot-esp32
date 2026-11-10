#pragma once

#include <cstdint>

#include "pet_fireworks_layout.h"
#include "pet_totem_assets.h"
#include "pet_totem_texture.h"

// Cinematic totem-of-undying revival, 3.5 s from the trigger.
//
// The module is pure: time in, pose out; pose in, RGB565 out. It owns no LVGL,
// SD, audio or heap state. PetDisplay feeds it the absolute show clock and
// draws the face from the same pose, so a skipped tick cannot desynchronise
// the particles from the face.
class PetTotem {
public:
    enum class Stage {
        Idle,
        Death,
        Summon,
        Awaken,
        Burst,
        Revival,
    };

    struct Pose {
        float time = 0;         // seconds since Start(), for particle phases
        float face_alpha = 1;   // 0 hides the face entirely
        float face_scale = 1;   // multiplier on the face radius
        float face_squash = 0;  // additive squash, positive = flatter
        float face_x = 0;
        float face_y = 0;
        float eye_open = 1;  // multiplier on eye height
        float gaze_x = 0;    // override of the resting gaze
        float gaze_y = 0;
        float dark = 0;  // 0..1 darkens the face into the background
        float zoom = 1;  // camera push-in
        float shake_x = 0;
        float shake_y = 0;
        float totem_alpha = 0;
        float totem_scale = 0;  // screen pixels per texture pixel
        float totem_rot = 0;    // radians
        float totem_y = 0;
        float totem_glow = 0;
        float flash = 0;  // 0..1 full-canvas warm flash
        float ring = 0;   // 0..1 shockwave expansion
        float ring_alpha = 0;
        bool aftermath = false;  // entered after a TNT blast: start black
    };

    // aftermath=true skips the dying-face beat and opens on a black hold,
    // because the TNT show already killed the face.
    void Start(int64_t now, bool aftermath);
    void Cancel();
    bool IsActive() const { return start_ >= 0; }
    bool Aftermath() const { return aftermath_; }
    uint32_t ShowId() const { return show_id_; }

    // Returns Idle after 3.5 s or as soon as the device leaves idle.
    Stage Update(int64_t now, bool idle);
    float Progress(int64_t now) const;

    // Non-owning pixels, copied into the pose-independent texture slot. The
    // caller keeps the pack alive; passing nullptr restores the built-in sprite.
    void SetTexture(const pet_totem::Texture* texture) {
        texture_ = texture != nullptr ? *texture : pet_totem::assets::kTexture;
    }
    const pet_totem::Texture& Texture() const { return texture_; }

    static Pose Sample(Stage stage, float progress, bool aftermath = false);
    static pet_fireworks::Rect Bounds(Stage stage, const Pose& pose, int width, int height);
    static void Draw(uint16_t* pixels, int width, int height, Stage stage, float progress,
                     const Pose& pose, const pet_totem::Texture& texture);

private:
    int64_t start_ = -1;
    uint32_t show_id_ = 0;
    Stage stage_ = Stage::Idle;
    bool aftermath_ = false;
    pet_totem::Texture texture_ = pet_totem::assets::kTexture;
};
