#include "launcher/safe_files.hpp"
#include "launcher/error.hpp"
#include "launcher/manifest.hpp"
#include "launcher/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace launcher {
namespace fs = std::filesystem;
namespace {
#ifdef _WIN32
using Handle = HANDLE;
const Handle invalid = INVALID_HANDLE_VALUE;
void close_handle(Handle h) {
    CloseHandle(h);
}
#else
using Handle = int;
constexpr Handle invalid = -1;
void close_handle(Handle h) {
    ::close(h);
}
#endif
struct Owner {
    Handle handle = invalid;
    explicit Owner(Handle h) : handle(h) {}
    ~Owner() {
        if (handle != invalid)
            close_handle(handle);
    }
    Owner(const Owner &) = delete;
};
using Owners = std::vector<std::shared_ptr<Owner>>;
[[noreturn]] void io_error(const char *action) {
#ifdef _WIN32
    const auto code = GetLastError();
    if (code == ERROR_FILENAME_EXCED_RANGE)
        fail(ErrorCategory::Filesystem,
             "Path exceeds the Windows path limit; choose a shorter installation folder");
    const auto category =
        (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL)
            ? ErrorCategory::DiskSpace
            : (code == ERROR_SHARING_VIOLATION ? ErrorCategory::GameRunning : ErrorCategory::Filesystem);
#else
    const auto code = errno;
    const auto category = code == ENOSPC ? ErrorCategory::DiskSpace : ErrorCategory::Filesystem;
#endif
    fail(category, std::string(action) + " (OS error " + std::to_string(code) + ")");
}
Owners pin_directories(const fs::path &absolute, bool create) {
    Owners chain;
#ifdef _WIN32
    if (absolute.native().size() >= 248)
        fail(ErrorCategory::Filesystem,
             "Directory exceeds the supported Windows path limit; choose a shorter installation folder");
    fs::path current = absolute.root_path();
    auto pin = [&](const fs::path &directory) {
        Handle h =
            CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == invalid)
            io_error("Open installation directory");
        auto holder = std::make_shared<Owner>(h);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info))
            io_error("Inspect directory");
        if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail(ErrorCategory::Filesystem, "Linked/reparse directory is not allowed");
        chain.push_back(std::move(holder));
    };
    pin(current);
    for (const auto &component : absolute.relative_path()) {
        current /= component;
        if (create && !CreateDirectoryW(current.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            io_error("Create directory");
        pin(current);
    }
#else
    Handle first = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (first < 0)
        io_error("Open filesystem root");
    chain.push_back(std::make_shared<Owner>(first));
    for (const auto &component : absolute.relative_path()) {
        const auto name = component.native();
        if (create) {
            if (mkdirat(chain.back()->handle, name.c_str(), 0700) == 0) {
                if (fsync(chain.back()->handle))
                    io_error("Flush created directory");
            } else if (errno != EEXIST)
                io_error("Create directory");
        }
        auto h = openat(chain.back()->handle, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (h < 0)
            io_error("Open safe directory");
        chain.push_back(std::make_shared<Owner>(h));
    }
#endif
    return chain;
}
} // namespace
struct SafeRoot::Impl {
    fs::path root;
    Owners parents;
};
struct SafeFile::Impl {
    std::shared_ptr<Owner> file;
    Owners parents;
};
SafeFile::SafeFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SafeFile::SafeFile(SafeFile &&) noexcept = default;
SafeFile &SafeFile::operator=(SafeFile &&) noexcept = default;
SafeFile::~SafeFile() = default;
std::uint64_t SafeFile::size() const {
#ifdef _WIN32
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(impl_->file->handle, &size))
        io_error("Read file size");
    return static_cast<std::uint64_t>(size.QuadPart);
#else
    struct stat status{};
    if (fstat(impl_->file->handle, &status))
        io_error("Read file size");
    return static_cast<std::uint64_t>(status.st_size);
#endif
}
std::size_t SafeFile::read_at(std::uint64_t offset, std::span<char> bytes) {
#ifdef _WIN32
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(impl_->file->handle, position, nullptr, FILE_BEGIN))
        io_error("Seek file");
    DWORD count = 0;
    if (!ReadFile(impl_->file->handle, bytes.data(),
                  static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 0x7fffffff)), &count, nullptr))
        io_error("Read file");
    return count;
