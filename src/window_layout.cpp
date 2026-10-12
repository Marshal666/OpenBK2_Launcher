#include "launcher/window_layout.hpp"
#include <algorithm>
#include <cmath>

namespace launcher {
WindowLayout window_layout(float width, float height, float dpi_scale) noexcept {
    if (!std::isfinite(dpi_scale) || dpi_scale <= 0)
        dpi_scale = 1;
    return {std::max(width, 1.0f), std::max(height, 1.0f), dpi_scale};
}
bool WindowLayout::draggable(float x, float y) const noexcept {
    return x >= 180 * scale && x < width - 270 * scale && y >= 0 && y < 48 * scale;
}
} // namespace launcher
