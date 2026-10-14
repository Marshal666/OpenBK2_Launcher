#pragma once
#include "json.hpp"
#include "launcher/platform_services.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <fstream>
#include <map>

namespace publisher {
// Only public settings belong here. The token lives separately in the GUI.
struct Settings {
    std::map<std::string, std::string> fields;
    bool first_time = true, local_only = false, build_launcher = false, resume = false;

    std::string serialize() const {
        namespace json = launcher::json;
        std::string data = "{\n";
        for (const auto &[name, value] : fields)
            data += "  " + json::quote(name) + ": " + json::quote(value) + ",\n";
        data += "  \"first_time\": " + std::string(first_time ? "true" : "false") +
                ",\n  \"publish\": " + (local_only ? "false" : "true") +
                ",\n  \"build_launcher\": " + (build_launcher ? "true" : "false") +
                ",\n  \"resume\": " + (resume ? "true" : "false") + "\n}\n";
        return data;
    }
    void load(const std::string &data) {
        namespace json = launcher::json;
        auto doc = json::parse(data, 1024 * 1024);
        auto root = yyjson_doc_get_root(doc.get());
        json::require(yyjson_is_obj(root), "Expected publisher settings object");
        // Validate everything before replacing any current entry.
        auto next = *this;
        bool recognized = false;
        for (auto &[name, text] : next.fields) {
            if (auto item = yyjson_obj_get(root, name.c_str())) {
                text = json::string(item);
                recognized = true;
            } else if (name == "shared_init_data") {
                text.clear(); // Older settings files did not contain this optional input.
            }
        }
        json::require(recognized, "This file does not contain publisher settings");
        auto boolean = [&](const char *name, bool fallback) {
            auto item = yyjson_obj_get(root, name);
            if (!item)
                return fallback;
            json::require(yyjson_is_bool(item), std::string("Expected checkbox value: ") + name);
            return yyjson_get_bool(item);
        };
        next.first_time = boolean("first_time", true);
        next.local_only = !boolean("publish", true);
        next.build_launcher = boolean("build_launcher", false);
        next.resume = boolean("resume", false);
        *this = std::move(next);
    }
    void load_file(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Cannot read settings: " + launcher::path_utf8(path));
        const auto size = file.tellg();
        launcher::json::require(size > 0 && size <= 1024 * 1024, "Settings file must be between 1 byte and 1 MiB");
        std::string data(static_cast<size_t>(size), '\0');
        file.seekg(0);
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file)
            throw std::runtime_error("Could not read the complete settings file");
        load(data);
    }
    void save_file(const std::filesystem::path &path) const {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        const auto temporary = absolute.parent_path() / ("publisher-settings-" + launcher::unique_id() + ".tmp");
        const auto temp = launcher::path_utf8(temporary), target = launcher::path_utf8(absolute);
        const auto data = serialize();
        // SDL handles native Unicode filenames and replacement on both Windows and Linux.
        if (!SDL_SaveFile(temp.c_str(), data.data(), data.size()) || !SDL_RenamePath(temp.c_str(), target.c_str())) {
            const std::string error = SDL_GetError();
            SDL_RemovePath(temp.c_str());
            throw std::runtime_error("Cannot save settings: " + error);
        }
    }
};
} // namespace publisher
