#pragma once
#include "launcher/local_state.hpp"
namespace launcher {
enum class InstallMode { Update, Repair, Reinstall };
struct UpdatePlan {
    Inventory desired, changed;
    std::vector<std::string> remove, packages;
    bool patch_chain = false;
    std::uint64_t download_bytes = 0, working_bytes = 0;
};
UpdatePlan plan_update(const SafeRoot &root, const LocalInstallation &local, const Manifest &target,
                       const std::string &platform, const std::string &language,
                       InstallMode mode = InstallMode::Update, std::stop_token stop = {});
} // namespace launcher
