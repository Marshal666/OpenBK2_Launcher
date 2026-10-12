#pragma once
#include <array>
#include <cstdint>

// Original procedural placeholder artwork, embedded at compile time. MIT.
// No game assets or runtime resource files are used.
namespace launcher::art {
inline constexpr int width = 320;
inline constexpr int height = 160;
consteval auto make_hero() {
    std::array<std::uint32_t, width * height> pixels{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = 21 + y / 7, g = 29 + y / 6, b = 38 + y / 5;
            const int ridge = 76 + ((x / 29) % 3) * 9 - (x % 29) / 3;
            if (y > ridge) {
                r = 17;
                g = 25;
                b = 31;
            }
            if (y > 115 + (x % 43) / 6) {
                r = 11;
                g = 17;
                b = 23;
            }
            // Distant amber horizon and a geometric armored silhouette.
            if (x > 205 && x < 280 && y > 75 && y < 81) {
                r = 160;
                g = 99;
                b = 42;
            }
            if ((x > 202 && x < 274 && y > 105 && y < 123) || (x > 216 && x < 254 && y > 95 && y < 106) ||
                (x > 250 && x < 299 && y > 96 && y < 100)) {
                r = 8;
                g = 12;
                b = 15;
            }
            pixels[y * width + x] = 0xff000000u | (static_cast<unsigned>(r) << 16) |
                                    (static_cast<unsigned>(g) << 8) | static_cast<unsigned>(b);
        }
    }
    return pixels;
}
inline constexpr auto hero = make_hero();
} // namespace launcher::art
