#include "launcher/error.hpp"
#include "launcher/mod_manager.hpp"
#include "utf8_main.hpp"
#include <cstdlib>
#include <iostream>

int test_main(int argc, char **argv) {
    if (argc < 7)
        return 2;
    try {
        const std::string command = argv[1], id = argv[5], mode = argv[6];
        launcher::TrustedKeys keys{{"fixture-only", launcher::public_key_from_hex(argv[4])}};
        launcher::ModManager mods(launcher::utf8_path(argv[2]), keys);
        unsigned count = 0, fetched = 0;
        auto checkpoint = [&](std::string_view) {
            if (++count == (argc > 7 ? std::stoul(argv[7]) : 0))
                std::_Exit(86);
        };
        if (command == "list") {
            for (const auto &mod : mods.list())
                std::cout << mod.id << (mod.managed_by_game ? " protected" : " removable") << '\n';
        } else if (command == "remove")
            std::cout << mods.remove(id, checkpoint) << '\n';
        else {
            launcher::SafeRoot release(launcher::utf8_path(argv[3]));
            std::stop_source stop;
            if (mode == "cancel")
                stop.request_stop();
            auto fetch = [&](const launcher::PackageRecord &p, std::stop_token, launcher::ProgressSink) {
                ++fetched;
                if (mode == "missing" || (mode == "partial" && fetched == 2))
                    launcher::fail(launcher::ErrorCategory::Network, "Injected missing mod asset");
                return release.path() / launcher::utf8_path(p.id + ".zip");
            };
            auto progress = [&](const launcher::Progress &) {
                if (mode == "cancel-extract")
                    stop.request_stop();
            };
            mods.download(release.read("manifest.envelope.json", 16 * 1024 * 1024), id, fetch,
                          stop.get_token(), progress, checkpoint);
            std::cout << "downloaded " << fetched << '\n';
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
