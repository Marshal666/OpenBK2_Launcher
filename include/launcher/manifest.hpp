#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {
using Inventory = std::map<std::string, struct FileRecord>;
struct FileRecord {
    std::string path;
    std::uint64_t size = 0;
    std::string sha256;
    bool executable = false;
    bool operator==(const FileRecord &) const = default;
};
struct PackageRecord {
    std::string id, kind, platform, url, sha256;
    std::uint64_t size = 0, unpacked_size = 0;
    Inventory files;
};
struct UpdateEdge {
    std::uint64_t from, to;
    std::vector<std::string> packages;
};
struct LaunchProfile {
    std::string executable, working_directory;
    std::vector<std::string> args;
    std::map<std::string, std::string> environment;
};
struct PlatformRecord {
    LaunchProfile launch;
    Inventory files;
    std::set<std::string> managed_user_dir_files;
    std::vector<std::string> snapshots;
    std::vector<UpdateEdge> edges;
};
struct LanguageRecord {
    std::string label, package_id;
    FileRecord file;
};
struct Manifest {
    std::string version, created_utc, payload, payload_hash, envelope;
    std::uint64_t sequence = 0;
    std::map<std::string, PackageRecord> packages;
    std::map<std::string, PlatformRecord> platforms;
    std::map<std::string, LanguageRecord> languages;
};
using TrustedKeys = std::map<std::string, std::array<std::uint8_t, 32>>;
struct ManifestPolicy {
    bool allow_loopback_http = false;
};
std::vector<std::uint8_t> decode_base64(std::string_view text);
std::array<std::uint8_t, 32> public_key_from_hex(std::string_view text);
void validate_relative_path(std::string_view path);
std::string path_key(std::string_view path);
void validate_game_path(std::string_view path, const std::set<std::string> &allowlist);
bool safe_url(std::string_view url, ManifestPolicy policy = {});
Manifest verify_manifest(std::string_view envelope, const TrustedKeys &keys, ManifestPolicy policy = {});
void enforce_sequence(const Manifest &target, std::uint64_t highest, std::string_view accepted_hash);
Inventory desired_files(const Manifest &manifest, const std::string &platform, const std::string &language);
} // namespace launcher
