#pragma once

#include <cstdint>
#include <memory>

#include "pet_tnt_audio_logic.h"

namespace PetMedia {
class TntClip;
}

// One background TNT soundtrack request at a time. The task never holds a
// PetDisplay pointer: it owns a shared gate/state until PlaySound returns.
class PetTntAudio {
public:
    PetTntAudio();
    ~PetTntAudio();
    PetTntAudio(const PetTntAudio&) = delete;
    PetTntAudio& operator=(const PetTntAudio&) = delete;

    // Start the track for the show identified by show_id. Returns false when
    // the asset is absent, another TNT track already owns the slot, or task
    // creation fails. The visual show continues in every case.
    bool Start(uint32_t show_id, int64_t show_start_us,
               std::shared_ptr<const PetMedia::TntClip> clip = {}, int end_offset_ms = 0);
    // Voice interruption or the end of the show: the callback stops before the
    // next packet. The worker still finishes the running slot.
    void Cancel();
    bool Active() const;

private:
    std::shared_ptr<pet_tnt_audio::Gate> gate_;
    uint32_t current_id_ = 0;
};
