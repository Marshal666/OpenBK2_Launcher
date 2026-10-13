#pragma once
#include <SDL3/SDL.h>
#include <vector>

namespace launcher {
// Native clipping for Windows/X11; alpha clipping for composited Wayland.
// Rebuild only when the pixel dimensions or display scale change.
class WindowShape {
    int width_ = 0, height_ = 0, radius_ = 0;
    bool transparent_ = false;
    std::vector<SDL_FRect> corners_;

  public:
    void update(SDL_Window *window, float radius);
    void clip(SDL_Renderer *renderer) const;
};
} // namespace launcher
