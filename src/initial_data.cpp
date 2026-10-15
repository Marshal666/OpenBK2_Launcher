#include "launcher/initial_data.hpp"
#include "build_config.hpp"
#include "json.hpp"
#include "launcher/error.hpp"
#include "launcher/platform_services.hpp"
#include <regex>

namespace launcher {
namespace {
constexpr auto journal_path = ".launcher/initial-reset.json";
struct Reset {
    std::string id, hash, phase = "commit";
    std::map<std::string, bool> paths{};
    bool retain = build::retain_user_backups;
};
void step(const FaultHook &hook, std::string_view text) {
    if (hook)
        hook(text);
}
bool directory_exists(const SafeRoot &root, const std::string &relative) {
    validate_relative_path(relative);
    const auto path = root.path() / utf8_path(relative);
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found)
        return false;
    if (error)
        fail(ErrorCategory::Filesystem, "Cannot inspect reset folder");
    SafeRoot checked(path); // Reject files, symlinks, reparse points and unsafe ancestors.
    return true;
}
void inspect_tree(const SafeRoot &folder) {
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(folder.path())) {
        if (++count > 200000)
            fail(ErrorCategory::Filesystem, "Reset folder exceeds entry limit");
        if (entry.is_directory()) {
            SafeRoot checked(entry.path());
        } else {
            auto checked =
                folder.open(path_utf8(entry.path().lexically_relative(folder.path())), FileMode::Read);
        }
    }
}
bool scope_exists(const SafeRoot &root, const std::string &path, bool file) {
    if (!file)
        return directory_exists(root, path);
    if (!root.exists(path))
        return false;
    auto checked = root.open(path, FileMode::Read);
    return true;
}
void move_scope(const SafeRoot &root, const std::string &from, const std::string &to, bool file) {
    if (file)
        root.rename(from, to);
    else
        root.rename_directory(from, to);
}
std::string encode(const Reset &reset) {
    std::string text = "{\"reset_schema_version\":2,\"id\":" + json::quote(reset.id) +
                       ",\"payload_sha256\":" + json::quote(reset.hash) +
                       ",\"phase\":" + json::quote(reset.phase) +
                       ",\"retain_backups\":" + (reset.retain ? "true" : "false") + ",\"paths\":{";
    bool first = true;
    for (const auto &[folder, exists] : reset.paths) {
        if (!first)
            text += ',';
        first = false;
        text += json::quote(folder) + ':' + (exists ? "true" : "false");
    }
    return text + "}}";
}
Reset decode(std::string_view text, const Manifest &manifest) {
    using namespace json;
    auto doc = parse(text);
    const auto value = yyjson_doc_get_root(doc.get());
    const auto schema = number(get(value, "reset_schema_version"));
    require(schema == 1 || schema == 2, "Unsupported reset journal");
    const char *paths_field = schema == 1 ? "folders" : "paths";
    fields(value, {"reset_schema_version", "id", "payload_sha256", "phase", "retain_backups", paths_field});
    Reset reset;
    reset.id = string(get(value, "id"));
    reset.hash = string(get(value, "payload_sha256"));
    reset.phase = string(get(value, "phase"));
    require(std::regex_match(reset.id, std::regex("[a-f0-9]{32}")) && reset.hash == manifest.payload_hash &&
                (reset.phase == "commit" || reset.phase == "rollback"),
            "Invalid reset identity");
    require(yyjson_is_bool(get(value, "retain_backups")), "Invalid reset backup policy");
    reset.retain = yyjson_get_bool(get(value, "retain_backups"));
    const auto authorized = initial_reset_paths(manifest);
    for (const auto &[folder, existed] : object(get(value, paths_field))) {
        require(authorized.contains(folder) && yyjson_is_bool(existed) &&
                    (schema == 2 || !data_path(folder)),
                "Reset path is not authorized by installed signature");
        reset.paths.emplace(folder, yyjson_get_bool(existed));
    }
    require(reset.paths.size() == authorized.size() && !reset.paths.empty(),
            "Reset journal has incomplete path inventory");
    return reset;
}
bool matches_scope(const SafeRoot &root, const Manifest &manifest, const std::string &folder,
                    const std::string &prefix) {
    if (data_path(folder)) {
        auto expected = manifest.initial_data.files.at(folder);
        expected.path = prefix + folder;
        return file_matches(root, expected);
    }
    for (const auto &[path, f] : manifest.initial_data.files)
        if (path.starts_with(folder + '/')) {
            auto expected = f;
            expected.path = prefix + path;
            if (!file_matches(root, expected))
                return false;
        }
    return true;
}
} // namespace

