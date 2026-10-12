#pragma once
#include "launcher/manifest.hpp"
#include "launcher/safe_files.hpp"
#include <functional>
#include <stop_token>

namespace launcher {
struct Progress {
    std::string stage;
    std::uint64_t completed = 0, total = 0;
    double bytes_per_second = 0;
};
using ProgressSink = std::function<void(const Progress &)>;
struct DownloadOptions {
    ManifestPolicy policy;
    long connect_timeout = 15, transfer_timeout = 3600, low_speed_time = 30;
    unsigned retries = 2;
    std::string test_ca_bundle;
};
class HttpDownloader {
    DownloadOptions options_;

  public:
    explicit HttpDownloader(DownloadOptions options = {});
    std::string fetch_envelope(const std::string &url, std::stop_token stop = {},
                               ProgressSink progress = {}) const;
    std::filesystem::path download(const PackageRecord &package, const SafeRoot &cache,
                                   std::stop_token stop = {}, ProgressSink progress = {}) const;
};
} // namespace launcher
