#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "pet_totem_audio_logic.h"

// A soundtrack request owns its bytes. The built-in track is a static flash
// view; a microSD track is a shared_ptr keepalive so the worker cannot read
// freed PSRAM.
struct TotemAudioSource {
    std::string_view ogg;
    std::shared_ptr<const void> keepalive;
};

// One background totem soundtrack request at a time. The task never holds a
// PetDisplay pointer: it owns a shared gate/source until PlaySound returns.
class PetTotemAudio {
public:
    PetTotemAudio();
    ~PetTotemAudio();
    PetTotemAudio(const PetTotemAudio&) = delete;
    PetTotemAudio& operator=(const PetTotemAudio&) = delete;

    // Start the track for the show identified by show_id. Returns false when
    // the source is absent, another totem track owns the slot, or task creation
    // fails. The visual show continues in every case.
    bool Start(uint32_t show_id, int64_t show_start_us, TotemAudioSource source, bool link = false);
    // Voice interruption or the end of the show: the callback stops before the
    // next packet. The worker still finishes the running slot.
    void Cancel();
    bool Active() const;

private:
    std::shared_ptr<pet_totem_audio::Gate> gate_;
    uint32_t current_id_ = 0;
};
