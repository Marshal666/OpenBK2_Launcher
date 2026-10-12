#include "launcher/manifest.hpp"
#include "launcher/sha256.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    unsigned checks = 0;
    auto check = [&](bool ok) {
        ++checks;
        if (!ok)
            throw std::runtime_error("Security assertion failed");
    };
    auto rejects = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected);
    };
    try {
        check(launcher::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check(launcher::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        for (const char *path : {"../bad", "/bad", "C:/bad", "a//b", "a/./b", "a/../b", "a\\b", "NUL",
                                 "bin/CON.exe", "com1", "a.", "a ", "a:", "a?"})
            rejects([&] { launcher::validate_relative_path(path); });
        for (const char *path : {".launcher/state.json", "launcher_installed.txt", "bin/OpenBK2Launcher.exe",
                                 "MODs/private.pak", "Profiles/player.dat"})
            rejects([&] { launcher::validate_game_path(path, {}); });
        launcher::validate_game_path("MODs/official.pak", {"MODs/official.pak"});
        for (const char *bad : {"A", "====", "Zg=Z", "Zh==", "Zm9=", "Zg==\n", " Zg=="})
            rejects([&] { (void)launcher::decode_base64(bad); });
        check(launcher::decode_base64("Zg==") == std::vector<std::uint8_t>{'f'});
        check(launcher::decode_base64("Zm8=") == std::vector<std::uint8_t>({'f', 'o'}));
        check(launcher::safe_url("https://example.org/a.zip"));
        for (const char *bad : {"http://example.org/a", "file:///a", "https://u:p@example.org/a",
                                "https://example.org/\\bad", "ftp://example.org/a"})
            check(!launcher::safe_url(bad));
        check(launcher::safe_url("http://127.0.0.1:1234/a", {true}));
        check(!launcher::safe_url("http://example.org/a", {true}));
        std::cout << checks << " security assertions passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
