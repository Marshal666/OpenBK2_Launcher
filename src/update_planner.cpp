#include "launcher/error.hpp"
#include "launcher/planner.hpp"
#include <limits>

namespace launcher {
namespace {
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        fail(ErrorCategory::Manifest, "Planning size overflow");
    return a + b;
}
bool same(const FileRecord &a, const FileRecord &b) {
    return a.path == b.path && a.size == b.size && a.sha256 == b.sha256;
}
} // namespace
UpdatePlan plan_update(const SafeRoot &root, const LocalInstallation &local, const Manifest &target,
                       const std::string &platform, const std::string &language, InstallMode mode,
                       std::stop_token stop) {
    if (local.pending_recovery)
        fail(ErrorCategory::Recovery, "Recover unfinished transaction before planning");
    if (local.authenticated_manifest)
        enforce_sequence(target, local.authenticated_manifest->sequence,
                         local.authenticated_manifest->payload_hash);
    UpdatePlan plan;
    plan.desired = desired_files(target, platform, language);
    for (const auto &[path, f] : plan.desired)
        if (mode == InstallMode::Reinstall || !file_matches(root, f, stop))
            plan.changed.emplace(path, f);
    if (local.installed && local.installed->platform == platform)
        for (const auto &[path, f] : local.installed->files) {
            (void)f;
            if (!plan.desired.contains(path))
                plan.remove.push_back(path);
        }
    const auto &base = target.platforms.at(platform);
    bool needs_base = false;
    for (const auto &[path, f] : plan.changed) {
        (void)f;
        if (base.files.contains(path))
            needs_base = true;
    }
    if (needs_base) {
        plan.packages = base.snapshots;
        std::uint64_t snapshot_cost = 0;
        for (const auto &id : base.snapshots)
            snapshot_cost = add(snapshot_cost, target.packages.at(id).size);
        bool valid_old =
            local.installed && local.installed->platform == platform && mode == InstallMode::Update;
        if (valid_old)
            for (const auto &[path, f] : local.installed->manifest.platforms.at(platform).files) {
                (void)path;
                if (!file_matches(root, f, stop)) {
                    valid_old = false;
                    break;
                }
            }
        if (valid_old) {
            struct Route {
                std::uint64_t cost = 0;
                std::vector<std::string> packages;
                Inventory files;
            };
            std::map<std::uint64_t, Route> routes;
            routes.emplace(local.installed->manifest.sequence,
                           Route{0, {}, local.installed->manifest.platforms.at(platform).files});
            for (auto it = routes.begin(); it != routes.end(); ++it) {
                const auto position = it->first;
                const auto route = it->second;
                for (const auto &edge : base.edges)
                    if (edge.from == position) {
                        Route next = route;
                        for (const auto &id : edge.packages) {
                            const auto &package = target.packages.at(id);
                            next.cost = add(next.cost, package.size);
                            next.packages.push_back(id);
                            for (const auto &[path, f] : package.files)
                                next.files[path] = f;
                        }
                        if (!routes.contains(edge.to) || next.cost < routes.at(edge.to).cost)
                            routes[edge.to] = std::move(next);
                    }
            }
            if (routes.contains(target.sequence)) {
                const auto &route = routes.at(target.sequence);
                bool covers = true;
                for (const auto &[path, f] : base.files)
                    if (!route.files.contains(path) || !same(route.files.at(path), f)) {
                        covers = false;
                        break;
                    }
                if (covers && route.cost < snapshot_cost) {
                    plan.patch_chain = true;
                    plan.packages = route.packages;
                }
            }
        }
    }
    const auto &locale = target.languages.at(language);
    if (plan.changed.contains(locale.file.path))
        plan.packages.push_back(locale.package_id);
    std::set<std::string> seen;
    for (const auto &id : plan.packages) {
        if (!seen.insert(id).second)
            fail(ErrorCategory::Manifest, "Package reused in update route");
        const auto &p = target.packages.at(id);
        plan.download_bytes = add(plan.download_bytes, p.size);
        plan.working_bytes = add(plan.working_bytes, add(p.size, p.unpacked_size));
    }
    for (const auto &[path, f] : plan.changed) {
        plan.working_bytes = add(plan.working_bytes, add(f.size, f.size));
        if (root.exists(path)) {
            auto existing = root.open(path, FileMode::Read);
            plan.working_bytes = add(plan.working_bytes, existing.size());
        }
    }
    for (const auto &path : plan.remove)
        if (root.exists(path)) {
            auto existing = root.open(path, FileMode::Read);
            plan.working_bytes = add(plan.working_bytes, existing.size());
        }
    plan.working_bytes = add(plan.working_bytes, 16 * 1024 * 1024);
    return plan;
}
} // namespace launcher
