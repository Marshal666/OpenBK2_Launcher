#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace launcher {
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
enum class FileMode { Read, CreateNew, Append, Rewrite };
class SafeFile {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class SafeRoot;
    explicit SafeFile(std::unique_ptr<Impl> impl);

  public:
    SafeFile(SafeFile &&) noexcept;
    SafeFile &operator=(SafeFile &&) noexcept;
    ~SafeFile();
    std::uint64_t size() const;
    std::size_t read_at(std::uint64_t offset, std::span<char> output);
    void write(std::span<const char> bytes);
    void flush();
    void set_executable(bool executable);
    bool executable() const;
    void lock_exclusive();
};
// Pins every directory component, rejects links/reparse points, and opens leaf
// files without following links. Unicode is supported in the selected root.
class SafeRoot {
    struct Impl;
    std::shared_ptr<Impl> impl_;

  public:
    explicit SafeRoot(std::filesystem::path root, bool create = false);
    const std::filesystem::path &path() const;
    SafeFile open(std::string_view relative, FileMode mode) const;
    bool exists(std::string_view relative) const;
    void remove(std::string_view relative) const;
    void rename(std::string_view source, std::string_view destination, bool replace = false) const;
    void write_atomic(std::string_view relative, std::string_view bytes,
                      const std::function<void(std::string_view)> &checkpoint = {}) const;
    std::string read(std::string_view relative, std::size_t maximum) const;
};
std::string unique_id();
} // namespace launcher
