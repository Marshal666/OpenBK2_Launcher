#include "launcher/transaction.hpp"
#include "json.hpp"
#include "launcher/error.hpp"
#include "launcher/sha256.hpp"
#include <algorithm>
#include <array>
#include <picosha2.h>
#include <regex>

namespace launcher {
namespace {
constexpr auto journal_path = ".launcher/transaction.json";
struct Operation {
    std::string path, action, original_hash;
    std::uint64_t original_size = 0;
};
struct Journal {
    std::string id, platform, language, phase = "commit";
    std::uint64_t old_sequence = 0, new_sequence = 0;
    std::vector<Operation> operations;
};
std::string staging(const std::string &id) {
    return ".launcher/staging/" + id + "/";
}
std::string backup(const std::string &id) {
    return ".launcher/backups/" + id + "/";
}
void step(const FaultHook &hook, std::string_view name) {
    if (hook)
        hook(name);
}
std::string encode(const Journal &j) {
    using json::quote;
    std::string out = "{\"transaction_schema_version\":1,\"transaction_id\":" + quote(j.id) +
                      ",\"platform\":" + quote(j.platform) + ",\"language\":" + quote(j.language) +
                      ",\"phase\":" + quote(j.phase) + ",\"old_sequence\":" + std::to_string(j.old_sequence) +
                      ",\"new_sequence\":" + std::to_string(j.new_sequence) + ",\"operations\":[";
    bool first = true;
    for (const auto &op : j.operations) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"path\":" + quote(op.path) + ",\"action\":" + quote(op.action) +
               ",\"original_sha256\":" + quote(op.original_hash) +
               ",\"original_size\":" + std::to_string(op.original_size) + "}";
    }
    return out + "]}";
}
Journal decode(std::string_view bytes) {
    using namespace json;
    auto doc = parse(bytes);
    auto value = yyjson_doc_get_root(doc.get());
    fields(value, {"transaction_schema_version", "transaction_id", "platform", "language", "phase",
                   "old_sequence", "new_sequence", "operations"});
    require(number(get(value, "transaction_schema_version")) == 1, "Unsupported transaction schema");
    Journal j;
    j.id = string(get(value, "transaction_id"));
    j.platform = string(get(value, "platform"));
    j.language = string(get(value, "language"));
    j.phase = string(get(value, "phase"));
    j.old_sequence = number(get(value, "old_sequence"));
    j.new_sequence = number(get(value, "new_sequence"));
    require(std::regex_match(j.id, std::regex("[a-f0-9]{32}")) &&
                (j.phase == "commit" || j.phase == "rollback"),
            "Invalid transaction identity or phase");
    std::set<std::string> seen;
    for (auto operation : array(get(value, "operations"))) {
        fields(operation, {"path", "action", "original_sha256", "original_size"});
        Operation op{string(get(operation, "path")), string(get(operation, "action")),
                     string(get(operation, "original_sha256")), number(get(operation, "original_size"))};
        validate_relative_path(op.path);
        require(seen.insert(path_key(op.path)).second && (op.action == "replace" || op.action == "remove"),
                "Invalid transaction operations");
        require(op.original_hash.empty() || std::regex_match(op.original_hash, std::regex("[a-f0-9]{64}")),
                "Invalid original digest");
        j.operations.push_back(std::move(op));
    }
    return j;
}
bool matches_original(const SafeRoot &root, const Operation &op) {
    if (op.original_hash.empty())
        return !root.exists(op.path);
    return file_matches(root, {op.path, op.original_size, op.original_hash, false});
}
void install_copy(const SafeRoot &root, const std::string &from, const std::string &path,
                  const std::string &live_backup, const FaultHook &hook, const ProgressSink &progress) {
    const auto temp = ".launcher/staging/apply-" + unique_id();
    copy_verified(root, from, root, temp, hook, progress);
    if (root.exists(path)) {
        root.rename(path, live_backup);
        step(hook, "live file moved to recovery backup");
    }
    // Never replace a new file that appeared after we moved the original away.
    root.rename(temp, path);
    step(hook, "live file renamed");
}
void cleanup_completed(const SafeRoot &root, const Journal &j, const Inventory &desired,
                       TransactionResult &result) {
    try {
        for (const auto &op : j.operations) {
            const auto name = backup(j.id) + "original/" + op.path;
            if (!op.original_hash.empty() &&
                std::find(result.preserved.begin(), result.preserved.end(), name) == result.preserved.end())
                root.remove(name);
            for (const auto *kind : {"live/", "rollback-live/"}) {
                const auto live = backup(j.id) + kind + op.path;
                if (root.exists(live)) {
                    auto file = root.open(live, FileMode::Read);
                    const auto hash = sha256_file(file);
                    if (hash != op.original_hash &&
                        (!desired.contains(op.path) || hash != desired.at(op.path).sha256) &&
                        std::find(result.preserved.begin(), result.preserved.end(), live) ==
                            result.preserved.end())
                        result.preserved.push_back(live);
                }
                if (std::find(result.preserved.begin(), result.preserved.end(), live) ==
                    result.preserved.end())
                    root.remove(live);
            }
            if (op.action == "replace")
                root.remove(staging(j.id) + "final/" + op.path);
        }
        for (const auto *name : {"target.envelope.json", "old.envelope.json", "old.state.json"})
            root.remove(staging(j.id) + name);
    } catch (const std::exception &error) {
        result.warnings.push_back(std::string("Completed installation; scratch cleanup failed: ") +
                                  error.what());
    }
}
// Only the launcher's reserved scratch area is eligible. A durable journal
// prevents this reclamation entirely; downloads and preserved backups are separate.
void reclaim_staging(const SafeRoot &root, unsigned depth = 0, std::size_t *visited = nullptr) {
    std::size_t count = 0;
    if (!visited)
        visited = &count;
    if (depth > 64)
        fail(ErrorCategory::Filesystem, "Scratch tree exceeds cleanup depth limit");
    for (const auto &entry : std::filesystem::directory_iterator(root.path())) {
        if (++*visited > 200000)
            fail(ErrorCategory::Filesystem, "Scratch tree exceeds cleanup entry limit");
        if (entry.is_directory()) {
            SafeRoot child(entry.path()); // Reject links/reparse points before descent.
            reclaim_staging(child, depth + 1, visited);
        } else {
            const auto name = entry.path().filename().u8string();
            root.remove(std::string(name.begin(), name.end()));
        }
    }
}
} // namespace
std::string copy_verified(const SafeRoot &source, std::string_view from, const SafeRoot &destination,
                          std::string_view to, FaultHook checkpoint, ProgressSink progress,
                          std::stop_token stop) {
    auto input = source.open(from, FileMode::Read);
    auto output = destination.open(to, FileMode::CreateNew);
    const auto size = input.size();
    std::uint64_t offset = 0;
    std::array<char, 65536> buffer{};
    picosha2::hash256_one_by_one hash;
    while (offset < size) {
        if (stop.stop_requested())
            fail(ErrorCategory::Cancelled, "Copy cancelled before commit");
        const auto count = input.read_at(
            offset,
            {buffer.data(), static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - offset))});
        if (!count)
            fail(ErrorCategory::Filesystem, "Source changed during copy");
        output.write({buffer.data(), count});
        hash.process(buffer.begin(), buffer.begin() + count);
        offset += count;
        step(checkpoint, "file chunk written");
        if (progress)
            progress({"Installing", offset, size, 0});
    }
    if (input.size() != size)
        fail(ErrorCategory::Filesystem, "Source changed size during copy");
    output.set_executable(input.executable());
    output.flush();
    step(checkpoint, "copied file flushed");
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}
void Transaction::prepare(const std::string &id, const LocalInstallation &old, const Manifest &target,
                          const std::string &platform, const std::string &language, const UpdatePlan &plan) {
    if (root_.exists(journal_path))
        fail(ErrorCategory::Recovery, "A transaction already needs recovery");
    Journal j{id,
              platform,
              language,
              "commit",
              old.authenticated_manifest ? old.authenticated_manifest->sequence : 0,
              target.sequence,
              {}};
    const auto prefix = staging(id);
    root_.write_atomic(prefix + "target.envelope.json", target.envelope, checkpoint_);
    if (old.authenticated_manifest)
        root_.write_atomic(prefix + "old.envelope.json", old.authenticated_manifest->envelope, checkpoint_);
    if (old.installed)
        root_.write_atomic(prefix + "old.state.json", root_.read(".launcher/state.json", 16 * 1024 * 1024),
                           checkpoint_);
    auto add = [&](const std::string &path, const char *action) {
        Operation op{path, action, "", 0};
        if (root_.exists(path)) {
            {
                auto file = root_.open(path, FileMode::Read);
                op.original_size = file.size();
            }
            op.original_hash =
                copy_verified(root_, path, root_, backup(id) + "original/" + path, checkpoint_, progress_);
            if (!matches_original(root_, op))
                fail(ErrorCategory::Filesystem, "File changed while preparing a backup: " + path);
        }
        j.operations.push_back(std::move(op));
    };
    for (const auto &[path, f] : plan.changed) {
        auto staged = f;
        staged.path = prefix + "final/" + path;
        if (!file_matches(root_, staged))
            fail(ErrorCategory::PackageIntegrity, "Prepared target bytes are missing: " + path);
        add(path, "replace");
    }
    // All replacement operations, including the new language, precede removals.
    for (const auto &path : plan.remove)
        add(path, "remove");
    // Probe every live destination before making any file destructive. A running
    // Windows image refuses a write-capable handle here.
    const auto &executable = target.platforms.at(platform).launch.executable;
    if (root_.exists(executable)) {
        auto probe = root_.open(executable, FileMode::Append);
        (void)probe;
    }
    for (const auto &op : j.operations)
        if (root_.exists(op.path)) {
            auto probe = root_.open(op.path, FileMode::Append);
            (void)probe;
        }
    root_.write_atomic(journal_path, encode(j), checkpoint_);
    step(checkpoint_, "transaction prepared");
}
TransactionResult Transaction::recover() {
    TransactionResult result;
    if (!root_.exists(journal_path)) {
        try {
            const auto scratch = root_.path() / ".launcher/staging";
            if (std::filesystem::exists(scratch))
                reclaim_staging(SafeRoot(scratch));
        } catch (const std::exception &error) {
            result.warnings.push_back(std::string("Unused staging cleanup: ") + error.what());
        }
        return result;
    }
    auto j = decode(root_.read(journal_path, 16 * 1024 * 1024));
    const auto prefix = staging(j.id), back = backup(j.id);
    auto target =
        verify_manifest(root_.read(prefix + "target.envelope.json", 16 * 1024 * 1024), keys_, policy_);
    auto desired = desired_files(target, j.platform, j.language);
    std::optional<InstalledState> old;
    std::optional<Manifest> old_manifest;
    if (root_.exists(prefix + "old.envelope.json"))
        old_manifest =
            verify_manifest(root_.read(prefix + "old.envelope.json", 16 * 1024 * 1024), keys_, policy_);
    if (old_manifest && root_.exists(prefix + "old.state.json"))
        old = parse_state(root_.read(prefix + "old.state.json", 16 * 1024 * 1024), *old_manifest);
    if (old_manifest)
        enforce_sequence(target, old_manifest->sequence, old_manifest->payload_hash);
    if (j.new_sequence != target.sequence || j.old_sequence != (old_manifest ? old_manifest->sequence : 0))
        fail(ErrorCategory::Recovery, "Journal sequence does not match signatures");
    bool can_finish = true;
    for (const auto &op : j.operations) {
        if (op.action == "replace") {
            if (!desired.contains(op.path))
                fail(ErrorCategory::Recovery, "Journal replacement is not authorized by target signature");
            if (!file_matches(root_, desired.at(op.path))) {
                auto staged = desired.at(op.path);
                staged.path = prefix + "final/" + op.path;
                if (!file_matches(root_, staged))
                    can_finish = false;
            }
        } else if (!old || !old->files.contains(op.path) || desired.contains(op.path))
            fail(ErrorCategory::Recovery, "Journal removal is not authorized by previous signature");
        if (!op.original_hash.empty()) {
            if (!file_matches(root_,
                              {back + "original/" + op.path, op.original_size, op.original_hash, false}))
                fail(ErrorCategory::Recovery,
                     "Recovery backup missing or corrupt; refusing destructive changes");
            if (!old || !old->files.contains(op.path) || old->files.at(op.path).sha256 != op.original_hash)
                result.preserved.push_back(back + "original/" + op.path);
        }
        const auto live = back + "live/" + op.path;
        if (root_.exists(live)) {
            auto file = root_.open(live, FileMode::Read);
            if (sha256_file(file) != op.original_hash)
                result.preserved.push_back(live);
        }
    }
    if (!can_finish && j.phase != "rollback") {
        j.phase = "rollback";
        root_.write_atomic(journal_path, encode(j), checkpoint_);
    }
    if (j.phase == "rollback") {
        for (auto it = j.operations.rbegin(); it != j.operations.rend(); ++it) {
            const auto &op = *it;
            if (matches_original(root_, op))
                continue;
            if (root_.exists(op.path) &&
                (!desired.contains(op.path) || !file_matches(root_, desired.at(op.path)))) {
                const auto preserve = back + "late-" + unique_id() + "/" + op.path;
                copy_verified(root_, op.path, root_, preserve, checkpoint_, progress_);
                result.preserved.push_back(preserve);
            }
            if (op.original_hash.empty()) {
                if (root_.exists(op.path))
                    root_.rename(op.path, back + "rollback-live/" + op.path);
            } else
                install_copy(root_, back + "original/" + op.path, op.path, back + "rollback-live/" + op.path,
                             checkpoint_, progress_);
            step(checkpoint_, "rollback operation complete");
        }
        if (old_manifest)
            root_.write_atomic(".launcher/installed_manifest.envelope.json", old_manifest->envelope,
                               checkpoint_);
        else
            root_.remove(".launcher/installed_manifest.envelope.json");
        if (old)
            root_.write_atomic(".launcher/state.json",
                               root_.read(prefix + "old.state.json", 16 * 1024 * 1024), checkpoint_);
        else
            root_.remove(".launcher/state.json");
        root_.remove("launcher_installed.txt");
        root_.write_atomic(back + "receipt.json", encode(j), checkpoint_);
        root_.remove(journal_path);
        step(checkpoint_, "rollback journal removed");
        result.rolled_back = true;
        cleanup_completed(root_, j, desired, result);
        return result;
    }
    std::uint64_t completed = 0;
    for (const auto &op : j.operations) {
        if (progress_)
            progress_(
                {"Recovering / Installing", completed, static_cast<std::uint64_t>(j.operations.size()), 0});
        if (op.action == "replace" && file_matches(root_, desired.at(op.path))) {
            ++completed;
            continue;
        }
        if (op.action == "remove" && !root_.exists(op.path)) {
            ++completed;
            continue;
        }
        const auto live = back + "live/" + op.path;
        const bool resume_after_move = !root_.exists(op.path) && root_.exists(live);
        if (!matches_original(root_, op) && !resume_after_move)
            fail(ErrorCategory::Recovery,
                 "Live file changed after backup; preserved and awaiting reconciliation: " + op.path);
        if (op.action == "replace")
            install_copy(root_, prefix + "final/" + op.path, op.path, live, checkpoint_, progress_);
        else {
            root_.rename(op.path, live);
            step(checkpoint_, "managed file moved out of target");
        }
        if (root_.exists(live)) {
            auto file = root_.open(live, FileMode::Read);
            if (sha256_file(file) != op.original_hash &&
                std::find(result.preserved.begin(), result.preserved.end(), live) == result.preserved.end())
                result.preserved.push_back(live);
        }
        ++completed;
    }
    for (const auto &[path, f] : desired) {
        if (!file_matches(root_, f))
            fail(ErrorCategory::Recovery, "Final installation verification failed: " + path);
        auto file = root_.open(path, FileMode::Append);
        file.set_executable(f.executable);
        file.flush();
        step(checkpoint_, "installed file verified and flushed");
    }
    root_.write_atomic(".launcher/installed_manifest.envelope.json", target.envelope, checkpoint_);
    root_.write_atomic(".launcher/state.json", serialize_state(target, j.platform, j.language), checkpoint_);
    root_.write_atomic("launcher_installed.txt",
                       "OpenBK2 Launcher 0.1.0\nGame: " + target.version + "\nPlatform: " + j.platform +
                           "\nLanguage: " + j.language + "\nRelease date: " + target.created_utc +
                           "\nNon-authoritative summary; signed state is in .launcher.\n",
                       checkpoint_);
    root_.write_atomic(back + "receipt.json", encode(j), checkpoint_);
    root_.remove(journal_path);
    step(checkpoint_, "committed journal removed");
    // Normal old copies are disposable only after state and envelope durability.
    // Preserved modifications are never automatically purged.
    cleanup_completed(root_, j, desired, result);
    return result;
}
} // namespace launcher
