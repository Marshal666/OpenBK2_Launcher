#include "launcher/installer.hpp"
#include "build_config.hpp"
#include "launcher/error.hpp"
#include "launcher/initial_data.hpp"
#include "launcher/sha256.hpp"

namespace launcher {
TransactionResult Installer::recover(FaultHook checkpoint, ProgressSink progress) {
    auto lock = root_.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    auto initial = recover_initial_data(root_, keys_, policy_, checkpoint);
    auto result = Transaction(root_, keys_, policy_, std::move(checkpoint), std::move(progress)).recover();
    result.warnings.insert(result.warnings.end(), initial.warnings.begin(), initial.warnings.end());
    result.rolled_back |= initial.rolled_back;
    return result;
}
InstallResult Installer::install(std::string_view envelope, const std::string &platform,
                                 const std::string &language, PackageFetcher fetch, InstallOptions options,
                                 std::stop_token stop, ProgressSink progress) {
    auto lock = root_.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    recover_initial_data(root_, keys_, policy_, options.checkpoint);
    Transaction transaction(root_, keys_, policy_, options.checkpoint, progress);
    auto recovered = transaction.recover();
    if (progress)
        progress({"Checking", 0, 0, 0});
    auto target = verify_manifest(envelope, keys_, policy_);
    auto local = load_local(root_, keys_, policy_);
    auto plan = plan_update(root_, local, target, platform, language, options.mode, stop);
    const auto available = options.available_bytes.value_or(std::filesystem::space(root_.path()).available);
    if (plan.working_bytes > available)
        fail(ErrorCategory::DiskSpace,
             "Insufficient working space; need " + std::to_string(plan.working_bytes) + " bytes");
    std::uint64_t retained = 0;
    const auto backups = root_.path() / ".launcher/backups";
    if (build::retain_user_backups && std::filesystem::exists(backups)) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(backups)) {
            if (entry.is_symlink())
                fail(ErrorCategory::Filesystem, "Linked backup entry requires manual inspection");
            if (entry.is_regular_file()) {
                const auto size = entry.file_size();
                if (size >
                    options.preserved_backup_limit - std::min(retained, options.preserved_backup_limit))
                    fail(ErrorCategory::DiskSpace,
                         "Backup retention limit reached; archive preserved files before updating");
                retained += size;
            }
        }
    }
    std::vector<std::string> affected = plan.remove;
    for (const auto &[path, f] : plan.changed) {
        (void)f;
        affected.push_back(path);
    }
    if (build::retain_user_backups)
        for (const auto &path : affected)
            if (root_.exists(path)) {
                auto file = root_.open(path, FileMode::Read);
                const bool preserve = !local.installed || !local.installed->files.contains(path) ||
                                      sha256_file(file, stop) != local.installed->files.at(path).sha256;
                if (preserve) {
                    if (file.size() >
                        options.preserved_backup_limit - std::min(retained, options.preserved_backup_limit))
                        fail(ErrorCategory::DiskSpace,
                             "User-file backups would exceed the retention limit; no files changed");
                    retained += file.size();
                }
            }
    InstallResult result;
    result.preserved = std::move(recovered.preserved);
    result.warnings = std::move(recovered.warnings);
    auto id = unique_id();
    auto prepare = [&](const UpdatePlan &candidate) {
        const auto prefix = ".launcher/staging/" + id + "/";
        unsigned package_index = 0;
        for (const auto &package_id : candidate.packages) {
            if (stop.stop_requested())
                fail(ErrorCategory::Cancelled, "Installation cancelled before commit");
            const auto &package = target.packages.at(package_id);
            auto archive = fetch(package, stop, progress);
            const auto package_prefix = prefix + "packages/p" + std::to_string(package_index++) + "/";
            SafeRoot package_stage(root_.path() / utf8_path(package_prefix), true);
            ZipPackageReader{}.extract(archive, package, package_stage, stop, progress);
            for (const auto &[path, f] : package.files)
                if (candidate.changed.contains(path)) {
                    const auto &final = candidate.changed.at(path);
                    if (final.size == f.size && final.sha256 == f.sha256 &&
                        !root_.exists(prefix + "final/" + path))
                        copy_verified(package_stage, path, root_, prefix + "final/" + path, {}, progress,
                                      stop);
                }
            for (const auto &[path, f] : package.files) {
                (void)f;
                package_stage.remove(path);
            }
        }
        for (const auto &[path, f] : candidate.changed) {
            auto staged = f;
            staged.path = prefix + "final/" + path;
            if (!file_matches(root_, staged, stop))
                fail(ErrorCategory::PackageIntegrity, "Packages cannot construct final target: " + path);
        }
    };
    try {
        prepare(plan);
    } catch (const Error &error) {
        if (!plan.patch_chain || error.category == ErrorCategory::Cancelled ||
            error.category == ErrorCategory::DiskSpace || error.category == ErrorCategory::Filesystem)
            throw;
        if (progress)
            progress({"Patch unavailable; downloading current snapshot", 0, 0, 0});
        id = unique_id();
        plan = plan_update(root_, local, target, platform, language, InstallMode::Repair, stop);
        if (plan.working_bytes > std::filesystem::space(root_.path()).available)
            fail(ErrorCategory::DiskSpace, "Insufficient space for snapshot fallback");
        result.used_snapshot_fallback = true;
        prepare(plan);
    }
    if (stop.stop_requested())
        fail(ErrorCategory::Cancelled, "Installation cancelled before commit");
    if (progress)
        progress({"Preparing backups", 0, 0, 0});
    transaction.prepare(id, local, target, platform, language, plan);
    // Cancellation is intentionally deferred until the durable commit finishes.
    auto committed = transaction.recover();
    if (committed.rolled_back)
        fail(ErrorCategory::Recovery, "Prepared data was unavailable; previous files restored");
    result.used_patch_chain = plan.patch_chain;
    result.preserved.insert(result.preserved.end(), committed.preserved.begin(), committed.preserved.end());
    result.warnings.insert(result.warnings.end(), committed.warnings.begin(), committed.warnings.end());
    if (progress)
        progress({"Ready", 1, 1, 0});
    return result;
}
} // namespace launcher
