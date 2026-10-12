#include "launcher/error.hpp"
#include "launcher/installer.hpp"
#include "utf8_main.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>

int test_main(int argc, char **argv) {
    if (argc < 8)
        return 2;
    try {
        const std::string command = argv[1], mode = argv[7];
        const auto install_root = launcher::utf8_path(argv[2]), release = launcher::utf8_path(argv[3]);
        launcher::TrustedKeys keys{{"fixture-only", launcher::public_key_from_hex(argv[4])}};
        launcher::Installer installer(install_root, keys);
        unsigned count = 0;
        const auto fail_at = argc > 8 ? std::stoul(argv[8]) : 0;
        auto fault = [&](std::string_view) {
            ++count;
            if (mode == "disk-full" && count == 5)
                launcher::fail(launcher::ErrorCategory::DiskSpace, "Injected disk-full write failure");
            if (count == fail_at)
                std::_Exit(86);
        };
        if (command == "hold-lock") {
            auto lock = installer.root().open(".launcher/update.lock", launcher::FileMode::Append);
            lock.lock_exclusive();
            std::cout << "locked" << std::endl;
            std::cin.get();
            return 0;
        }
        if (command == "recover") {
            auto result = installer.recover(fault);
            std::cout << "{\"rolled_back\":" << (result.rolled_back ? "true" : "false")
                      << ",\"steps\":" << count << "}\n";
            return 0;
        }
        auto local = launcher::load_local(installer.root(), keys);
        if (command == "verify") {
            if (!local.installed || !launcher::verify_installation(installer.root(), *local.installed))
                return 1;
            std::cout << "{\"sequence\":" << local.installed->manifest.sequence << ",\"language\":\""
                      << local.installed->language << "\"}\n";
            return 0;
        }
        launcher::SafeRoot release_root(release);
        const auto envelope = release_root.read("manifest.envelope.json", 16 * 1024 * 1024);
        if (command == "plan") {
            auto target = launcher::verify_manifest(envelope, keys);
            auto plan = launcher::plan_update(installer.root(), local, target, argv[5], argv[6]);
            std::cout << "{\"patch\":" << (plan.patch_chain ? "true" : "false")
                      << ",\"packages\":" << plan.packages.size() << "}\n";
            return 0;
        }
        launcher::InstallOptions options;
        options.checkpoint = fault;
        if (mode == "repair")
            options.mode = launcher::InstallMode::Repair;
        if (mode == "reinstall")
            options.mode = launcher::InstallMode::Reinstall;
        if (mode == "no-space")
            options.available_bytes = 0;
        if (mode == "backup-quota")
            options.preserved_backup_limit = 0;
        auto fetch = [&](const launcher::PackageRecord &package, std::stop_token, launcher::ProgressSink) {
            if ((mode == "missing-patch" && package.kind == "patch") ||
                (mode == "missing-language" && package.kind == "language"))
                launcher::fail(launcher::ErrorCategory::Network, "Injected fixture package unavailable");
            return release / launcher::utf8_path(package.id + ".zip");
        };
        auto result = installer.install(envelope, argv[5], argv[6], fetch, options);
        std::cout << "{\"patch\":" << (result.used_patch_chain ? "true" : "false")
                  << ",\"fallback\":" << (result.used_snapshot_fallback ? "true" : "false")
                  << ",\"preserved\":" << result.preserved.size() << ",\"steps\":" << count << "}\n";
        return 0;
    } catch (const launcher::Error &error) {
        std::cerr << static_cast<int>(error.category) << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
