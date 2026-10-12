#pragma once
#include "launcher/downloader.hpp"
namespace launcher {
class ZipPackageReader {
  public:
    void extract(const std::filesystem::path &archive, const PackageRecord &package, const SafeRoot &staging,
                 std::stop_token stop = {}, ProgressSink progress = {}) const;
};
} // namespace launcher
