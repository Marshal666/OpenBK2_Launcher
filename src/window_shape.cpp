#include "window_shape.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <X11/extensions/shape.h>
#endif

namespace launcher {
namespace {
void require(bool ok, const char *action) {
    if (!ok)
        throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}
int inset(int y, int radius) {
    const double distance = radius - y - .5;
    return std::max(0, int(std::ceil(radius - std::sqrt(radius * radius - distance * distance) - .5)));
}
} // namespace
void WindowShape::update(SDL_Window *window, float radius) {
    int width = 0, height = 0;
    require(SDL_GetWindowSizeInPixels(window, &width, &height), "Read window pixel size");
    const auto rounding = std::clamp(int(std::lround(radius)), 0, std::min(width, height) / 2);
    if (width == width_ && height == height_ && rounding == radius_)
        return;
    width_ = width;
    height_ = height;
    radius_ = rounding;
    transparent_ = (SDL_GetWindowFlags(window) & SDL_WINDOW_TRANSPARENT) != 0;
    corners_.clear();
    for (int y = 0; y < rounding; ++y) {
        const float cut = float(inset(y, rounding));
        corners_.insert(corners_.end(), {{0, float(y), cut, 1},
                                         {float(width) - cut, float(y), cut, 1},
                                         {0, float(height - y - 1), cut, 1},
                                         {float(width) - cut, float(height - y - 1), cut, 1}});
    }
    const auto properties = SDL_GetWindowProperties(window);
#ifdef _WIN32
    auto hwnd =
        static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd) {
        auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1, rounding * 2, rounding * 2);
        if (!region || !SetWindowRgn(hwnd, region, TRUE)) {
            if (region)
                DeleteObject(region);
            throw std::runtime_error("Apply rounded native window region");
        }
        // Windows owns the region after a successful SetWindowRgn.
    }
#else
    auto *display = static_cast<Display *>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
    if (display) {
        // Keep X11 optional at runtime: Wayland does not load this library.
        std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> extension(
            SDL_LoadObject("libXext.so.6"), SDL_UnloadObject);
        require(bool(extension), "Load X11 shape extension");
        auto combine = reinterpret_cast<decltype(&XShapeCombineRectangles)>(
            SDL_LoadFunction(extension.get(), "XShapeCombineRectangles"));
        auto query = reinterpret_cast<decltype(&XShapeQueryExtension)>(
            SDL_LoadFunction(extension.get(), "XShapeQueryExtension"));
        int event = 0, error = 0;
        require(combine && query && query(display, &event, &error), "Find X11 shape extension");
        std::vector<XRectangle> rows;
        for (int y = 0; y < height; ++y) {
            const int edge = std::min(y, height - y - 1);
            const int cut = edge < rounding ? inset(edge, rounding) : 0;
            rows.push_back({short(cut), short(y), static_cast<unsigned short>(width - 2 * cut), 1});
        }
        const auto xid =
            static_cast<Window>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        combine(display, xid, ShapeBounding, 0, 0, rows.data(), int(rows.size()), ShapeSet, YXBanded);
    }
#endif
}
void WindowShape::clip(SDL_Renderer *renderer) const {
    if (!transparent_)
        return;
    // SDL's software renderer does not support the custom alpha multiplication
    // used by SDL_SetWindowShape. Clear just the corner pixels with NONE instead;
    // this also clips ImGui's full-viewport modal dimming and works on every renderer.
    float x_scale = 1, y_scale = 1;
    SDL_GetRenderScale(renderer, &x_scale, &y_scale);
    require(SDL_SetRenderScale(renderer, 1, 1), "Set window clipping scale");
    require(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE), "Set corner clipping mode");
    require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0), "Set transparent corners");
    require(SDL_RenderFillRects(renderer, corners_.data(), int(corners_.size())), "Clip window corners");
    require(SDL_SetRenderScale(renderer, x_scale, y_scale), "Restore render scale");
}
} // namespace launcher
