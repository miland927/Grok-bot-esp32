#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace PetMedia {
// Keep the card filenames and numeric slots compatible with the existing GRA library.
inline constexpr std::array kFireworkEffects = {"creeper", "star", "sphere", "burst",
                                               "rings", "willow", "heart", "snowflake"};
inline constexpr std::array kFireworkNames = {"creeper", "star", "sphere", "burst",
                                             "lan", "willow", "nessie", "snowflake"};
inline constexpr unsigned kCreeper = 0;
inline constexpr unsigned kRandomFirework = kFireworkEffects.size();

struct FireworkSelection {
    static constexpr size_t kMaxBytes = 512;
    std::array<uint8_t, kFireworkEffects.size()> weights{10, 14, 14, 14, 10, 14, 10, 14};
    unsigned test_effect = kRandomFirework;

    unsigned Pick(unsigned roll) const {
        if (test_effect != kRandomFirework) {
            return test_effect;
        }
        for (unsigned i = 0; i < weights.size(); ++i) {
            if (roll < weights[i]) {
                return i;
            }
            roll -= weights[i];
        }
        return kCreeper;
    }

    static bool Parse(std::string_view json, FireworkSelection& out);
};
}  // namespace PetMedia
