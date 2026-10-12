#pragma once
#include "launcher/downloader.hpp"
namespace launcher {
class IReleaseProvider {
  public:
    virtual ~IReleaseProvider() = default;
    virtual std::string fetch_latest_envelope(std::stop_token stop) = 0;
    virtual std::string resolve_package(const PackageRecord &package) const = 0;
};
class GitHubReleaseProvider final : public IReleaseProvider {
    std::string endpoint_;
    HttpDownloader &downloader_;

  public:
    GitHubReleaseProvider(std::string endpoint, HttpDownloader &downloader)
        : endpoint_(std::move(endpoint)), downloader_(downloader) {}
    std::string fetch_latest_envelope(std::stop_token stop) override {
        return downloader_.fetch_envelope(endpoint_, stop);
    }
    std::string resolve_package(const PackageRecord &package) const override {
        return package.url;
    }
};
} // namespace launcher
