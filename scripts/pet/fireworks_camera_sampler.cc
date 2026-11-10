// Host sampler for the pure fireworks camera module. Prints one row per time:

//   ms origin_x origin_y zoom rocket_x rocket_y screen_x screen_y scale offset
//   progress sprite_height sprite_width burst_anchor_x burst_anchor_y burst_start_zoom

// Times are absolute show milliseconds, matching make_tracking_preview.py.

#include <cstdio>
#include <cstdlib>

#include "pet_fireworks_camera.h"

int main(int argc, char** argv) {
    pet_fireworks::Camera camera;
    camera.Prepare(410, 502);
    const auto anchor = camera.BurstAnchor();
    const float start_zoom = camera.BurstStartZoom();
    for (int i = 1; i < argc; ++i) {
        const float ms = std::strtof(argv[i], nullptr);
        const auto view = camera.LensAt(ms);
        const auto rocket = camera.RocketAt(ms);
        const auto screen = pet_fireworks::Camera::Project(rocket.x, rocket.y, view);
        const float scale = camera.ScaleAt(ms);
        const float height = 37.0f * scale * view.zoom;
        const float width = height * 8.0f / 13.0f;
        std::printf(
            "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.3f %.3f "
            "%.6f %.6f %.6f\n",
            ms, view.origin_x, view.origin_y, view.zoom, rocket.x, rocket.y, screen.x, screen.y,
            scale, camera.OffsetAt(ms), camera.ProgressAt(ms), height, width, anchor.x, anchor.y,
            start_zoom);
    }
    return 0;
}
