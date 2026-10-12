#pragma once

namespace launcher {
struct WindowLayout {
    float width;
    float height;
    float scale;
    [[nodiscard]] bool draggable(float x, float y) const noexcept;
};
[[nodiscard]] WindowLayout window_layout(float width, float height, float dpi_scale) noexcept;
} // namespace launcher
