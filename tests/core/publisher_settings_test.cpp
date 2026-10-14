#include "../../src/publisher_settings.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    namespace fs = std::filesystem;
    auto check = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    try {
        publisher::Settings original;
        original.fields = {{"repo", "game"}, {"owner", "example"},
                           {"shared_init_data", "/game inputs/initial"},
                           {"key_file", "/private/release.pem"},
                           {"managed_files", "Profiles/official.dat\nMODs/official.pak"},
                           {"excludes", "bin/*.log\npath with \"quotes\" & apostrophe's"}};
        original.first_time = false;
        original.local_only = true;
        original.build_launcher = true;
        original.resume = true;
        const auto directory = fs::current_path() / ("publisher-settings-test-" + launcher::unique_id());
        fs::create_directory(directory);
        const auto path = directory / launcher::utf8_path("settings & apostrophe's \xc4\x8d.json");
        original.save_file(path);
        auto read = [&] {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), {});
        };
        const auto data = read();
        check(data.find("token") == std::string::npos && data.find("BEGIN PRIVATE KEY") == std::string::npos,
              "Saved settings contain secret fields");
        auto restored = original;
        for (auto &[name, text] : restored.fields)
            text.clear();
        restored.first_time = true;
        restored.local_only = restored.build_launcher = restored.resume = false;
        restored.load_file(path);
        check(restored.serialize() == original.serialize(), "Settings did not round trip through a file");
        // Loading a foreign token must never adopt or copy it to a new settings file.
        auto injected = data;
        injected.insert(1, "\"token\":\"do-not-save-this-token\",\"future_field\":123,");
        restored.load(injected);
        restored.save_file(path);
        check(read() == data,
              "Unknown fields or token leaked into saved settings");
        const auto unchanged = restored.serialize();
        for (const char *bad : {"{", "[]", "{}", "{\"owner\":\"changed\",\"repo\":42}",
                               "{\"owner\":\"changed\",\"publish\":\"yes\"}",
                               "{\"owner\":\"changed\",\"repo\":\"a\",\"repo\":\"b\"}"}) {
            bool rejected = false;
            try {
                restored.load(bad);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "Invalid settings were accepted");
            check(restored.serialize() == unchanged, "Invalid settings partially changed entries");
        }
        // The pre-initial-data format remains readable and clears any stale initial folder.
        restored.load("{\"owner\":\"legacy\",\"repo\":\"game\",\"first_time\":false}");
        check(restored.fields.at("shared_init_data").empty() && !restored.first_time,
              "Legacy settings did not load correctly");
        fs::remove(path);
        fs::remove(directory);
        std::cout << "Publisher settings: file round trip, secret exclusion, invalid loads, legacy compatibility passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
