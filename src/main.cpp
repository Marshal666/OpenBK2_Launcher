#include "launcher/application.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstring>
#include <exception>

int main(int argc, char **argv) {
    launcher::ApplicationOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0)
            options.smoke_test = true;
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
            options.capture = argv[++i];
        else if (std::strcmp(argv[i], "--handoff") == 0 && i + 1 < argc)
            options.handoff = argv[++i];
#if OBK_FIXTURE_BUILD
        else if (std::strcmp(argv[i], "--capture-progress") == 0 && i + 1 < argc)
            options.progress_capture = argv[++i];
        else if (std::strcmp(argv[i], "--fixture-release") == 0 && i + 1 < argc)
            options.fixture_release = argv[++i];
        else if (std::strcmp(argv[i], "--fixture-public-key") == 0 && i + 1 < argc)
            options.fixture_public_key = argv[++i];
        else if (std::strcmp(argv[i], "--fixture-package-base") == 0 && i + 1 < argc)
            options.fixture_package_base = argv[++i];
        else if (std::strcmp(argv[i], "--config-dir") == 0 && i + 1 < argc)
            options.config_override = launcher::utf8_path(argv[++i]);
        else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            options.root_override = launcher::utf8_path(argv[++i]);
        else if (std::strcmp(argv[i], "--auto-install") == 0)
            options.auto_install = true;
#endif
        else {
            SDL_Log("Unknown argument: %s", argv[i]);
            return 2;
        }
    }
    try {
        if (!options.handoff.empty()) {
            launcher::SafeRoot config(options.config_override.empty() ? launcher::config_directory()
                                                                      : options.config_override);
            launcher::complete_handoff(config, options.handoff);
        }
        return launcher::run_application(options);
    } catch (const std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", e.what());
        if (!options.smoke_test && !options.auto_install)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenBK2 Launcher", e.what(), nullptr);
        return 1;
    }
}
