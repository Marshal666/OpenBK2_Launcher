#pragma once
#include "launcher/safe_files.hpp"
namespace launcher {
struct Settings {
    std::filesystem::path root;
    std::string requested_language = "en";
    bool configured = false, relocate = true, shortcut = true;
};
Settings load_settings(const SafeRoot &config);
void save_settings(const SafeRoot &config, const Settings &settings);
void append_log(const SafeRoot &config, std::string_view message);
} // namespace launcher
