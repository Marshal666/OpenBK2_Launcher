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
    std::map<std::string, bool> folders{};
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
std::string encode(const Reset &reset) {
    std::string text = "{\"reset_schema_version\":1,\"id\":" + json::quote(reset.id) +
                       ",\"payload_sha256\":" + json::quote(reset.hash) +
                       ",\"phase\":" + json::quote(reset.phase) +
                       ",\"retain_backups\":" + (reset.retain ? "true" : "false") + ",\"folders\":{";
    bool first = true;
    for (const auto &[folder, exists] : reset.folders) {
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
    fields(value, {"reset_schema_version", "id", "payload_sha256", "phase", "retain_backups", "folders"});
    require(number(get(value, "reset_schema_version")) == 1, "Unsupported reset journal");
    Reset reset;
    reset.id = string(get(value, "id"));
    reset.hash = string(get(value, "payload_sha256"));
    reset.phase = string(get(value, "phase"));
    require(std::regex_match(reset.id, std::regex("[a-f0-9]{32}")) && reset.hash == manifest.payload_hash &&
                (reset.phase == "commit" || reset.phase == "rollback"),
            "Invalid reset identity");
    require(yyjson_is_bool(get(value, "retain_backups")), "Invalid reset backup policy");
    reset.retain = yyjson_get_bool(get(value, "retain_backups"));
    for (const auto &[folder, existed] : object(get(value, "folders"))) {
        require(manifest.initial_data.folders.contains(folder) && yyjson_is_bool(existed),
                "Reset folder is not authorized by installed signature");
        reset.folders.emplace(folder, yyjson_get_bool(existed));
    }
    require(reset.folders.size() == manifest.initial_data.folders.size() && !reset.folders.empty(),
            "Reset journal has incomplete folder inventory");
    return reset;
}
bool matches_folder(const SafeRoot &root, const Manifest &manifest, const std::string &folder,
                    const std::string &prefix) {
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
    auto reset = decode(root.read(journal_path, 1024 * 1024), manifest);
    const auto prefix = ".launcher/resets/" + reset.id + '/';
    bool complete = true;
    for (const auto &[folder, existed] : reset.folders) {
        (void)existed;
        if (directory_exists(root, prefix + "new/" + folder))
            complete &= matches_folder(root, manifest, folder, prefix + "new/");
        else
            complete &= directory_exists(root, folder) && matches_folder(root, manifest, folder, "");
    }
    if (!complete && reset.phase != "rollback") {
        reset.phase = "rollback";
        root.write_atomic(journal_path, encode(reset), checkpoint);
    }
    for (const auto &[folder, existed] : reset.folders) {
        const auto staged = prefix + "new/" + folder, original = prefix + "original/" + folder;
        const bool has_stage = directory_exists(root, staged),
                   has_original = directory_exists(root, original);
        if (reset.phase == "rollback") {
            if (has_original || (!existed && !has_stage)) {
                if (directory_exists(root, folder))
                    root.rename_directory(folder, prefix + "discard/" + folder);
                step(checkpoint, "reset rollback replacement moved");
                if (has_original)
                    root.rename_directory(original, folder);
                step(checkpoint, "reset original restored");
            }
        } else if (has_stage) {
            if (existed && !has_original) {
                root.rename_directory(folder, original);
                step(checkpoint, "reset original moved");
            }
            root.rename_directory(staged, folder);
            step(checkpoint, "reset defaults published");
        }
    }
    if (reset.phase == "commit")
        for (const auto &[folder, existed] : reset.folders)
            if (!matches_folder(root, manifest, folder, ""))
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
    for (const auto &folder : manifest.initial_data.folders) {
        const bool existed = directory_exists(root_, folder);
        if (existed)
            inspect_tree(SafeRoot(root_.path() / utf8_path(folder)));
        reset.folders.emplace(folder, existed);
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
