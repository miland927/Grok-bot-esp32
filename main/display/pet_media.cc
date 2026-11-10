#include "pet_media.h"

#include <algorithm>
#include <cstring>
#include <new>

#include <cJSON.h>
#include <esp_crc.h>
#include <esp_heap_caps.h>

#include "demuxer/ogg_demuxer.h"

namespace PetMedia {

bool FireworkSelection::Parse(std::string_view json, FireworkSelection& out) {
    if (json.empty() || json.size() > kMaxBytes || json.find('\0') != json.npos ||
        json.find('\\') != json.npos) {
        return false;
    }
    const char* end = nullptr;
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), cJSON_Delete);
    if (!cJSON_IsObject(root.get()) ||
        std::string_view(end, json.data() + json.size() - end).find_first_not_of(" \t\r\n") != json.npos) {
        return false;
    }

    FireworkSelection candidate;
    bool version = false;
    bool test = false;
    std::array<bool, kFireworkNames.size()> seen{};
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root.get()) {
        const std::string_view key = item->string;
        if (key == "version") {
            if (version || !cJSON_IsNumber(item) || item->valuedouble != 1) {
                return false;
            }
            version = true;
        } else if (key == "test_effect") {
            if (test || !cJSON_IsString(item)) {
                return false;
            }
            test = true;
            const std::string_view value = item->valuestring;
            if (value != "random") {
                const auto effect = std::find(kFireworkNames.begin(), kFireworkNames.end(), value);
                if (effect == kFireworkNames.end()) {
                    return false;
                }
                candidate.test_effect = effect - kFireworkNames.begin();
            }
        } else {
            const auto effect = std::find(kFireworkNames.begin(), kFireworkNames.end(), key);
            if (effect == kFireworkNames.end() || !cJSON_IsNumber(item)) {
                return false;
            }
            const size_t index = effect - kFireworkNames.begin();
            const double value = item->valuedouble;
            if (seen[index] || !(value >= 0 && value <= 100) || value != static_cast<int>(value)) {
                return false;
            }
            seen[index] = true;
            candidate.weights[index] = static_cast<uint8_t>(value);
        }
    }
    unsigned sum = 0;
    for (auto weight : candidate.weights) {
        sum += weight;
    }
    if (!version || sum != 100) {
        return false;
    }
    out = candidate;
    return true;
}

namespace {
constexpr size_t kHeaderSize = 12;
constexpr size_t kPixels = kWidth * kHeight;
constexpr size_t kMaxAudioBytes = 32 * 1024;
constexpr size_t kTntAudioOffset = 8 + TntClip::kTexturePixels * sizeof(uint16_t);

uint16_t Read16(const uint8_t* data) {
    return data[0] | (uint16_t(data[1]) << 8);
}

uint32_t Read32(const uint8_t* data) {
    return Read16(data) | (uint32_t(Read16(data + 2)) << 16);
}

bool ValidAudio(const uint8_t* audio, size_t size) {
    if (size < 4 || std::memcmp(audio, "OggS", 4) != 0) {
        return false;
    }
    std::unique_ptr<OggDemuxer> demuxer(new (std::nothrow) OggDemuxer);
    if (!demuxer) {
        return false;
    }
    int duration_ms = 0;
    demuxer->Reset();
    demuxer->OnPacket([&duration_ms](const uint8_t*, int, int ms, size_t) { duration_ms += ms; });
    return demuxer->Process(audio, size) == size && demuxer->Finish() && duration_ms >= 3400 &&
           duration_ms <= 3700;
}
}  // namespace

Clip::~Clip() {
    heap_caps_free(storage_);
}

std::span<uint8_t> Clip::Allocate(size_t size) {
    if (storage_ || size == 0 || size > kMaxClipBytes) {
        return {};
    }
    storage_ = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    return storage_ ? std::span(storage_, size) : std::span<uint8_t>();
}

bool Clip::Validate(std::span<const uint8_t> bytes) {
    const auto* data = bytes.data();
    const size_t size = bytes.size();
    if (size <= kHeaderSize || size > kMaxClipBytes || std::memcmp(data, "GRA1", 4) != 0) {
        return false;
    }
    audio_offset_ = Read32(data + 4);
    if (audio_offset_ < kHeaderSize || audio_offset_ >= size || size - audio_offset_ > kMaxAudioBytes ||
        esp_crc32_le(0, data + kHeaderSize, size - kHeaderSize) != Read32(data + 8)) {
        return false;
    }

    size_t pos = kHeaderSize;
    for (auto& start : frames_) {
        start = pos;
        size_t remaining = kPixels;
        while (remaining != 0) {
            if (audio_offset_ - pos < 4) {
                return false;
            }
            const size_t count = Read16(data + pos);
            if (count == 0 || count > remaining) {
                return false;
            }
            remaining -= count;
            pos += 4;
        }
    }
    if (pos != audio_offset_) {
        return false;
    }

    const bool valid = ValidAudio(data + audio_offset_, size - audio_offset_);
    if (valid) {
        data_ = bytes;
    }
    return valid;
}

