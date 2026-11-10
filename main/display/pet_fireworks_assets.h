#pragma once

#if __has_include("../../local-only/grok-private-headers/pet_fireworks_assets.h")
#include "../../local-only/grok-private-headers/pet_fireworks_assets.h"
#else
#include <cstdint>

namespace pet_fireworks::assets {
inline constexpr int kRocketWidth = 8;
inline constexpr int kRocketHeight = 13;
inline constexpr uint16_t kRocketPixels[kRocketHeight][kRocketWidth] = {
    {0x0000, 0x0000, 0x0000, 0xefff, 0x75f7, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0x1abb, 0x1abb, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0x1abb, 0x1abb, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x0000, 0x0000},
    {0x0000, 0x43d5, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x43d5, 0x0000},
    {0x43d5, 0x43d5, 0xefff, 0xefff, 0x75f7, 0x75f7, 0x43d5, 0x43d5},
    {0x43d5, 0x0000, 0x0000, 0xfd48, 0xfd48, 0x0000, 0x0000, 0x43d5},
    {0x0000, 0x0000, 0x0000, 0xfd48, 0xfd48, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x0000, 0xfd48, 0x0000, 0x0000, 0x0000}
};
inline constexpr uint8_t kRocketMask[kRocketHeight] = {
    0x18, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x7e, 0xff, 0x99, 0x18, 0x10,
};

struct Streak {
    float x;
    float start;
    float depth;
};

// random.Random(4831) uniform table from the approved preview.
inline constexpr Streak kStreaks[] = {
    {253.7674f, 375.2575f, 0.4086f},
    {384.5478f, 203.0973f, 0.7198f},
    {119.9617f, 74.9764f, 0.9368f},
    {153.3754f, 16.6514f, 0.5374f},
    {40.0820f, 175.5576f, 0.5381f},
    {188.8570f, 140.9190f, 0.3815f},
    {228.6969f, 155.7299f, 0.5675f},
    {109.8571f, 177.8667f, 0.6175f},
    {37.6425f, 117.7843f, 0.8786f},
    {286.7772f, 240.2282f, 0.4215f},
    {145.6402f, 270.9580f, 0.9776f},
    {251.3081f, 180.2966f, 0.9213f},
    {151.5690f, 498.5505f, 0.4302f},
    {56.4702f, 319.0373f, 0.3055f},
    {214.8747f, 452.8181f, 0.6276f},
    {150.9089f, 305.4603f, 0.9166f},
    {209.5830f, 508.7760f, 0.3551f},
    {132.7581f, 563.4536f, 0.9099f},
    {18.1493f, 9.7435f, 0.6320f},
    {331.5523f, 113.1516f, 0.8887f},
    {14.5651f, 134.8004f, 0.4402f},
    {270.8267f, 471.6841f, 0.7825f},
    {372.6488f, 70.3740f, 0.7761f},
    {172.6414f, 545.5911f, 0.9939f},
    {265.2240f, 280.7229f, 0.5517f},
    {271.2927f, 179.6069f, 0.3064f},
    {70.0482f, 340.2905f, 0.4551f},
    {175.1094f, 259.4206f, 0.9132f},
    {95.3104f, 554.5655f, 0.7776f},
    {370.0849f, 321.0680f, 0.3205f},
    {20.4785f, 45.5147f, 0.8946f},
    {369.6350f, 360.1496f, 0.9517f},
    {396.6023f, 459.3256f, 0.7223f},
    {140.8723f, 321.4826f, 0.8619f},
    {239.9353f, 224.4173f, 0.5159f},
    {257.3449f, 506.8468f, 0.8286f},
    {300.9391f, 513.3487f, 0.5284f},
    {246.7574f, 358.5887f, 0.3600f},
    {175.6911f, 121.3125f, 0.7388f},
    {181.8726f, 128.2161f, 0.3647f},
    {360.7160f, 60.4411f, 0.3692f},
    {107.6241f, 2.5495f, 0.5137f},
    {160.6253f, 88.3105f, 0.5800f},
};
inline constexpr int kStreakCount = 43;

// Per-effect ceiling that keeps the largest burst footprint on panel.
inline constexpr float kBurstFitZoom[] = {
    3.3938f, 3.3938f, 3.1015f, 2.9761f, 3.3938f, 2.9552f, 3.3938f, 3.3938f,
};
static_assert(sizeof(kBurstFitZoom) / sizeof(kBurstFitZoom[0]) == 8,
              "one burst fit zoom per effect");

}  // namespace pet_fireworks::assets

#endif
