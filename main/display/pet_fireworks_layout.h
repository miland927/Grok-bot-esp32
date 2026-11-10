#pragma once

#include <algorithm>

namespace pet_fireworks {

// Small integer rectangle used for the canvas dirty regions. Inclusive edges;
// the default value is empty so a clear pass can simply skip it.
struct Rect {
    int x1 = 0;
    int y1 = 0;
    int x2 = -1;
    int y2 = -1;
};

inline bool Empty(const Rect& rect) { return rect.x1 > rect.x2 || rect.y1 > rect.y2; }

inline Rect Union(const Rect& a, const Rect& b) {
    if (Empty(a)) {
        return b;
    }
    if (Empty(b)) {
        return a;
    }
    return {std::min(a.x1, b.x1), std::min(a.y1, b.y1), std::max(a.x2, b.x2), std::max(a.y2, b.y2)};
}

inline Rect Clip(const Rect& rect, int width, int height) {
    if (Empty(rect)) {
        return {};
    }
    const Rect clipped{std::max(0, rect.x1), std::max(0, rect.y1), std::min(width - 1, rect.x2),
                       std::min(height - 1, rect.y2)};
    return Empty(clipped) ? Rect{} : clipped;
}

}  // namespace pet_fireworks
