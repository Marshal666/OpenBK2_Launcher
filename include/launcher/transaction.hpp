#pragma once
#include "launcher/downloader.hpp"
#include "launcher/planner.hpp"
namespace launcher {
using FaultHook = std::function<void(std::string_view)>;
struct TransactionResult {
    bool rolled_back = false;
    std::vector<std::string> preserved, warnings;
};
class Transaction {
    const SafeRoot &root_;
    const TrustedKeys &keys_;
    ManifestPolicy policy_;
    FaultHook checkpoint_;
    ProgressSink progress_;

  public:
    Transaction(const SafeRoot &root, const TrustedKeys &keys, ManifestPolicy policy = {},
                FaultHook checkpoint = {}, ProgressSink progress = {})
        : root_(root), keys_(keys), policy_(policy), checkpoint_(std::move(checkpoint)),
          progress_(std::move(progress)) {
        (void)checked_installation_directory(root.path());
    }
    void prepare(const std::string &id, const LocalInstallation &old, const Manifest &target,
                 const std::string &platform, const std::string &language, const UpdatePlan &plan);
    TransactionResult recover();
};
// Copies through pinned regular-file handles, flushes and returns copied SHA-256.
std::string copy_verified(const SafeRoot &source, std::string_view from, const SafeRoot &destination,
                          std::string_view to, FaultHook checkpoint = {}, ProgressSink progress = {},
                          std::stop_token stop = {});
} // namespace launcher
