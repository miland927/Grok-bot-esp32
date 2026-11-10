#pragma once

#include "pet_face_profile.h"
#include "pet_firework_selection.h"
#include "pet_totem_texture.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace PetMedia {
inline constexpr int kWidth = 160;
inline constexpr int kHeight = 160;
inline constexpr int kFrameCount = 70;
inline constexpr int kFrameMs = 50;
inline constexpr size_t kMaxClipBytes = 512 * 1024;

class Clip {
public:
    Clip() = default;
    Clip(const Clip&) = delete;
    Clip& operator=(const Clip&) = delete;
    ~Clip();
    std::span<uint8_t> Allocate(size_t size);
    bool Validate(std::span<const uint8_t> data);
    void DecodeFrame(size_t index, uint16_t* rgb565) const;
    std::string_view Audio() const;

private:
    uint8_t* storage_ = nullptr;
    std::span<const uint8_t> data_;
    std::array<size_t, kFrameCount> frames_{};
    size_t audio_offset_ = 0;
};

struct Request {
    explicit Request(unsigned roll) : roll(roll) {}

    const unsigned roll;
    // The worker publishes the selected effect and clip before setting done.
    unsigned effect = kCreeper;
    std::shared_ptr<Clip> clip;
    std::atomic<bool> done{false};
};

struct FaceRequest {
    PetFaceProfile profile;
    bool valid = false;
    std::atomic<bool> done{false};
};

// TNT is drawn in real time. The SD pack contains one 16x16 RGB565 face and
// the already synchronized Opus mix, with no animation frames.
class TntClip {
public:
    static constexpr size_t kTexturePixels = 16 * 16;
    TntClip() = default;
    TntClip(const TntClip&) = delete;
    TntClip& operator=(const TntClip&) = delete;
    ~TntClip();
    std::span<uint8_t> Allocate(size_t size);
    bool Validate(std::span<const uint8_t> bytes);
    std::array<uint16_t, kTexturePixels> texture{};
    std::string_view audio;

private:
    uint8_t* storage_ = nullptr;
};

struct TntRequest {
    std::shared_ptr<TntClip> clip;
    std::atomic<bool> done{false};
};

// /grokbot/totem/totem.gra: 16-byte TTA1 header, RGB565 pixels, one-bit mask,
// Ogg Opus. The pack is optional; the firmware keeps a built-in sprite/track.
inline constexpr size_t kMaxTotemBytes = 96 * 1024;
inline constexpr size_t kMaxTotemAudioBytes = 48 * 1024;

class TotemPack {
public:
    TotemPack() = default;
    TotemPack(const TotemPack&) = delete;
    TotemPack& operator=(const TotemPack&) = delete;
    ~TotemPack();

    std::span<uint8_t> Allocate(size_t size);
    bool Validate(std::span<const uint8_t> data);
    pet_totem::Texture Texture() const;
    std::string_view Audio() const;
    bool HasAudio() const { return audio_offset_ != 0 && audio_offset_ < data_.size(); }

private:
    uint8_t* storage_ = nullptr;
    std::span<const uint8_t> data_;
    size_t audio_offset_ = 0;
    int width_ = 0;
    int height_ = 0;
    int mask_stride_ = 0;
};

struct TotemRequest {
    std::shared_ptr<TotemPack> pack;
    std::atomic<bool> done{false};
};

std::shared_ptr<FaceRequest> LoadFaceProfile();
std::shared_ptr<Request> LoadFirework(unsigned roll);
std::shared_ptr<TntRequest> LoadTnt();
std::shared_ptr<TotemRequest> LoadTotem();
}  // namespace PetMedia
