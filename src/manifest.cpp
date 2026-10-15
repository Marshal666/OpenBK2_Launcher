#include "launcher/manifest.hpp"
#include "build_config.hpp"
#include "json.hpp"
#include "launcher/error.hpp"
#include "launcher/sha256.hpp"
#include <algorithm>
#include <limits>
#include <monocypher-ed25519.h>
#include <regex>

namespace launcher {
namespace {
using namespace json;
constexpr std::uint64_t package_limit = 1536ull * 1024 * 1024;
constexpr std::uint64_t unpack_limit = 64ull * 1024 * 1024 * 1024;
bool matches(const std::string &s, const char *pattern) {
    return std::regex_match(s, std::regex(pattern));
}
std::string digest(Value v) {
    auto s = string(v);
    require(matches(s, "[a-f0-9]{64}"), "Invalid SHA-256");
    return s;
}
std::string identifier(Value v) {
    auto s = string(v);
    require(matches(s, "[A-Za-z0-9][A-Za-z0-9._-]{0,95}"), "Invalid identifier");
    return s;
}
struct Paths {
    std::map<std::string, std::string> nodes;
    std::set<std::string> files;
    void add(const std::string &path) {
        validate_relative_path(path);
        std::size_t end = 0;
        while (true) {
            end = path.find('/', end);
            const auto prefix = path.substr(0, end);
            const auto key = path_key(prefix);
            const auto found = nodes.find(key);
            require(found == nodes.end() || found->second == prefix, "Case-fold path collision");
            require(!files.contains(key), "Duplicate path or file/directory conflict");
            nodes.emplace(key, prefix);
            if (end == std::string::npos) {
                require(found == nodes.end(), "File conflicts with existing directory");
                files.insert(key);
                break;
            }
            ++end;
        }
    }
};
FileRecord file(Value v, bool target) {
    if (target)
        fields(v, {"path", "size", "sha256", "executable"});
    else
        fields(v, {"path", "size", "sha256"}, {"executable"});
    FileRecord f{string(get(v, "path")), number(get(v, "size")), digest(get(v, "sha256")), false};
    validate_relative_path(f.path);
    require(f.size <= package_limit, "File exceeds v1 package limit");
    if (auto e = yyjson_obj_get(v, "executable")) {
        require(yyjson_is_bool(e), "Invalid executable flag");
        f.executable = yyjson_get_bool(e);
    }
    return f;
}
Inventory inventory(Value v, bool target) {
    Inventory out;
    Paths paths;
    for (auto item : array(v)) {
        auto f = file(item, target);
        paths.add(f.path);
        out.emplace(f.path, std::move(f));
    }
    return out;
}
std::vector<std::string> ids(Value v) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (auto item : array(v)) {
        auto id = identifier(item);
        require(seen.insert(id).second, "Duplicate reference");
        out.push_back(id);
    }
    return out;
}
bool same_bytes(const FileRecord &a, const FileRecord &b) {
    return a.path == b.path && a.size == b.size && a.sha256 == b.sha256;
}
} // namespace

