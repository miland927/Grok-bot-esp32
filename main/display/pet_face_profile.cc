#include "pet_face_profile.h"

#include <cmath>
#include <memory>

#include <cJSON.h>

namespace {
bool ParseColor(const cJSON* item, uint16_t& color) {
    if (!cJSON_IsString(item)) {
        return false;
    }
    const std::string_view text(item->valuestring);
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }
    unsigned rgb = 0;
    for (const char c : text.substr(1)) {
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return false;
        }
        rgb = (rgb << 4) | digit;
    }
    color = ((rgb >> 19) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31);
    return true;
}
}  // namespace

bool PetFaceProfile::Parse(std::string_view json, PetFaceProfile& out) {
    if (json.empty() || json.size() > kMaxBytes || json.find('\0') != json.npos) {
        return false;
    }
    // This profile is flat and uses unescaped ASCII keys and colors.
    const auto open = json.find('{');
    if (open == json.npos || json.find('{', open + 1) != json.npos ||
        json.find_first_of("[\\") != json.npos) {
        return false;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(json.data(), json.size() + 1, nullptr, true), cJSON_Delete);
    if (!cJSON_IsObject(root.get())) {
        return false;
    }

    PetFaceProfile candidate;
    constexpr struct {
        std::string_view name;
        float PetFaceProfile::*member;
        double min;
        double max;
    } floats[] = {
        {"eye_width", &PetFaceProfile::eye_width, 0.06, 0.22},
        {"eye_height", &PetFaceProfile::eye_height, 0.06, 0.25},
        {"eye_spacing", &PetFaceProfile::eye_spacing, 0.20, 0.44},
        {"gaze_x", &PetFaceProfile::gaze_x, 0.0, 0.40},
        {"gaze_y", &PetFaceProfile::gaze_y, 0.0, 0.18},
        {"angry_eye_scale", &PetFaceProfile::angry_eye_scale, 0.30, 1.30},
        {"angry_shake_px", &PetFaceProfile::angry_shake_px, 0.0, 6.0},
        {"angry_shake_hz", &PetFaceProfile::angry_shake_hz, 1.0, 12.0},
    };
    constexpr struct {
        std::string_view name;
        uint32_t PetFaceProfile::*member;
        unsigned min;
        unsigned max;
    } times[] = {
        {"blink_duration_ms", &PetFaceProfile::blink_duration_us, 70, 450},
        {"blink_min_ms", &PetFaceProfile::blink_min_us, 500, 15000},
        {"blink_max_ms", &PetFaceProfile::blink_max_us, 500, 15000},
    };

    // Validate every key and reject duplicates, including unknown version fields.
    bool version_seen = false;
    uint32_t seen = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root.get()) {
        const std::string_view name(item->string);
        if (name == "version") {
            if (version_seen || !cJSON_IsNumber(item) || item->valuedouble != 1) {
                return false;
            }
            version_seen = true;
            continue;
        }

        unsigned index = 0;
        bool found = false;
        const double value = item->valuedouble;
        for (const auto& field : floats) {
            if (name == field.name) {
                if (!cJSON_IsNumber(item) || !std::isfinite(value) ||
                    value < field.min || value > field.max) {
                    return false;
                }
                candidate.*(field.member) = static_cast<float>(value);
                found = true;
                break;
            }
            ++index;
        }
        if (!found) {
            for (const auto& field : times) {
                if (name == field.name) {
                    if (!cJSON_IsNumber(item) || !std::isfinite(value) ||
                        value < field.min || value > field.max || std::floor(value) != value) {
                        return false;
                    }
                    candidate.*(field.member) = static_cast<uint32_t>(value) * 1000;
                    found = true;
                    break;
                }
                ++index;
            }
        }
        if (!found) {
            if (name == "face_color_dark") {
                found = ParseColor(item, candidate.color_dark);
            } else if (name == "face_color_light") {
                ++index;
                found = ParseColor(item, candidate.color_light);
            }
        }
        if (!found || (seen & (1u << index))) {
            return false;
        }
        seen |= 1u << index;
    }
    if (!version_seen || candidate.blink_max_us < candidate.blink_min_us) {
        return false;
    }
    out = candidate;
    return true;
}
