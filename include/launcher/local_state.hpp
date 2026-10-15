#pragma once
#include "launcher/manifest.hpp"
#include "launcher/safe_files.hpp"
#include <optional>
#include <stop_token>
namespace launcher {
struct InstalledState {
    Manifest manifest;
    std::string platform, language;
    Inventory files;
    std::set<std::string> initialized_folders;
    std::set<std::string> initialized_data_files; // Case-folded; guards only, never deletion authority.
};
struct LocalInstallation {
    std::optional<Manifest> authenticated_manifest;
    std::optional<InstalledState> installed;
    bool pending_recovery = false;
    std::string warning;
};
std::string serialize_state(const Manifest &manifest, const std::string &platform,
                            const std::string &language, std::set<std::string> initialized_folders = {},
                            std::set<std::string> initialized_data_files = {});
bool initialized_user_path(const InstalledState &state, std::string_view path);
InstalledState parse_state(std::string_view state, Manifest manifest);
LocalInstallation load_local(const SafeRoot &root, const TrustedKeys &keys, ManifestPolicy policy = {});
bool file_matches(const SafeRoot &root, const FileRecord &file, std::stop_token stop = {});
bool verify_installation(const SafeRoot &root, const InstalledState &state, std::stop_token stop = {});
// Metadata and launch-file checks only; never scans game data or hashes its contents.
bool installation_ready(const SafeRoot &root, const InstalledState &state);
} // namespace launcher
