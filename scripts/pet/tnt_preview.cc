#include "pet_tnt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {
using Stage = PetTnt::Stage;
constexpr int kSize = 240;

void Check(bool condition) {
    if (!condition) {
        std::fputs("TNT timeline check failed\n", stderr);
        std::abort();
    }
}

void Trigger(PetTnt& tnt) {
    for (int i = 0; i < 4; ++i) {
        Check(!tnt.Tap(i * 130000));
    }
    Check(tnt.Tap(520000));
}

void CheckTapWindow() {
    PetTnt relaxed;
    for (int i = 0; i < 5; ++i) {
        Check(relaxed.Tap(i * 420000) == (i == 4));
    }

    PetTnt long_gap;
    Check(!long_gap.Tap(0));
    Check(!long_gap.Tap(500000));
    Check(long_gap.PendingTaps() == 1);

    PetTnt long_window;
    for (int i = 0; i < 5; ++i) {
        Check(!long_window.Tap(i * 475000));
    }
    Check(long_window.PendingTaps() == 1);
}

void CheckTimeline() {
    constexpr int64_t start = 520000;
    PetTnt tnt;
    Trigger(tnt);
    Check(!tnt.Tap(start + 1));
    constexpr Stage stages[] = {Stage::Agitated, Stage::Morph,   Stage::Charge1,
                                Stage::Charge2,  Stage::Charge3, Stage::Hold,
                                Stage::Blast,    Stage::Smoke,   Stage::Recover};
    constexpr int starts[] = {0, 250, 450, 950, 1500, 2200, 2350, 2550, 3100, 3500};
    for (int i = 0; i < 9; ++i) {
        Check(tnt.Update(start + starts[i] * 1000, true) == stages[i]);
        Check(tnt.Progress(start + starts[i] * 1000) == 0);
        Check(tnt.Update(start + starts[i + 1] * 1000 - 1, true) == stages[i]);
    }
    Check(tnt.Update(start + 3500000, true) == Stage::Idle);
    Check(!tnt.Tap(start + 3510000));
    Check(!tnt.Tap(start + 3860000));
    tnt.Update(start + 3870000, false);
    for (int i = 0; i < 4; ++i) {
        Check(!tnt.Tap(start + 4000000 + i * 130000));
    }
    tnt.CancelTaps();
    Check(!tnt.Tap(start + 4520000));
    for (int i = 1; i <= 4; ++i) {
        Check(tnt.Tap(start + 4520000 + i * 130000) == (i == 4));
    }
    Check(tnt.Update(start + 7420000, true) == Stage::Blast);
    Check(tnt.Update(start + 7430000, false) == Stage::Idle);
    Check(!tnt.IsActive());

    const auto hold_start = PetTnt::Sample(Stage::Hold, 0);
    const auto hold_end = PetTnt::Sample(Stage::Hold, 1);
    Check(hold_start.zoom == hold_end.zoom && hold_end.squash > hold_start.squash);
    Check(hold_end.x == 0 && hold_end.y == 0 && hold_end.flash == 0);
    Check(PetTnt::Sample(Stage::Recover, 1).face_alpha == 1);

    for (int fps : {20, 60}) {
        PetTnt sampled;
        Trigger(sampled);
        std::array<int, 10> counts{};
        for (int frame = 0; frame < fps * 35 / 10; ++frame) {
            const auto stage = sampled.Update(start + frame * 1000000LL / fps, true);
            ++counts[static_cast<int>(stage)];
        }
        Check(counts[static_cast<int>(Stage::Hold)] >= 3);
        for (auto stage : stages) {
            Check(counts[static_cast<int>(stage)] > 0);
        }
    }
}

void CheckTextureOverride() {
    const auto pose = PetTnt::Sample(Stage::Charge1, 0);
    std::array<uint16_t, 256> texture;
    texture.fill(0x07e0);
    std::array<uint16_t, kSize * kSize> pixels{};
    PetTnt::Draw(pixels.data(), kSize, kSize, Stage::Charge1, 0, pose, texture.data());
    Check(pixels[120 * kSize + 120] == 0x07e0);
    // At 1x the 84px crate starts at y=78. The top stays clear of fuse pixels.
    Check(std::all_of(pixels.begin(), pixels.begin() + 78 * kSize,
                      [](uint16_t pixel) { return pixel == 0; }));
    PetTnt::Draw(pixels.data(), kSize, kSize, Stage::Charge1, 0, pose);
    Check(pixels[120 * kSize + 120] != 0x07e0);
}

// Host stand-in for PetDisplay's live face, driven by the same pose. The TNT
// crate, flash, wave, debris and smoke use the firmware RGB565 renderer directly.
void DrawFace(uint16_t* pixels, const PetTnt::Pose& pose) {
    std::fill_n(pixels, kSize * kSize, 0);
    if (pose.face_alpha <= 0) {
        return;
    }
    const float rx = 88 * pose.face_scale * (1 + pose.squash);
    const float ry = 88 * pose.face_scale * (1 - pose.squash);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = x - kSize / 2 - pose.x;
            const float dy = y - kSize / 2 - pose.y;
            const float distance = std::hypot(dx / rx, dy / ry);
            const float eye_x = (std::abs(dx) - rx * .29f) / (rx * .093f);
            const float eye_y = (dy + .04f * ry) / (ry * .16f * pose.eye_height);
            const float ink = std::clamp((eye_x * eye_x + eye_y * eye_y - .92f) * 8, 0.0f, 1.0f);
            const float alpha = std::clamp((1 - distance) * rx + .5f, 0.0f, 1.0f) * pose.face_alpha;
            const int gray = std::lround(31 * alpha * ink);
            pixels[y * kSize + x] = (gray << 11) | (gray << 6) | gray;
        }
    }
}
}  // namespace

int main(int argc, char** argv) {
    CheckTapWindow();
    CheckTimeline();
    CheckTextureOverride();
    if (argc == 1) {
        return 0;
    }
    if (argc != 3) {
        return 2;
    }
    const int fps = std::atoi(argv[2]);
    if (fps != 20 && fps != 60) {
        return 2;
    }
    std::ofstream output(argv[1], std::ios::binary);
    std::array<uint16_t, kSize * kSize> pixels;
    std::array<unsigned char, kSize * kSize * 3> rgb;
    PetTnt tnt;
    Trigger(tnt);
    for (int frame = 0; frame < fps * 35 / 10; ++frame) {
        const int64_t now = 520000 + frame * 1000000LL / fps;
        const auto stage = tnt.Update(now, true);
        const float progress = tnt.Progress(now);
        const auto pose = PetTnt::Sample(stage, progress);
        DrawFace(pixels.data(), pose);
        PetTnt::Draw(pixels.data(), kSize, kSize, stage, progress, pose);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const auto c = pixels[i];
            rgb[i * 3] = ((c >> 11) & 31) * 255 / 31;
            rgb[i * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
            rgb[i * 3 + 2] = (c & 31) * 255 / 31;
        }
        output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    }
    return output ? 0 : 1;
}