#else
    auto count = pread(impl_->file->handle, bytes.data(), bytes.size(), static_cast<off_t>(offset));
    if (count < 0)
        io_error("Read file");
    return static_cast<std::size_t>(count);
#endif
}
void SafeFile::write(std::span<const char> bytes) {
    while (!bytes.empty()) {
#ifdef _WIN32
        DWORD count = 0;
        if (!WriteFile(impl_->file->handle, bytes.data(),
                       static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 0x7fffffff)), &count,
                       nullptr) ||
            count == 0)
            io_error("Write file");
#else
        auto count = ::write(impl_->file->handle, bytes.data(), bytes.size());
        if (count <= 0)
            io_error("Write file");
#endif
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}
void SafeFile::flush() {
#ifdef _WIN32
    if (!FlushFileBuffers(impl_->file->handle))
        io_error("Flush file");
#else
    if (fsync(impl_->file->handle))
        io_error("Flush file");
#endif
}
void SafeFile::set_executable(bool executable) {
#ifndef _WIN32
    if (fchmod(impl_->file->handle, executable ? 0755 : 0644))
        io_error("Set signed executable mode");
#else
    (void)executable;
#endif
}
bool SafeFile::executable() const {
#ifdef _WIN32
    return false;
#else
    struct stat status{};
    if (fstat(impl_->file->handle, &status))
        io_error("Inspect executable permissions");
    return (status.st_mode & S_IXUSR) != 0;
#endif
}
void SafeFile::lock_exclusive() {
#ifndef _WIN32
    if (flock(impl_->file->handle, LOCK_EX | LOCK_NB))
        fail(ErrorCategory::Permission, "Another updater holds this installation lock");
#endif
    // Windows read/write opens already deny write sharing for the handle lifetime.
}
SafeRoot::SafeRoot(fs::path root, bool create) : impl_(std::make_shared<Impl>()) {
    impl_->root = fs::absolute(root).lexically_normal();
    if (impl_->root.filename().empty() && impl_->root != impl_->root.root_path())
        impl_->root = impl_->root.parent_path();
    impl_->parents = pin_directories(impl_->root, create);
}
const fs::path &SafeRoot::path() const {
    return impl_->root;
}
SafeFile SafeRoot::open(std::string_view relative, FileMode mode) const {
    validate_relative_path(relative);
    const auto target = impl_->root / utf8_path(relative);
    auto value = std::make_unique<SafeFile::Impl>();
    value->parents = pin_directories(target.parent_path(), mode != FileMode::Read);
#ifdef _WIN32
    const DWORD access = GENERIC_READ | (mode == FileMode::Read ? 0 : GENERIC_WRITE);
    const DWORD creation =
        mode == FileMode::CreateNew ? CREATE_NEW : (mode == FileMode::Read ? OPEN_EXISTING : OPEN_ALWAYS);
    auto h = CreateFileW(target.c_str(), access, FILE_SHARE_READ, nullptr, creation,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == invalid)
        io_error("Open safe regular file");
    value->file = std::make_shared<Owner>(h);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info))
        io_error("Inspect regular file");
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nNumberOfLinks != 1)
        fail(ErrorCategory::Filesystem, "Linked or nonregular file is not allowed");
    LARGE_INTEGER offset{};
    if (mode == FileMode::Append && !SetFilePointerEx(h, offset, nullptr, FILE_END))
        io_error("Seek append");
    if (mode == FileMode::Rewrite && (!SetFilePointerEx(h, offset, nullptr, FILE_BEGIN) || !SetEndOfFile(h)))
        io_error("Truncate file");
