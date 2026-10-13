#pragma once
#include "launcher/installer.hpp"

namespace launcher {
struct LocalMod {
    std::string id;
    bool managed_by_game = false;
};
// Optional mods never join the base game's managed inventory. Removal retains
// the complete folder, including edits and unknown files, in a unique backup.
class ModManager {
    SafeRoot root_;
    TrustedKeys keys_;
    ManifestPolicy policy_;

  public:
    ModManager(std::filesystem::path root, TrustedKeys keys, ManifestPolicy policy = {})
        : root_(std::move(root)), keys_(std::move(keys)), policy_(policy) {}
    std::vector<LocalMod> list() const;
    void download(std::string_view envelope, const std::string &id, PackageFetcher fetch,
                  std::stop_token stop = {}, ProgressSink progress = {}, FaultHook checkpoint = {});
    std::string remove(const std::string &id, FaultHook checkpoint = {});
};
} // namespace launcher