std::string path_key(std::string_view path) {
    std::string out(path);
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return out;
}
void validate_relative_path(std::string_view path) {
    json::require(!path.empty() && path.size() <= 1024 && path.front() != '/' && path.back() != '/',
                  "Unsafe path length or root");
    unsigned depth = 0;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == path.npos)
            end = path.size();
        const auto part = path.substr(start, end - start);
        json::require(!part.empty() && part.size() <= 255 && part != "." && part != ".." &&
                          part.back() != '.' && part.back() != ' ',
                      "Unsafe path component");
        json::require(++depth <= 32, "Path depth exceeded");
        for (unsigned char c : part)
            json::require(c >= 32 && c <= 126 &&
                              std::string_view("\\:<>\"|?*").find(static_cast<char>(c)) ==
                                  std::string_view::npos,
                          "Nonportable game path");
        const auto stem = path_key(part.substr(0, part.find('.')));
        json::require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" && stem != "clock$" &&
                          !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                            stem[3] >= '0' && stem[3] <= '9'),
                      "Reserved Windows name");
        start = end + 1;
    }
}
void validate_game_path(std::string_view path, const std::set<std::string> &allowlist) {
    validate_relative_path(path);
    const auto key = path_key(path);
    const auto basename = key.substr(key.rfind('/') == key.npos ? 0 : key.rfind('/') + 1);
    json::require(key != ".launcher" && !key.starts_with(".launcher/") && key != "launcher_installed.txt" &&
                      !basename.starts_with("openbk2launcher"),
                  "Protected launcher path");
    if (key.starts_with("mods/") || key.starts_with("profiles/"))
        json::require(allowlist.contains(std::string(path)), "User path requires exact signed allowlist");
}
bool safe_url(std::string_view url, ManifestPolicy policy) {
    if (url.size() > 4096 || url.find_first_of("\r\n\t \\#") != url.npos)
        return false;
    auto start = url.starts_with("https://") ? 8u : 0u;
    if (!start && policy.allow_loopback_http && url.starts_with("http://"))
        start = 7;
    if (!start)
        return false;
    const auto slash = url.find('/', start);
    if (slash == url.npos)
        return false;
    const auto host = url.substr(start, slash - start);
    if (host.empty() || host.find('@') != host.npos || host.find('%') != host.npos)
        return false;
    if (start == 7 && host != "127.0.0.1" && !host.starts_with("127.0.0.1:"))
        return false;
    return std::all_of(url.begin(), url.end(), [](unsigned char c) { return c >= 33 && c <= 126; });
}
Manifest verify_manifest(std::string_view envelope, const TrustedKeys &keys, ManifestPolicy policy) {
    using namespace json;
    auto doc = parse(envelope);
    auto root = yyjson_doc_get_root(doc.get());
    fields(root, {"envelope_version", "key_id", "payload_b64", "signature_b64"});
    require(number(get(root, "envelope_version")) == 1, "Unsupported envelope version");
    const auto signing_id = string(get(root, "key_id"));
    const auto signing_key = keys.find(signing_id);
    if (signing_key == keys.end())
        fail(ErrorCategory::Signature, "Unknown signing key");
    auto payload = decode_base64(string(get(root, "payload_b64")));
    auto signature = decode_base64(string(get(root, "signature_b64")));
    if (signature.size() != 64 || crypto_ed25519_check(signature.data(), signing_key->second.data(),
                                                       payload.data(), payload.size()) != 0)
        fail(ErrorCategory::Signature, "Manifest signature verification failed");
    Manifest m;
    m.payload.assign(reinterpret_cast<const char *>(payload.data()), payload.size());
    m.payload_hash = sha256(m.payload);
    m.envelope = envelope;
    auto body = parse(m.payload, 12 * 1024 * 1024);
    root = yyjson_doc_get_root(body.get());
    const auto schema = number(get(root, "schema_version"));
    require(schema >= 1 && schema <= 3, "Unsupported manifest schema");
    if (schema == 3)
        fields(root, {"schema_version", "release", "min_launcher_version", "packages", "platforms",
                      "languages", "mods", "initial_data"});
    else if (schema == 2)
        fields(root, {"schema_version", "release", "min_launcher_version", "packages", "platforms",
                      "languages", "mods"});
    else
        fields(root,
               {"schema_version", "release", "min_launcher_version", "packages", "platforms", "languages"});
    auto release = get(root, "release");
    fields(release, {"version", "sequence", "created_utc"});
    m.version = string(get(release, "version"));
    m.sequence = number(get(release, "sequence"));
    m.created_utc = string(get(release, "created_utc"));
    require(matches(m.version, "[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}(-[A-Za-z0-9.-]+)?") && m.sequence > 0,
            "Invalid release version or sequence");
    require(matches(m.created_utc, "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z"),
            "Invalid UTC time");
    const auto minimum = string(get(root, "min_launcher_version"));
    std::smatch minimum_parts;
    require(
        std::regex_match(minimum, minimum_parts, std::regex("([0-9]{1,6})\\.([0-9]{1,6})\\.([0-9]{1,6})")),
        "Invalid minimum launcher version");
    const std::array<unsigned long, 3> required_version{
        std::stoul(minimum_parts[1]), std::stoul(minimum_parts[2]), std::stoul(minimum_parts[3])};
    require(required_version <= build::version_parts,
            "This release requires a newer launcher; update manually");
    require(schema == 1 || required_version >= std::array<unsigned long, 3>{0, 2, 0},
            "Schema v2 requires launcher 0.2.0");
    require(schema < 3 || required_version >= std::array<unsigned long, 3>{0, 3, 0},
            "Schema v3 requires launcher 0.3.0");
    for (auto item : array(get(root, "packages"))) {
        fields(item, {"id", "kind", "platform", "url", "size", "unpacked_size", "sha256", "files"});
        PackageRecord p;
        p.id = identifier(get(item, "id"));
        p.kind = string(get(item, "kind"));
        p.platform = string(get(item, "platform"));
        p.url = string(get(item, "url"));
        p.size = number(get(item, "size"));
        p.unpacked_size = number(get(item, "unpacked_size"));
        p.sha256 = digest(get(item, "sha256"));
        require(p.kind == "snapshot" || p.kind == "patch" || p.kind == "language" ||
                    (schema >= 2 && p.kind == "mod") || (schema >= 3 && p.kind == "initial"),
                "Unknown package kind");
        require(((p.kind == "language" || p.kind == "mod" || p.kind == "initial") && p.platform == "any") ||
                    ((p.kind == "snapshot" || p.kind == "patch") &&
                     (p.platform == "windows-x64" || p.platform == "linux-x64" ||
                      (schema >= 2 && p.platform == "any"))),
                "Invalid package platform");
        require(safe_url(p.url, policy), "Unsafe package URL");
        require(p.size > 0 && p.size <= package_limit && p.unpacked_size <= unpack_limit,
                "Package size limit exceeded");
        p.files = inventory(get(item, "files"), false);
        require(!p.files.empty(), "Empty package inventory");
        std::uint64_t total = 0;
        for (const auto &[path, f] : p.files) {
            (void)path;
            require(f.size <= unpack_limit - total, "Package inventory size overflow");
            total += f.size;
        }
        require(total == p.unpacked_size, "Incorrect unpacked size");
        require(m.packages.emplace(p.id, std::move(p)).second, "Duplicate package ID");
    }
    std::set<std::string> used;
    auto package = [&](const std::string &id, const std::string &kind,
                       const std::string &platform) -> const PackageRecord & {
        const auto it = m.packages.find(id);
        require(it != m.packages.end(), "Unknown package reference");
        require(it->second.kind == kind && (it->second.platform == platform ||
                                            (schema >= 2 && (kind == "snapshot" || kind == "patch") &&
                                             it->second.platform == "any")),
                "Package membership mismatch");
        used.insert(id);
        return it->second;
    };
    for (const auto &[platform, value] : object(get(root, "platforms"))) {
        require(platform == "windows-x64" || platform == "linux-x64", "Unsupported platform");
        fields(value, {"launch", "files", "snapshot_package_ids", "managed_user_dir_files", "update_edges"});
        PlatformRecord p;
        p.files = inventory(get(value, "files"), true);
        require(!p.files.empty(), "Empty target platform");
        std::uint64_t target_size = 0;
        for (const auto &[path, f] : p.files) {
            (void)path;
            require(f.size <= 512ull * 1024 * 1024 * 1024 - target_size,
                    "Target inventory exceeds v1 size limit");
            target_size += f.size;
        }
        for (auto item : array(get(value, "managed_user_dir_files"))) {
            const auto path = string(item);
            validate_relative_path(path);
            const auto lower = path_key(path);
            require(lower.starts_with("mods/") || lower.starts_with("profiles/"),
                    "Allowlist must name exact user-directory files");
            require(p.managed_user_dir_files.insert(path).second, "Duplicate allowlist path");
        }
        for (const auto &[path, f] : p.files) {
            (void)f;
            validate_game_path(path, p.managed_user_dir_files);
            require(!path_key(path).starts_with("data/texts-"), "Language file in base inventory");
        }
        p.snapshots = ids(get(value, "snapshot_package_ids"));
        Inventory snapshot;
        for (const auto &id : p.snapshots)
            for (const auto &[path, f] : package(id, "snapshot", platform).files)
                require(snapshot.emplace(path, f).second, "Overlapping snapshot inventories");
        require(snapshot.size() == p.files.size(), "Snapshot does not cover target");
        for (const auto &[path, f] : p.files)
            require(snapshot.contains(path) && same_bytes(snapshot.at(path), f), "Snapshot target mismatch");
        std::set<std::pair<std::uint64_t, std::uint64_t>> edges;
        for (auto e : array(get(value, "update_edges"))) {
            fields(e, {"from_sequence", "to_sequence", "package_ids"});
            UpdateEdge edge{number(get(e, "from_sequence")), number(get(e, "to_sequence")),
                            ids(get(e, "package_ids"))};
            require(edge.from > 0 && edge.from < edge.to && edge.to <= m.sequence &&
                        edges.emplace(edge.from, edge.to).second,
                    "Invalid update edge");
            Paths entries;
            for (const auto &id : edge.packages)
                for (const auto &[path, f] : package(id, "patch", platform).files) {
                    (void)f;
                    entries.add(path);
                    validate_game_path(path, p.managed_user_dir_files);
                    require(!path_key(path).starts_with("data/texts-"), "Language file in base patch");
                }
            p.edges.push_back(std::move(edge));
        }
        auto launch = get(value, "launch");
        fields(launch, {"executable", "working_directory", "args", "environment"});
        p.launch.executable = string(get(launch, "executable"));
        p.launch.working_directory = string(get(launch, "working_directory"));
        require(p.launch.working_directory == "bin" && p.launch.executable.starts_with("bin/") &&
                    p.files.contains(p.launch.executable) && p.files.at(p.launch.executable).executable,
                "Unsafe launch executable or working directory");
        for (auto arg : array(get(launch, "args"))) {
            auto a = string(arg);
            require(a.size() <= 1024, "Launch argument too long");
            p.launch.args.push_back(a);
        }
        require(p.launch.args.size() <= 32, "Too many launch arguments");
        for (const auto &[key, environment_value] : object(get(launch, "environment"))) {
            const auto v = string(environment_value);
            require(platform == "linux-x64" && ((key == "LD_LIBRARY_PATH" && v == "${INSTALL_DIR}/bin") ||
                                                (key == "DXVK_WSI_DRIVER" && v == "SDL3")),
                    "Disallowed launch environment");
            p.launch.environment.emplace(key, v);
        }
        if (platform == "linux-x64")
            require(p.launch.environment.size() == 2, "Linux launch environment is incomplete");
        m.platforms.emplace(platform, std::move(p));
    }
    require(!m.platforms.empty(), "No platforms");
    Paths languages;
    for (const auto &[locale, v] : object(get(root, "languages"))) {
        require(matches(locale, "[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,2}"), "Invalid locale");
        fields(v, {"label", "path", "package_id", "size", "sha256"});
        LanguageRecord l{string(get(v, "label")),
                         identifier(get(v, "package_id")),
                         {string(get(v, "path")), number(get(v, "size")), digest(get(v, "sha256")), false}};
        require(!l.label.empty() && l.label.size() <= 128 && l.file.path == "Data/texts-" + locale + ".pak",
                "Invalid language record");
        languages.add(l.file.path);
        const auto &pkg = package(l.package_id, "language", "any");
        require(pkg.files.size() == 1 && pkg.files.contains(l.file.path) &&
                    same_bytes(pkg.files.at(l.file.path), l.file),
                "Language inventory mismatch");
        for (const auto &[id, p] : m.platforms) {
            (void)id;
            Paths all;
            for (const auto &[path, f] : p.files) {
                (void)f;
                all.add(path);
            }
            all.add(l.file.path);
        }
        m.languages.emplace(locale, std::move(l));
    }
    require(!m.languages.empty() && m.languages.size() <= 64, "Invalid language count");
    if (schema >= 2) {
        Paths mod_names;
        for (const auto &[id, v] : object(get(root, "mods"))) {
            require(matches(id, "[A-Za-z0-9][A-Za-z0-9_-]{0,63}"), "Invalid mod identifier");
            mod_names.add(id);
            fields(v, {"label", "files", "package_ids"});
            ModRecord mod{string(get(v, "label")), inventory(get(v, "files"), true),
                          ids(get(v, "package_ids"))};
            require(!mod.label.empty() && mod.label.size() <= 128 && !mod.files.empty(),
                    "Invalid mod description or files");
            Inventory packaged;
            for (const auto &pid : mod.packages)
                for (const auto &[path, f] : package(pid, "mod", "any").files)
                    require(packaged.emplace(path, f).second, "Overlapping mod packages");
            require(packaged.size() == mod.files.size(), "Mod package coverage mismatch");
            std::uint64_t total = 0;
            for (const auto &[path, f] : mod.files) {
                require(path.starts_with("MODs/" + id + "/"), "Mod file outside its folder");
                validate_game_path(path, {path});
                require(packaged.contains(path) && same_bytes(packaged.at(path), f),
                        "Mod package bytes mismatch");
                require(f.size <= 64ull * 1024 * 1024 * 1024 - total, "Mod size limit exceeded");
                total += f.size;
            }
            for (const auto &[platform, target] : m.platforms) {
                (void)platform;
                const auto prefix = path_key("MODs/" + id);
                for (const auto &[path, f] : target.files) {
                    (void)f;
                    const auto key = path_key(path);
                    require(key != prefix && !key.starts_with(prefix + "/") && !prefix.starts_with(key + "/"),
                            "Optional mod overlaps base game");
                }
            }
            m.mods.emplace(id, std::move(mod));
        }
        require(m.mods.size() <= 256, "Too many mods");
    }
    if (schema >= 3) {
        const auto value = get(root, "initial_data");
        fields(value, {"folders", "files", "package_ids"});
        auto &initial = m.initial_data;
        Paths folders;
        for (auto item : array(get(value, "folders"))) {
            const auto folder = string(item), key = path_key(folder);
            folders.add(folder);
            require(folder.find('/') == folder.npos && key != ".launcher" && key != "bin" &&
                        key != "mods" && key != "launcher_installed.txt" &&
                        !key.starts_with("openbk2launcher"),
                    "Unsafe initial-data folder");
            if (key == "data")
                require(required_version >= std::array<unsigned long, 3>{0, 3, 1},
                        "Initial files in Data require launcher 0.3.1");
            initial.folders.insert(folder);
        }
        require(initial.folders.size() <= 64, "Too many initial-data folders");
        initial.files = inventory(get(value, "files"), true);
        initial.packages = ids(get(value, "package_ids"));
        Inventory packaged;
        for (const auto &id : initial.packages)
            for (const auto &[path, f] : package(id, "initial", "any").files)
                require(packaged.emplace(path, f).second, "Overlapping initial packages");
        require(packaged.size() == initial.files.size(), "Initial package coverage mismatch");
        std::set<std::string> populated;
        std::uint64_t size = 0;
        for (const auto &[path, f] : initial.files) {
            const auto slash = path.find('/');
            require(slash != path.npos && !f.executable,
                    "Initial data must be non-executable files inside folders");
            populated.insert(path.substr(0, slash));
            if (data_path(path))
                initial.data_file_keys.insert(path_key(path));
            validate_game_path(path, {path});
            require(packaged.contains(path) && same_bytes(f, packaged.at(path)),
                    "Initial package bytes mismatch");
            require(f.size <= unpack_limit - size, "Initial-data size limit exceeded");
            size += f.size;
        }
        require(populated == initial.folders, "Initial-data folders must contain files");
        for (const auto &[platform, target] : m.platforms) {
            Paths combined;
            for (const auto &[path, f] : target.files) {
                require(!initial_data_path(m, path), "Initial-data folders overlap managed game files");
                combined.add(path);
            }
            for (const auto &[path, f] : initial.files)
                combined.add(path);
            for (const auto &[locale, language] : m.languages)
                combined.add(language.file.path);
        }
    }
    require(used.size() == m.packages.size(), "Unreferenced packages");
    return m;
}
void enforce_sequence(const Manifest &target, std::uint64_t highest, std::string_view accepted_hash) {
    if (target.sequence < highest || (target.sequence == highest && target.payload_hash != accepted_hash))
        fail(ErrorCategory::Signature, "Release downgrade or same-sequence mutation rejected");
}
Inventory desired_files(const Manifest &m, const std::string &platform, const std::string &language) {
    if (!m.platforms.contains(platform) || !m.languages.contains(language))
        fail(ErrorCategory::Manifest, "Unknown platform or language");
    auto result = m.platforms.at(platform).files;
    const auto &f = m.languages.at(language).file;
    result.emplace(f.path, f);
    return result;
}
bool initial_data_path(const Manifest &m, std::string_view path) {
    if (data_path(path)) {
        return m.initial_data.data_file_keys.contains(path_key(path));
    }
    const auto top = path_key(path.substr(0, path.find('/')));
    return std::any_of(m.initial_data.folders.begin(), m.initial_data.folders.end(),
                       [&](const auto &folder) { return path_key(folder) == top; });
}
bool data_path(std::string_view path) {
    return path.size() > 5 && path_key(path.substr(0, 5)) == "data/";
}
std::set<std::string> initial_reset_paths(const Manifest &m) {
    std::set<std::string> result;
    for (const auto &folder : m.initial_data.folders)
        if (path_key(folder) != "data")
            result.insert(folder);
    for (const auto &[path, file] : m.initial_data.files)
        if (data_path(path))
            result.insert(path);
    return result;
}
} // namespace launcher