#else
    const auto name = target.filename().native();
    int flags = O_NOFOLLOW | O_CLOEXEC | (mode == FileMode::Read ? O_RDONLY : O_RDWR | O_CREAT);
    if (mode == FileMode::CreateNew)
        flags |= O_EXCL;
    if (mode == FileMode::Append)
        flags |= O_APPEND;
    auto h = openat(value->parents.back()->handle, name.c_str(), flags, 0600);
    if (h < 0)
        io_error("Open safe regular file");
    value->file = std::make_shared<Owner>(h);
    struct stat status{};
    if (fstat(h, &status))
        io_error("Inspect regular file");
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        fail(ErrorCategory::Filesystem, "Linked or nonregular file is not allowed");
    if (mode == FileMode::Rewrite && ftruncate(h, 0))
        io_error("Truncate file");
#endif
    return SafeFile(std::move(value));
}
bool SafeRoot::exists(std::string_view relative) const {
    validate_relative_path(relative);
    const auto target = impl_->root / utf8_path(relative);
    std::error_code error;
    const auto status = fs::symlink_status(target, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return false;
    if (error)
        fail(ErrorCategory::Filesystem, "Cannot inspect path");
    // An existing candidate must pass the handle-based link/ancestor checks.
    auto file = open(relative, FileMode::Read);
    (void)file;
    return true;
}
void SafeRoot::remove(std::string_view relative) const {
    validate_relative_path(relative);
    if (!exists(relative))
        return;
    const auto target = impl_->root / utf8_path(relative);
    auto parents = pin_directories(target.parent_path(), false);
#ifdef _WIN32
    if (!DeleteFileW(target.c_str()))
        io_error("Remove file");
#else
    if (unlinkat(parents.back()->handle, target.filename().c_str(), 0))
        io_error("Remove file");
    if (fsync(parents.back()->handle))
        io_error("Flush directory");
#endif
}
void SafeRoot::rename(std::string_view from, std::string_view to, bool replace) const {
    validate_relative_path(from);
    validate_relative_path(to);
    const auto source = impl_->root / utf8_path(from), destination = impl_->root / utf8_path(to);
    auto source_parents = pin_directories(source.parent_path(), false);
    auto destination_parents = pin_directories(destination.parent_path(), true);
    if (!exists(from))
        fail(ErrorCategory::Filesystem, "Rename source is missing");
    if (exists(to) && !replace)
        fail(ErrorCategory::Filesystem, "Rename destination already exists");
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
        io_error("Rename file");
#else
    const auto result = replace ? renameat(source_parents.back()->handle, source.filename().c_str(),
                                           destination_parents.back()->handle, destination.filename().c_str())
                                : syscall(SYS_renameat2, source_parents.back()->handle,
                                          source.filename().c_str(), destination_parents.back()->handle,
                                          destination.filename().c_str(), 1 /* RENAME_NOREPLACE */);
    if (result)
        io_error("Rename file");
    if (fsync(source_parents.back()->handle) || fsync(destination_parents.back()->handle))
        io_error("Flush renamed directories");
#endif
}
std::string unique_id() {
    static std::atomic<unsigned long long> counter = 0;
    std::random_device random;
    return sha256(std::to_string(random()) + ":" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ":" +
                  std::to_string(++counter))
        .substr(0, 32);
}
void SafeRoot::write_atomic(std::string_view relative, std::string_view bytes,
                            const std::function<void(std::string_view)> &checkpoint) const {
    const auto temp = std::string(relative) + ".tmp-" + unique_id();
    {
        auto file = open(temp, FileMode::CreateNew);
        file.write({bytes.data(), bytes.size()});
        if (checkpoint)
            checkpoint("atomic file written");
        file.flush();
        if (checkpoint)
            checkpoint("atomic file flushed");
    }
    rename(temp, relative, true);
    if (checkpoint)
        checkpoint("atomic file renamed");
}
std::string SafeRoot::read(std::string_view relative, std::size_t maximum) const {
    auto file = open(relative, FileMode::Read);
    if (file.size() > maximum)
        fail(ErrorCategory::Filesystem, "Metadata size limit exceeded");
    std::string result(static_cast<std::size_t>(file.size()), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto count = file.read_at(offset, {result.data() + offset, result.size() - offset});
        if (!count)
            fail(ErrorCategory::Filesystem, "Metadata truncated while reading");
        offset += count;
    }
    return result;
}
} // namespace launcher
