#pragma once
#include "launcher/package_reader.hpp"
#include "launcher/transaction.hpp"
namespace launcher {
using PackageFetcher =
    std::function<std::filesystem::path(const PackageRecord &, std::stop_token, ProgressSink)>;
struct InstallOptions {
    InstallMode mode = InstallMode::Update;
    std::optional<std::uint64_t> available_bytes;
    std::uint64_t preserved_backup_limit = 8ull * 1024 * 1024 * 1024;
    FaultHook checkpoint;
};
struct InstallResult {
    bool used_patch_chain = false, used_snapshot_fallback = false;
    std::vector<std::string> preserved, warnings;
};
class Installer {
    SafeRoot root_;
    TrustedKeys keys_;
    ManifestPolicy policy_;

  public:
    Installer(std::filesystem::path root, TrustedKeys keys, ManifestPolicy policy = {})
        : root_(std::move(root), true), keys_(std::move(keys)), policy_(policy) {}
    TransactionResult recover(FaultHook checkpoint = {}, ProgressSink progress = {});
    InstallResult install(std::string_view envelope, const std::string &platform, const std::string &language,
                          PackageFetcher fetch, InstallOptions options = {}, std::stop_token stop = {},
                          ProgressSink progress = {});
    const SafeRoot &root() const {
        return root_;
    }
};
} // namespace launcher