void Clip::DecodeFrame(size_t index, uint16_t* rgb565) const {
    const size_t end = index + 1 < frames_.size() ? frames_[index + 1] : audio_offset_;
    for (size_t pos = frames_[index]; pos < end; pos += 4) {
        const uint16_t count = Read16(data_.data() + pos);
        const uint16_t color = Read16(data_.data() + pos + 2);
        std::fill_n(rgb565, count, color);
        rgb565 += count;
    }
}

std::string_view Clip::Audio() const {
    const auto audio = data_.subspan(audio_offset_);
    return {reinterpret_cast<const char*>(audio.data()), audio.size()};
}

TntClip::~TntClip() { heap_caps_free(storage_); }

std::span<uint8_t> TntClip::Allocate(size_t size) {
    if (storage_ || size <= kTntAudioOffset || size > kTntAudioOffset + kMaxAudioBytes) {
        return {};
    }
    storage_ = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    return storage_ ? std::span(storage_, size) : std::span<uint8_t>();
}

bool TntClip::Validate(std::span<const uint8_t> bytes) {
    const auto* data = bytes.data();
    const size_t size = bytes.size();
    if (size <= kTntAudioOffset || size > kTntAudioOffset + kMaxAudioBytes ||
        std::memcmp(data, "GTN1", 4) != 0 ||
        esp_crc32_le(0, data + 8, size - 8) != Read32(data + 4) ||
        !ValidAudio(data + kTntAudioOffset, size - kTntAudioOffset)) {
        return false;
    }
    for (size_t i = 0; i < texture.size(); ++i) {
        texture[i] = Read16(data + 8 + i * 2);
    }
    audio = {reinterpret_cast<const char*>(data + kTntAudioOffset), size - kTntAudioOffset};
    return true;
}

TotemPack::~TotemPack() { heap_caps_free(storage_); }

std::span<uint8_t> TotemPack::Allocate(size_t size) {
    if (storage_ || size == 0 || size > kMaxTotemBytes) {
        return {};
    }
    storage_ = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    return storage_ ? std::span(storage_, size) : std::span<uint8_t>();
}

bool TotemPack::Validate(std::span<const uint8_t> bytes) {
    constexpr size_t kHeader = 16;
    const auto* data = bytes.data();
    const size_t size = bytes.size();
    if (size <= kHeader || size > kMaxTotemBytes || std::memcmp(data, "TTA1", 4) != 0) {
        return false;
    }
    const int width = Read16(data + 4);
    const int height = Read16(data + 6);
    if (width < 8 || width > 64 || height < 8 || height > 64) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(width) * height * 2;
    const int stride = (width + 7) / 8;
    const size_t audio_offset = Read32(data + 8);
    if (audio_offset != kHeader + pixels + static_cast<size_t>(stride) * height ||
        audio_offset >= size || size - audio_offset > kMaxTotemAudioBytes) {
        return false;
    }
    if (esp_crc32_le(0, data + kHeader, size - kHeader) != Read32(data + 12)) {
        return false;
    }
    if (size - audio_offset < 4 || std::memcmp(data + audio_offset, "OggS", 4) != 0) {
        return false;
    }
    width_ = width;
    height_ = height;
    mask_stride_ = stride;
    audio_offset_ = audio_offset;
    data_ = bytes;
    return true;
}

pet_totem::Texture TotemPack::Texture() const {
    pet_totem::Texture texture;
    if (data_.empty()) {
        return texture;
    }
    texture.pixels = reinterpret_cast<const uint16_t*>(data_.data() + 16);
    texture.mask = data_.data() + 16 + static_cast<size_t>(width_) * height_ * 2;
    texture.width = width_;
    texture.height = height_;
    texture.mask_stride = mask_stride_;
    return texture;
}

std::string_view TotemPack::Audio() const {
    if (!HasAudio()) {
        return {};
    }
    const auto audio = data_.subspan(audio_offset_);
    return {reinterpret_cast<const char*>(audio.data()), audio.size()};
}
}  // namespace PetMedia
