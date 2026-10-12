#include "launcher/window_layout.hpp"
#include <iostream>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool value, const char *name) {
        if (!value) {
            std::cerr << name << '\n';
            ++failures;
        }
    };
    const auto normal = launcher::window_layout(1000, 600, 1);
    check(normal.draggable(300, 24), "blank title bar must drag");
    check(!normal.draggable(920, 24), "minimize must receive clicks");
    check(!normal.draggable(980, 24), "close must receive clicks");
    check(!normal.draggable(300, 52), "hero must not drag");
    check(!normal.draggable(-1, 10), "outside window must not drag");
    const auto hidpi = launcher::window_layout(2000, 1200, 2);
    check(hidpi.draggable(600, 48), "scaled title bar must drag");
    check(!hidpi.draggable(1840, 48), "scaled controls must receive clicks");
    check(launcher::window_layout(1000, 600, std::numeric_limits<float>::quiet_NaN()).scale == 1,
          "invalid display scale falls back");
    return failures ? 1 : 0;
}
