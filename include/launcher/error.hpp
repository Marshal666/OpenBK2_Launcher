#pragma once
#include <stdexcept>
#include <string>
namespace launcher {
enum class ErrorCategory {
    Network,
    Signature,
    Manifest,
    PackageIntegrity,
    Filesystem,
    DiskSpace,
    Permission,
    GameRunning,
    UnsupportedPlatform,
    Recovery,
    Cancelled
};
class Error : public std::runtime_error {
  public:
    ErrorCategory category;
    Error(ErrorCategory c, const std::string &message) : std::runtime_error(message), category(c) {}
};
[[noreturn]] inline void fail(ErrorCategory category, const std::string &message) {
    throw Error(category, message);
}
} // namespace launcher
