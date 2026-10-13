#include "launcher/mod_manager.hpp"
#include "launcher/error.hpp"
#include "launcher/platform_services.hpp"
#include <algorithm>

namespace launcher {
namespace {
bool owned(const LocalInstallation &local, const std::string &id) {
    if (!local.authenticated_manifest)
        return false;
    const auto folder = path_key("MODs/" + id);
    for (const auto &[platform, target] : local.authenticated_manifest->platforms) {
        (void)platform;
        for (const auto &[path, file] : target.files) {
            (void)file;
            const auto key = path_key(path);
            if (key == folder || key.starts_with(folder + "/") || folder.starts_with(key + "/"))
                return true;
        }
    }
    return false;
}
LocalInstallation idle_installation(const SafeRoot &root, const TrustedKeys &keys, ManifestPolicy policy) {
    auto local = load_local(root, keys, policy);
    if (local.pending_recovery)
        fail(ErrorCategory::Recovery, "Recover the game installation before changing mods");
    if (!local.installed)
        fail(ErrorCategory::Manifest, "Install the game before changing mods");
    const auto &launch = local.installed->manifest.platforms.at(local.installed->platform).launch;
    if (game_process_running(root.path() / utf8_path(launch.executable)))
        fail(ErrorCategory::GameRunning, "Close the game before changing mods");
    return local;
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        fail(ErrorCategory::Cancelled, "Mod download cancelled before publication");
}
} // namespace
std::vector<LocalMod> ModManager::list() const {
    std::vector<LocalMod> result;
    const auto folder = root_.path() / "MODs";
    if (!std::filesystem::exists(std::filesystem::symlink_status(folder)))
        return result;
    SafeRoot mods(folder); // Pins and validates even an empty directory.
    const auto local = load_local(root_, keys_, policy_);
    std::set<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(mods.path())) {
        const auto id = path_utf8(entry.path().filename());
        validate_relative_path(id);
        if (!names.insert(path_key(id)).second)
            fail(ErrorCategory::Filesystem, "Case-aliased entries in MODs require manual inspection");
        if (entry.is_directory()) {
            SafeRoot checked(entry.path());
            result.push_back({id, owned(local, id)});
        } else {
            auto checked = mods.open(id, FileMode::Read); // Reject links and special files.
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}
void ModManager::download(std::string_view envelope, const std::string &id, PackageFetcher fetch,
                          std::stop_token stop, ProgressSink progress, FaultHook checkpoint) {
    auto lock = root_.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    auto local = idle_installation(root_, keys_, policy_);
    const auto catalog = verify_manifest(envelope, keys_, policy_);
    enforce_sequence(catalog, local.installed->manifest.sequence, local.installed->manifest.payload_hash);
    if (catalog.payload_hash != local.installed->manifest.payload_hash)
        fail(ErrorCategory::Manifest, "Update the game before downloading mods from this release");
    if (!catalog.mods.contains(id))
        fail(ErrorCategory::Manifest, "Mod is absent from the signed catalog");
    if (!list().empty())
        fail(ErrorCategory::Filesystem, "Remove the installed mod first; the game loads one mod at a time");
    if (!verify_installation(root_, *local.installed, stop))
        fail(ErrorCategory::PackageIntegrity, "Repair game files before downloading mods");
    const auto &mod = catalog.mods.at(id);
    std::uint64_t required = 16 * 1024 * 1024;
    for (const auto &package : mod.packages) {
        const auto &p = catalog.packages.at(package);
        required += p.size + p.unpacked_size;
    }
    if (required > std::filesystem::space(root_.path()).available)
        fail(ErrorCategory::DiskSpace, "Insufficient space for mod download and staging");
    const auto prefix = ".launcher/staging/mod-" + unique_id();
    {
        SafeRoot stage(root_.path() / utf8_path(prefix), true);
        for (const auto &package : mod.packages) {
            cancelled(stop);
            const auto &p = catalog.packages.at(package);
            ZipPackageReader{}.extract(fetch(p, stop, progress), p, stage, stop, progress);
        }
        for (const auto &[path, file] : mod.files) {
            cancelled(stop);
            if (!file_matches(stage, file, stop))
                fail(ErrorCategory::PackageIntegrity, "Mod staged inventory mismatch");
            auto output = stage.open(path, FileMode::Append);
            output.set_executable(file.executable);
            output.flush();
            SafeRoot(stage.path() / utf8_path(path).parent_path()).flush_directory();
        }
    }
    cancelled(stop);
    idle_installation(root_, keys_, policy_);
    if (!list().empty())
        fail(ErrorCategory::Filesystem, "MODs changed during download; no mod was published");
    if (checkpoint)
        checkpoint("before mod publication");
    // Cancellation is deferred across this single atomic commit.
    root_.rename_directory(prefix + "/MODs/" + id, "MODs/" + id);
    if (checkpoint)
        checkpoint("after mod publication");
    if (progress)
        progress({"Mod installed", 1, 1, 0});
}
std::string ModManager::remove(const std::string &id, FaultHook checkpoint) {
    validate_relative_path(id);
    if (id.find('/') != std::string::npos)
        fail(ErrorCategory::Filesystem, "Select a direct child folder of MODs");
    auto lock = root_.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    const auto local = idle_installation(root_, keys_, policy_);
    if (owned(local, id))
        fail(ErrorCategory::Filesystem,
             "This folder contains signed base-game files; it cannot be removed as a mod");
    const auto mods = list();
    if (std::none_of(mods.begin(), mods.end(), [&](const auto &m) { return m.id == id; }))
        fail(ErrorCategory::Filesystem, "Selected mod folder is missing");
    {
        SafeRoot folder(root_.path() / "MODs" / utf8_path(id));
        // Do not traverse links or delete anything. Validate ordinary entries,
        // then retain the entire tree (including unrecognized user files).
        std::size_t entries = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(folder.path())) {
            if (++entries > 200000)
                fail(ErrorCategory::Filesystem, "Mod folder exceeds the entry limit");
            const auto relative = entry.path().lexically_relative(folder.path()).generic_string();
            validate_relative_path(relative);
            if (entry.is_directory()) {
                SafeRoot checked(entry.path());
            } else {
                auto checked = folder.open(relative, FileMode::Read);
            }
        }
    }
    const auto backup = ".launcher/backups/removed-mods/" + unique_id() + "/" + id;
    if (checkpoint)
        checkpoint("before mod removal");
    root_.rename_directory("MODs/" + id, backup);
    if (checkpoint)
        checkpoint("after mod removal");
    return backup;
}
} // namespace launcher
