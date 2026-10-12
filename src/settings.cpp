#include "launcher/settings.hpp"
#include "json.hpp"
#include "launcher/platform_services.hpp"
#include <mutex>
#include <regex>
namespace launcher {
Settings load_settings(const SafeRoot &config) {
    Settings result;
    result.root = default_game_directory();
    if (!config.exists("settings.json"))
        return result;
    using namespace json;
    auto doc = parse(config.read("settings.json", 65536));
    auto root = yyjson_doc_get_root(doc.get());
    fields(root, {"settings_schema_version", "installation_root", "requested_language", "configured",
                  "relocate", "shortcut"});
    require(number(get(root, "settings_schema_version")) == 1, "Unsupported settings schema");
    result.root = utf8_path(string(get(root, "installation_root")));
    require(result.root.is_absolute(), "Saved installation root is not absolute");
    result.requested_language = string(get(root, "requested_language"));
    for (const auto *field : {"configured", "relocate", "shortcut"})
        require(yyjson_is_bool(get(root, field)), "Invalid setting flag");
    result.configured = yyjson_get_bool(get(root, "configured"));
    result.relocate = yyjson_get_bool(get(root, "relocate"));
    result.shortcut = yyjson_get_bool(get(root, "shortcut"));
    return result;
}
void save_settings(const SafeRoot &config, const Settings &s) {
    using json::quote;
    config.write_atomic("settings.json",
                        "{\"settings_schema_version\":1,\"installation_root\":" + quote(path_utf8(s.root)) +
                            ",\"requested_language\":" + quote(s.requested_language) +
                            ",\"configured\":" + (s.configured ? "true" : "false") +
                            ",\"relocate\":" + (s.relocate ? "true" : "false") +
                            ",\"shortcut\":" + (s.shortcut ? "true" : "false") + "}");
}
void append_log(const SafeRoot &config, std::string_view message) {
    try {
        static std::mutex log_mutex;
        std::scoped_lock guard(log_mutex);
        bool rotate = false;
        {
            if (config.exists("launcher.log")) {
                auto file = config.open("launcher.log", FileMode::Read);
                rotate = file.size() > 1024 * 1024;
            }
        }
        if (rotate)
            config.rename("launcher.log", "launcher.previous.log", true);
        auto text = std::regex_replace(std::string(message), std::regex("\\?[^ \\r\\n]+"), "?[redacted]");
        text = text.substr(0, 8192) + "\n";
        auto file = config.open("launcher.log", FileMode::Append);
        file.write({text.data(), text.size()});
        file.flush();
    } catch (...) { /* Logging must not turn a successful installation into failure. */
    }
}
} // namespace launcher