TransactionResult recover_initial_data(const SafeRoot &root, const TrustedKeys &keys, ManifestPolicy policy,
                                       FaultHook checkpoint) {
    TransactionResult result;
    if (!root.exists(journal_path)) {
        discard_launcher_tree(root, ".launcher/resets");
        return result;
    }
    if (root.exists(".launcher/transaction.json"))
        fail(ErrorCategory::Recovery, "Conflicting installation and initial-data transactions");
    auto local = load_local(root, keys, policy);
    if (!local.installed)
        fail(ErrorCategory::Recovery, "Reset requires authenticated installed metadata");
    const auto &manifest = local.installed->manifest;
    auto reset = decode(root.read(journal_path, 16 * 1024 * 1024), manifest);
    const auto prefix = ".launcher/resets/" + reset.id + '/';
    bool complete = true;
    for (const auto &[folder, existed] : reset.paths) {
        (void)existed;
        if (scope_exists(root, prefix + "new/" + folder, data_path(folder)))
            complete &= matches_scope(root, manifest, folder, prefix + "new/");
        else
            complete &= scope_exists(root, folder, data_path(folder)) && matches_scope(root, manifest, folder, "");
    }
    if (!complete && reset.phase != "rollback") {
        reset.phase = "rollback";
        root.write_atomic(journal_path, encode(reset), checkpoint);
    }
    for (const auto &[folder, existed] : reset.paths) {
        const auto staged = prefix + "new/" + folder, original = prefix + "original/" + folder;
        const bool file = data_path(folder), has_stage = scope_exists(root, staged, file),
                   has_original = scope_exists(root, original, file);
        if (reset.phase == "rollback") {
            if (has_original || (!existed && !has_stage)) {
                if (scope_exists(root, folder, file))
                    move_scope(root, folder, prefix + "discard/" + folder, file);
                step(checkpoint, "reset rollback replacement moved");
                if (has_original)
                    move_scope(root, original, folder, file);
                step(checkpoint, "reset original restored");
            }
        } else if (has_stage) {
            if (existed && !has_original) {
                move_scope(root, folder, original, file);
                step(checkpoint, "reset original moved");
            }
            move_scope(root, staged, folder, file);
            step(checkpoint, "reset defaults published");
        }
    }
    if (reset.phase == "commit")
        for (const auto &[folder, existed] : reset.paths)
            if (!matches_scope(root, manifest, folder, ""))
                fail(ErrorCategory::Recovery, "Reset defaults changed during commit");
    if (reset.retain && reset.phase == "commit" && directory_exists(root, prefix + "original")) {
        const auto backup = ".launcher/backups/reset-data/" + reset.id;
        root.rename_directory(prefix + "original", backup);
        result.preserved.push_back(backup);
    }
    root.remove(journal_path);
    step(checkpoint, "reset journal removed");
    result.rolled_back = reset.phase == "rollback";
    try {
        discard_launcher_tree(root, ".launcher/resets/" + reset.id);
    } catch (const std::exception &error) {
        result.warnings.push_back(error.what());
    }
    return result;
}

TransactionResult Installer::reset_initial_data(PackageFetcher fetch, std::stop_token stop,
                                                ProgressSink progress, FaultHook checkpoint) {
    auto lock = root_.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    recover_initial_data(root_, keys_, policy_, checkpoint);
    auto local = load_local(root_, keys_, policy_);
    if (!local.installed || local.pending_recovery)
        fail(ErrorCategory::Recovery, "Install or recover the game before resetting initial data");
    const auto &manifest = local.installed->manifest;
    const auto &launch = manifest.platforms.at(local.installed->platform).launch;
    if (game_process_running(root_.path() / utf8_path(launch.executable)))
        fail(ErrorCategory::GameRunning, "Close the game before resetting initial data");
    if (manifest.initial_data.folders.empty())
        fail(ErrorCategory::Manifest, "Installed release has no initial data");
    Reset reset{unique_id(), manifest.payload_hash};
    for (const auto &folder : initial_reset_paths(manifest)) {
        const bool existed = scope_exists(root_, folder, data_path(folder));
        if (existed && !data_path(folder))
            inspect_tree(SafeRoot(root_.path() / utf8_path(folder)));
        reset.paths.emplace(folder, existed);
    }
    std::uint64_t required = 16 * 1024 * 1024;
    for (const auto &id : manifest.initial_data.packages) {
        const auto &p = manifest.packages.at(id);
        required += p.size + p.unpacked_size;
    }
    if (required > std::filesystem::space(root_.path()).available)
        fail(ErrorCategory::DiskSpace, "Insufficient working space for initial-data reset");
    const auto prefix = ".launcher/resets/" + reset.id + "/new";
    {
        SafeRoot stage(root_.path() / utf8_path(prefix), true);
        for (const auto &id : manifest.initial_data.packages) {
            const auto &p = manifest.packages.at(id);
            ZipPackageReader{}.extract(fetch(p, stop, progress), p, stage, stop, progress);
        }
        for (const auto &[path, f] : manifest.initial_data.files) {
            if (!file_matches(stage, f, stop))
                fail(ErrorCategory::PackageIntegrity, "Initial-data staging mismatch");
            auto file = stage.open(path, FileMode::Append);
            file.set_executable(false);
            file.flush();
            SafeRoot(stage.path() / utf8_path(path).parent_path()).flush_directory();
        }
    }
    if (stop.stop_requested())
        fail(ErrorCategory::Cancelled, "Initial-data reset cancelled before commit");
    if (game_process_running(root_.path() / utf8_path(launch.executable)))
        fail(ErrorCategory::GameRunning, "Close the game before resetting initial data");
    root_.write_atomic(journal_path, encode(reset), checkpoint);
    step(checkpoint, "reset prepared");
    auto result = recover_initial_data(root_, keys_, policy_, checkpoint);
    if (result.rolled_back)
        fail(ErrorCategory::Recovery, "Reset could not finish; previous folders restored");
    return result;
}
} // namespace launcher
