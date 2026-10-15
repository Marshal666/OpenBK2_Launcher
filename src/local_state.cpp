#include "launcher/local_state.hpp"
#include "json.hpp"
#include "launcher/error.hpp"
#include "launcher/sha256.hpp"
#include <ctime>

namespace launcher {
namespace {
std::string utc_now() {
    const auto now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}
} // namespace
std::string serialize_state(const Manifest &m, const std::string &platform, const std::string &language,
                            std::set<std::string> initialized_folders,
                            std::set<std::string> initialized_data_files) {
    using json::quote;
    for (const auto &[path, file] : m.initial_data.files)
        if (data_path(path))
            initialized_data_files.insert(path_key(path));
    std::string data = "{\"state_schema_version\":" + std::string(initialized_data_files.empty() ? "2" : "3") +
                       ",\"installed_release\":{\"version\":" + quote(m.version) +
                       ",\"sequence\":" + std::to_string(m.sequence) +
                       ",\"signed_payload_sha256\":" + quote(m.payload_hash) +
                       "},\"platform\":" + quote(platform) + ",\"installed_language\":" + quote(language) +
                       ",\"highest_accepted_sequence\":" + std::to_string(m.sequence) +
                       ",\"managed_files\":[";
    bool first = true;
    for (const auto &[path, f] : desired_files(m, platform, language)) {
        if (!first)
            data += ',';
        first = false;
        data += "{\"path\":" + quote(path) + ",\"size\":" + std::to_string(f.size) +
                ",\"sha256\":" + quote(f.sha256) + "}";
    }
    data += "],\"initialized_folders\":[";
    initialized_folders.insert(m.initial_data.folders.begin(), m.initial_data.folders.end());
    first = true;
    for (const auto &folder : initialized_folders) {
        if (!first)
            data += ',';
        first = false;
        data += quote(folder);
    }
    data += ']';
    if (!initialized_data_files.empty()) {
        data += ",\"initialized_data_files\":[";
        first = true;
        for (const auto &path : initialized_data_files) {
            if (!first)
                data += ',';
            first = false;
            data += quote(path);
        }
        data += ']';
    }
    return data + ",\"updated_utc\":" + quote(utc_now()) + "}";
}
InstalledState parse_state(std::string_view state, Manifest manifest) {
    using namespace json;
    auto doc = parse(state);
    auto value = yyjson_doc_get_root(doc.get());
    fields(value,
           {"state_schema_version", "installed_release", "platform", "installed_language",
            "highest_accepted_sequence", "managed_files", "updated_utc"},
           {"requested_language", "initialized_folders", "initialized_data_files"});
    const auto schema = number(get(value, "state_schema_version"));
    require(schema >= 1 && schema <= 3, "Unsupported local state schema");
    std::set<std::string> initialized, initialized_files;
    if (schema >= 2) {
        for (auto item : array(get(value, "initialized_folders"))) {
            const auto folder = string(item);
            validate_relative_path(folder);
            require(folder.find('/') == folder.npos && initialized.insert(folder).second,
                    "Invalid initialized folder");
            require(path_key(folder) != "data" || schema == 3, "Data initialization requires state v3");
        }
        require(initialized.size() <= 256, "Too many initialized folders");
        for (const auto &folder : manifest.initial_data.folders)
            require(initialized.contains(folder), "Initial-data installation was not completed");
    } else
        require(manifest.initial_data.folders.empty(), "Initial-data installation requires state v2");
    if (schema == 3) {
        for (auto item : array(get(value, "initialized_data_files"))) {
            const auto path = string(item);
            validate_relative_path(path);
            require(data_path(path) && initialized_files.insert(path_key(path)).second,
                    "Invalid initialized Data file");
        }
        require(initialized_files.size() <= 100000, "Too many initialized Data files");
        for (const auto &[path, file] : manifest.initial_data.files)
            if (data_path(path))
                require(initialized_files.contains(path_key(path)), "Initial Data files were not recorded");
    }
    auto installed = get(value, "installed_release");
    fields(installed, {"version", "sequence", "signed_payload_sha256"});
    require(number(get(installed, "sequence")) == manifest.sequence &&
                string(get(installed, "version")) == manifest.version &&
                string(get(installed, "signed_payload_sha256")) == manifest.payload_hash &&
                number(get(value, "highest_accepted_sequence")) == manifest.sequence,
            "Local state does not match authenticated manifest");
    const auto platform = string(get(value, "platform")), language = string(get(value, "installed_language"));
    auto expected = desired_files(manifest, platform, language);
    std::set<std::string> seen;
    for (auto f : array(get(value, "managed_files"))) {
        fields(f, {"path", "size", "sha256"});
        const auto path = string(get(f, "path"));
        require(seen.insert(path).second && expected.contains(path),
                "Local state has unauthorized managed path");
        require(number(get(f, "size")) == expected.at(path).size &&
                    string(get(f, "sha256")) == expected.at(path).sha256,
                "Local state file differs from signed inventory");
    }
    require(seen.size() == expected.size(), "Local inventory is incomplete");
    return {std::move(manifest), platform, language, std::move(expected), std::move(initialized),
            std::move(initialized_files)};
}
bool initialized_user_path(const InstalledState &state, std::string_view path) {
    const auto key = path_key(path), top = key.substr(0, key.find('/'));
    for (const auto &folder : state.initialized_folders)
        if (path_key(folder) != "data" && path_key(folder) == top)
            return true;
    if (top == "data") {
        if (state.initialized_data_files.contains(key))
            return true;
        const auto next = state.initialized_data_files.lower_bound(key + '/');
        if (next != state.initialized_data_files.end() && next->starts_with(key + '/'))
            return true;
        for (auto slash = key.find('/'); slash != key.npos; slash = key.find('/', slash + 1))
            if (state.initialized_data_files.contains(key.substr(0, slash)))
                return true;
    }
    return false;
}
LocalInstallation load_local(const SafeRoot &root, const TrustedKeys &keys, ManifestPolicy policy) {
    LocalInstallation result;
    result.pending_recovery =
        root.exists(".launcher/transaction.json") || root.exists(".launcher/initial-reset.json");
    try {
        if (root.exists(".launcher/installed_manifest.envelope.json"))
            result.authenticated_manifest = verify_manifest(
                root.read(".launcher/installed_manifest.envelope.json", 16 * 1024 * 1024), keys, policy);
        if (result.authenticated_manifest && root.exists(".launcher/state.json"))
            result.installed = parse_state(root.read(".launcher/state.json", 16 * 1024 * 1024),
                                           *result.authenticated_manifest);
        else
            result.warning =
                "No authenticated installed inventory; existing files will be preserved during adoption.";
    } catch (const Error &error) {
        result.installed.reset();
        result.warning = std::string("Safe adoption required: ") + error.what();
    }
    return result;
}
bool file_matches(const SafeRoot &root, const FileRecord &f, std::stop_token stop) {
    if (!root.exists(f.path))
        return false;
    auto file = root.open(f.path, FileMode::Read);
    return file.size() == f.size && sha256_file(file, stop) == f.sha256;
}
bool verify_installation(const SafeRoot &root, const InstalledState &state, std::stop_token stop) {
    if (root.exists(".launcher/transaction.json") || root.exists(".launcher/initial-reset.json"))
        return false;
    for (const auto &[path, file] : state.files) {
        if (!file_matches(root, file, stop))
            return false;
#ifndef _WIN32
        auto handle = root.open(path, FileMode::Read);
        if (handle.executable() != file.executable)
            return false;
#endif
    }
    return true;
}
bool installation_ready(const SafeRoot &root, const InstalledState &state) {
    if (root.exists(".launcher/transaction.json") || root.exists(".launcher/initial-reset.json"))
        return false;
    const auto &profile = state.manifest.platforms.at(state.platform).launch;
    if (!root.exists(profile.executable))
        return false;
    auto file = root.open(profile.executable, FileMode::Read);
    const auto &expected = state.files.at(profile.executable);
    if (file.size() != expected.size)
        return false;
#ifndef _WIN32
    if (!file.executable())
        return false;
#endif
    return true;
}
} // namespace launcher
