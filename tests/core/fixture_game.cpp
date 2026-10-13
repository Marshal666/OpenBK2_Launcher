#include "launcher/platform_services.hpp"
#include "utf8_main.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>
int test_main(int argc, char **argv) {
    std::ofstream output("fixture-run.txt", std::ios::binary);
    output << launcher::path_utf8(std::filesystem::current_path()) << '\n';
    for (const auto *key : {"DXVK_WSI_DRIVER", "LD_LIBRARY_PATH"}) {
        const char *value = std::getenv(key);
        output << (value ? value : "") << '\n';
    }
    for (int i = 1; i < argc; ++i)
        output << argv[i] << '\n';
    output.close();
    if (argc == 3 && std::string_view(argv[1]) == "--wait-for-file") {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!std::filesystem::exists(launcher::utf8_path(argv[2])) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return 0;
}
