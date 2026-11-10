#pragma once

#include <atomic>
#include <cstdint>

namespace pet_totem_audio {

inline constexpr int64_t kStartDeadlineUs = 100000;
// After a TNT blast the totem opens on the 0.4 s black hold while the TNT
// soundtrack tail drains. Wait for that queue, then start the totem track with
// a matching sample offset so its local 1.86 s burst keeps the screen time.
inline constexpr int64_t kLinkWaitUs = 450000;
inline constexpr int64_t kLinkDeadlineUs = 500000;

// Sample offset for a late start: start the track at the point it would have
// reached at show_start_us. 0 when on time; capped at the link wait window.
inline int LinkOffsetMs(int64_t now_us, int64_t show_start_us, int64_t max_us = kLinkWaitUs) {
    const int64_t delta = now_us > show_start_us ? now_us - show_start_us : 0;
    const int64_t clamped = delta < max_us ? delta : max_us;
    return static_cast<int>(clamped / 1000);
}

enum class Reject {
    kNone,
    kMissing,
    kBusy,
    kPlaybackBusy,
    kNotIdle,
    kLate,
};

// One totem track is accepted only when the asset is present, no other totem
// track owns the slot, playback is idle, the device is still idle, and the
// worker reached the audio start before the in-point deadline.
inline Reject Decide(bool available, bool another_track_running, bool playback_idle,
                     bool idle_state, int64_t late_us, int64_t deadline_us = kStartDeadlineUs) {
    if (!available) {
        return Reject::kMissing;
    }
    if (another_track_running) {
        return Reject::kBusy;
    }
    if (!playback_idle) {
        return Reject::kPlaybackBusy;
    }
    if (!idle_state) {
        return Reject::kNotIdle;
    }
    if (late_us > deadline_us) {
        return Reject::kLate;
    }
    return Reject::kNone;
}

// Single-owner gate. Begin() succeeds once; Cancel() invalidates the current
// show immediately, while the worker still holds the running slot until
// Finish(). A new show therefore cannot overlap an old cancelled track, and
// the old callback can never continue after its id is gone.
class Gate {
public:
    bool Begin(uint32_t id) {
        if (running_.exchange(true)) {
            return false;
        }
        active_.store(id);
        cancelled_.store(false);
        return true;
    }

    bool Continue(uint32_t id) const {
        return running_.load() && active_.load() == id && !cancelled_.load();
    }

    void Cancel(uint32_t id) {
        if (active_.load() == id) {
            cancelled_.store(true);
            active_.store(0);
        }
    }

    void Finish(uint32_t id) {
        if (active_.load() == id) {
            active_.store(0);
        }
        running_.store(false);
    }

    bool Running() const { return running_.load(); }
    uint32_t ActiveId() const { return active_.load(); }

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<uint32_t> active_{0};
};

}  // namespace pet_totem_audio
