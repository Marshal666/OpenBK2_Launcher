#include "launcher/controller.hpp"
#include "launcher/error.hpp"
#include "utf8_main.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace launcher;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void idle(Controller &controller) {
    for (unsigned i = 0; i < 600; ++i) {
        auto state = controller.snapshot();
        if (!state.busy && !state.checking)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("Controller timeout");
}
int test_main(int argc, char **argv) {
    try {
        if (argc < 3)
            return 2;
        const std::string mode = argv[1];
        if (mode == "path-text") {
            std::cout << path_utf8(utf8_path(argv[2])) << '\n';
            return 0;
        }
        if (mode == "hold") {
            std::cout << "ready" << std::endl;
            std::cin.get();
            return 0;
        }
        if (mode == "hold-lock") {
            SafeRoot root(checked_installation_directory(utf8_path(argv[2])));
            auto lock = root.open(".launcher/update.lock", FileMode::Append);
            lock.lock_exclusive();
            std::cout << "ready" << std::endl;
            std::cin.get();
            return 0;
        }
        if (mode == "handoff-test") {
            SafeRoot config(utf8_path(argv[2]));
            check(argc == 5, "handoff args");
            complete_handoff(config, argv[4]);
            config.write_atomic("handoff-success", "yes");
            return 0;
        }
        if (mode == "settings-root") {
            SafeRoot config(utf8_path(argv[2]), true);
            std::cout << path_utf8(load_settings(config).root) << '\n';
            return 0;
        }
        if (mode == "safe-read") {
            SafeRoot root(utf8_path(argv[2]));
            auto file = root.open(argv[3], FileMode::Read);
            return 0;
        }
        if (mode == "relocate" || mode == "relocate-no-ack") {
            SafeRoot config(utf8_path(argv[2]), true), destination(utf8_path(argv[3]), true);
            const std::vector<std::string> args = mode == "relocate"
                                                      ? std::vector<std::string>{"handoff-test", argv[2]}
                                                      : std::vector<std::string>{"path-text", "no-ack"};
            prepare_relocation(destination, config, args);
            return 0;
        }
        if (mode == "shortcut") {
            create_desktop_shortcut(utf8_path(argv[2]), utf8_path(argv[3]));
            return 0;
        }
        if (mode == "spawn") {
            ChildProcess process;
            std::vector<std::string> args;
            for (int i = 4; i < argc; ++i)
                args.push_back(argv[i]);
            std::map<std::string, std::string> env;
#ifndef _WIN32
            env = {{"DXVK_WSI_DRIVER", "SDL3"}, {"LD_LIBRARY_PATH", argv[3]}};
#endif
            process.start(utf8_path(argv[2]), utf8_path(argv[3]), args, env);
            check(process.running(), "child not running");
            check(game_process_running(utf8_path(argv[2])), "manual process detection failed");
            while (process.running())
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return 0;
        }
        check(argc == 6 || (mode == "mods" && argc == 7), "controller args");
        ApplicationOptions options;
        options.config_override = utf8_path(argv[2]);
        options.root_override = utf8_path(argv[3]);
        options.fixture_release = argv[4];
        options.fixture_public_key = argv[5];
        if (argc == 7)
            options.fixture_package_base = argv[6];
        options.auto_install = true;
        if (mode == "offline")
            options.fixture_release.clear();
        Controller controller(options);
        controller.initialize();
        idle(controller);
        if (mode == "missing") {
            check(controller.snapshot().root_missing, "missing root not reported");
            return 0;
        }
        if (mode == "online" || mode == "mods") {
            controller.check_updates();
            idle(controller);
            check(bool(controller.snapshot().latest), "verified manifest missing");
            controller.install();
            idle(controller);
            check(controller.snapshot().error.empty(), controller.snapshot().error.c_str());
            check(controller.snapshot().ready, "not ready after install");
            if (mode == "mods") {
                check(controller.snapshot().mods.empty(), "base install fetched a mod");
                controller.download_mod("Alpha");
                idle(controller);
                check(controller.snapshot().error.empty(), controller.snapshot().error.c_str());
                check(controller.snapshot().mods.size() == 1, "mod download not reflected in UI state");
                SafeRoot game_root(options.root_override);
                game_root.write_atomic("MODs/Alpha/user.cfg", "player edit");
                controller.play();
                idle(controller);
                check(controller.game_running(), "mod fixture game did not start");
                controller.remove_mod("Alpha");
                idle(controller);
                check(!controller.snapshot().error.empty(), "mod removal permitted while game running");
                TrustedKeys keys{{"fixture-only", public_key_from_hex(options.fixture_public_key)}};
                try {
                    ModManager(options.root_override, keys).remove("Alpha");
                    check(false, "core mod removal permitted while game running");
                } catch (const Error &error) {
                    check(error.category == ErrorCategory::GameRunning, "wrong running-game error");
                }
                game_root.write_atomic("bin/fixture-stop", "stop");
                while (controller.game_running())
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                controller.remove_mod("Alpha");
                idle(controller);
                check(controller.snapshot().error.empty(), controller.snapshot().error.c_str());
                check(controller.snapshot().mods.empty() && controller.snapshot().ready,
                      "mod removal state wrong");
                controller.download_mod("Beta");
                idle(controller);
                check(controller.snapshot().error.empty(), controller.snapshot().error.c_str());
                check(controller.snapshot().mods.size() == 1, "second mod not installed");
                return 0;
            }
        } else {
            check(controller.snapshot().ready, "offline verification failed");
            controller.check_updates();
            idle(controller);
            check(controller.snapshot().ready, "failed update check disabled Play");
        }
        controller.play();
        idle(controller);
        check(controller.game_running(), "Play failed");
        if (mode == "online") {
            controller.install();
            idle(controller);
            check(controller.snapshot().error.find("Close the game") != std::string::npos,
                  "update accepted while game running");
        }
        while (controller.game_running())
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (mode == "online") {
            auto settings = controller.snapshot().settings;
            settings.requested_language = "de";
            controller.configure(settings);
            idle(controller);
            controller.install();
            idle(controller);
            check(controller.snapshot().ready && controller.snapshot().installed->language == "de",
                  "language application failed");
            SafeRoot config(options.config_override);
            check(load_settings(config).requested_language == "de", "selection not persisted");
        }
        std::cout << "lifecycle passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
