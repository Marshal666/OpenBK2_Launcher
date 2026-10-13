#include "launcher/platform_services.hpp"
#include "json.hpp"
#include "launcher/error.hpp"
#include "launcher/sha256.hpp"
#include "launcher/transaction.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <windows.h>
#else
#include "../assets/launcher_icon.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace launcher {
namespace fs = std::filesystem;
std::string path_utf8(const fs::path &path) {
    auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
std::string native_platform() {
#ifdef _WIN32
    return "windows-x64";
#else
    return "linux-x64";
#endif
}
namespace {
#ifdef _WIN32
fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value)))
        fail(ErrorCategory::Filesystem, "Cannot locate user directory");
    fs::path result(value);
    CoTaskMemFree(value);
    return result;
}
std::wstring quote_windows(const std::wstring &argument) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#else
fs::path home_directory() {
    const char *value = std::getenv("HOME");
    if (!value || !fs::path(value).is_absolute())
        fail(ErrorCategory::Filesystem, "HOME is unavailable");
    return value;
}
std::string desktop_icon_path(const fs::path &path) {
    // Icon is an iconstring, not an Exec argument: escape desktop-entry values
    // without shell quotes or percent field-code expansion.
    std::string result;
    for (char c : path_utf8(path)) {
        switch (c) {
        case '\\':
            result += "\\\\";
            break;
        case ' ':
            result += "\\s";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}
#endif
} // namespace
fs::path executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count == buffer.size())
        fail(ErrorCategory::Filesystem, "Cannot locate running executable");
    buffer.resize(count);
    return fs::path(buffer);
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}
fs::path config_directory() {
#ifdef _WIN32
    return known_folder(FOLDERID_LocalAppData) / "OpenBK2Launcher";
#else
    const char *value = std::getenv("XDG_CONFIG_HOME");
    return (value && fs::path(value).is_absolute() ? fs::path(value) : home_directory() / ".config") /
           "openbk2-launcher";
#endif
}
fs::path default_game_directory() {
#ifdef _WIN32
    return known_folder(FOLDERID_Profile) / "Games/OpenBK2";
#else
    return home_directory() / "Games/OpenBK2";
#endif
}
fs::path desktop_directory() {
#ifdef _WIN32
    return known_folder(FOLDERID_Desktop);
#else
    // Parse the standard file as data, never source it as shell code.
    const auto user_dirs = config_directory().parent_path() / "user-dirs.dirs";
    if (fs::exists(user_dirs)) {
        SafeRoot parent(user_dirs.parent_path());
        const auto data = parent.read("user-dirs.dirs", 65536);
        const std::string marker = "XDG_DESKTOP_DIR=\"";
        auto start = data.find(marker);
        if (start != data.npos) {
            start += marker.size();
            auto end = data.find('"', start);
            auto value = data.substr(start, end - start);
            if (value.starts_with("$HOME/"))
                return home_directory() / value.substr(6);
            if (value.find('$') == value.npos && fs::path(value).is_absolute())
                return value;
        }
    }
    return home_directory() / "Desktop";
#endif
}
std::string desktop_exec_quote(std::string_view argument) {
    if (argument.find_first_of("\r\n") != argument.npos || argument.find('\0') != argument.npos)
        fail(ErrorCategory::Filesystem, "Desktop shortcut path contains an unsupported character");
    std::string result = "\"";
    for (char c : argument) {
        if (c == '%')
            result += "%%";
        else if (c == '\\')
            result += "\\\\\\\\";
        else if (c == '"' || c == '`' || c == '$') {
            result += "\\\\";
            result += c;
        } else
            result += c;
    }
    return result + '"';
}
void create_desktop_shortcut(const fs::path &executable, const fs::path &desktop) {
    SafeRoot root(desktop, true);
#ifdef _WIN32
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init))
        fail(ErrorCategory::Filesystem, "Cannot initialize native shortcut service");
    struct ComGuard {
        ~ComGuard() {
            CoUninitialize();
        }
    } guard;
    IShellLinkW *raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void **>(&raw))))
        fail(ErrorCategory::Filesystem, "Cannot create native shortcut");
    const auto link =
        std::unique_ptr<IShellLinkW, void (*)(IShellLinkW *)>(raw, [](auto *p) { p->Release(); });
    if (FAILED(link->SetPath(executable.c_str())) ||
        FAILED(link->SetWorkingDirectory(executable.parent_path().c_str())) ||
        FAILED(link->SetIconLocation(executable.c_str(), 0)) ||
        FAILED(link->SetDescription(L"OpenBK2 Launcher")))
        fail(ErrorCategory::Filesystem, "Cannot configure shortcut");
    IPersistFile *persist_raw = nullptr;
    if (FAILED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist_raw))))
        fail(ErrorCategory::Filesystem, "Cannot save shortcut");
    const auto persist =
        std::unique_ptr<IPersistFile, void (*)(IPersistFile *)>(persist_raw, [](auto *p) { p->Release(); });
    const auto temp = "OpenBK2-" + unique_id() + ".lnk";
    if (FAILED(persist->Save((root.path() / temp).c_str(), TRUE)))
        fail(ErrorCategory::Filesystem, "Cannot write shortcut");
    root.rename(temp, "OpenBK2 Launcher.lnk", true);
#else
    const char *data_home = std::getenv("XDG_DATA_HOME");
    const auto data = data_home && fs::path(data_home).is_absolute() ? fs::path(data_home)
                                                                     : home_directory() / ".local/share";
    SafeRoot icons(data / "openbk2-launcher", true);
    icons.write_atomic("icon.png", {reinterpret_cast<const char *>(art::icon::png), sizeof(art::icon::png)});
    // GLib 2.72 checks the executable before expanding %% field escapes. Use
    // the native env utility as argv[0], with the final launcher as a quoted
    // argument. This invokes no shell and preserves literal percent characters.
    const auto contents = "[Desktop Entry]\nType=Application\nName=OpenBK2 Launcher\nComment=Install, update "
                          "and play OpenBK2\nExec=/usr/bin/env -- " +
                          desktop_exec_quote(path_utf8(executable)) +
                          "\nIcon=" + desktop_icon_path(icons.path() / "icon.png") +
                          "\nTerminal=false\nCategories=Game;\n";
    root.write_atomic("openbk2-launcher.desktop", contents);
    auto file = root.open("openbk2-launcher.desktop", FileMode::Append);
    file.set_executable(true);
    file.flush();
#endif
}
struct ChildProcess::Impl {
#ifdef _WIN32
    HANDLE process = nullptr;
    ~Impl() {
        if (process)
            CloseHandle(process);
    }
#else
    pid_t pid = -1;
#endif
};
ChildProcess::ChildProcess() : impl_(std::make_unique<Impl>()) {}
ChildProcess::~ChildProcess() = default;
ChildProcess::ChildProcess(ChildProcess &&) noexcept = default;
ChildProcess &ChildProcess::operator=(ChildProcess &&) noexcept = default;
bool ChildProcess::running() {
#ifdef _WIN32
    if (!impl_->process)
        return false;
    if (WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT)
        return true;
    CloseHandle(impl_->process);
    impl_->process = nullptr;
    return false;
#else
    if (impl_->pid < 0)
        return false;
    int status = 0;
    const auto result = waitpid(impl_->pid, &status, WNOHANG);
    if (result == 0)
        return true;
    impl_->pid = -1;
    return false;
#endif
}
void ChildProcess::start(const fs::path &executable, const fs::path &cwd,
                         const std::vector<std::string> &args,
                         const std::map<std::string, std::string> &environment) {
    if (running())
        fail(ErrorCategory::GameRunning, "A game process is already running");
#ifdef _WIN32
    if (!environment.empty())
        fail(ErrorCategory::Manifest, "Windows launch environment must be empty");
    auto command = quote_windows(executable.native());
    for (const auto &argument : args)
        command += L" " + quote_windows(utf8_path(argument).native());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, cwd.c_str(), &startup, &process))
        fail(ErrorCategory::Filesystem,
             "Game process could not start (Windows error " + std::to_string(GetLastError()) + ")");
    CloseHandle(process.hThread);
    impl_->process = process.hProcess;
#else
    std::vector<std::string> strings{path_utf8(executable)};
    strings.insert(strings.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (auto &s : strings)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    std::vector<std::string> env;
    for (char **p = environ; *p; ++p) {
        const std::string value = *p;
        const auto key = value.substr(0, value.find('='));
        if (!key.starts_with("LD_") && !environment.contains(key))
            env.push_back(value);
    }
    for (const auto &[key, value] : environment)
        env.push_back(key + "=" + value);
    std::vector<char *> envp;
    for (auto &value : env)
        envp.push_back(value.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions))
        fail(ErrorCategory::Filesystem, "Initialize process actions");
    const int directory = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    pid_t pid = -1;
    const auto result =
        directory ? directory
                  : posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (result)
        fail(ErrorCategory::Filesystem,
             "Game process could not start (POSIX error " + std::to_string(result) + ")");
    impl_->pid = pid;
#endif
}
bool game_process_running(const fs::path &executable) {
    // Signed paths use '/', while native Windows process image paths use '\\'.
    const auto expected = fs::absolute(executable).lexically_normal().make_preferred();
#ifdef _WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
            continue;
        std::wstring name(32768, L'\0');
        DWORD size = static_cast<DWORD>(name.size());
        if (QueryFullProcessImageNameW(process, 0, name.data(), &size)) {
            name.resize(size);
            found = CompareStringOrdinal(name.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
        }
        CloseHandle(process);
        if (found)
            break;
    }
    CloseHandle(snapshot);
    return found;
#else
    std::error_code error;
    for (const auto &entry : fs::directory_iterator("/proc", error)) {
        const auto name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != name.npos)
            continue;
        auto target = fs::read_symlink(entry.path() / "exe", error);
        if (!error && target == expected)
            return true;
        error.clear();
    }
    return false;
#endif
}
void GameLauncher::launch(const SafeRoot &root, const InstalledState &state) {
    std::scoped_lock guard(mutex_);
    if (state.platform != native_platform())
        fail(ErrorCategory::UnsupportedPlatform, "Installed game is for a different platform");
    auto lock = root.open(".launcher/update.lock", FileMode::Append);
    lock.lock_exclusive();
    if (!verify_installation(root, state))
        fail(ErrorCategory::PackageIntegrity, "Installation needs repair or recovery before Play");
    const auto &profile = state.manifest.platforms.at(state.platform).launch;
    const auto executable = root.path() / utf8_path(profile.executable);
    if (process_.running() || game_process_running(executable))
        fail(ErrorCategory::GameRunning, "The game is already running");
    // Keep the executable handle open through process creation.
    auto file = root.open(profile.executable, FileMode::Read);
    std::array<char, 4> magic{};
    if (file.read_at(0, magic) < 4)
        fail(ErrorCategory::PackageIntegrity, "Game executable is truncated");
#ifdef _WIN32
    if (magic[0] != 'M' || magic[1] != 'Z')
        fail(ErrorCategory::PackageIntegrity, "Expected a native Windows game executable");
#else
    if (magic != std::array<char, 4>{0x7f, 'E', 'L', 'F'})
        fail(ErrorCategory::PackageIntegrity, "Expected a native Linux game executable");
#endif
    auto environment = profile.environment;
    for (auto &[key, value] : environment)
        if (value == "${INSTALL_DIR}/bin")
            value = path_utf8(root.path() / "bin");
    process_.start(executable, root.path() / utf8_path(profile.working_directory), profile.args, environment);
}
bool GameLauncher::running() {
    std::unique_lock guard(mutex_, std::try_to_lock);
    return !guard.owns_lock() || process_.running();
}

fs::path prepare_relocation(const SafeRoot &install, const SafeRoot &config,
                            const std::vector<std::string> &arguments) {
    const auto source = executable_path();
    const auto name = native_platform() == "windows-x64" ? "OpenBK2Launcher.exe" : "OpenBK2Launcher";
    const auto destination = install.path() / name;
    std::error_code existing_error;
    if (fs::equivalent(source, destination, existing_error) && !existing_error)
        return destination;
    SafeRoot source_root(source.parent_path());
    const auto source_name = path_utf8(source.filename());
    auto source_file = source_root.open(source_name, FileMode::Read);
    const auto expected = sha256_file(source_file);
    if (install.exists(name)) {
        auto file = install.open(name, FileMode::Read);
        if (sha256_file(file) != expected)
            fail(ErrorCategory::Filesystem,
                 "A different launcher already occupies the destination; original launcher remains usable");
    } else {
        const auto temp = ".launcher/relocate-" + unique_id();
        if (copy_verified(source_root, source_name, install, temp) != expected)
            fail(ErrorCategory::PackageIntegrity, "Relocated launcher copy failed verification");
        {
            auto file = install.open(temp, FileMode::Append);
            file.set_executable(true);
            file.flush();
        }
        install.rename(temp, name);
    }
    const auto token = unique_id();
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    config.write_atomic("handoff-" + token + ".json",
                        "{\"source\":" + json::quote(path_utf8(source)) + ",\"destination\":" +
                            json::quote(path_utf8(destination)) + ",\"parent_pid\":" + std::to_string(pid) +
                            ",\"sha256\":" + json::quote(expected) + "}");
    auto args = arguments;
    args.insert(args.end(), {"--handoff", token});
    ChildProcess child;
    child.start(destination, install.path(), args);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (config.exists("handoff-" + token + ".ack")) {
            config.remove("handoff-" + token + ".ack");
            return destination;
        }
        if (!child.running())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    fail(ErrorCategory::Filesystem,
         "Copied launcher did not acknowledge handoff; original launcher remains usable");
}
void complete_handoff(const SafeRoot &config, const std::string &token) {
    if (token.size() != 32 || token.find_first_not_of("0123456789abcdef") != token.npos)
        fail(ErrorCategory::Filesystem, "Invalid relocation handoff token");
    auto doc = json::parse(config.read("handoff-" + token + ".json", 65536));
    auto value = yyjson_doc_get_root(doc.get());
    json::fields(value, {"source", "destination", "parent_pid", "sha256"});
    // The handoff record uses '/', but Windows process APIs return native separators.
    const auto source = utf8_path(json::string(json::get(value, "source"))).make_preferred();
    const auto destination = utf8_path(json::string(json::get(value, "destination")));
    const auto pid = json::number(json::get(value, "parent_pid"));
    const auto self = executable_path();
    std::error_code error;
    if (!fs::equivalent(self, destination, error) || error || source == destination)
        fail(ErrorCategory::Filesystem, "Handoff destination mismatch");
    SafeRoot source_root(source.parent_path()), self_root(self.parent_path());
    const auto expected = sha256_file(self);
    if (expected != json::string(json::get(value, "sha256")) || sha256_file(source) != expected)
        fail(ErrorCategory::Signature, "Handoff source is not this launcher binary");
#ifdef _WIN32
    HANDLE parent =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!parent)
        fail(ErrorCategory::Filesystem, "Relocation parent is unavailable; source cleanup skipped");
    std::wstring parent_path(32768, L'\0');
    DWORD count = static_cast<DWORD>(parent_path.size());
    const bool valid = QueryFullProcessImageNameW(parent, 0, parent_path.data(), &count) != 0;
    parent_path.resize(count);
    if (!valid || CompareStringOrdinal(parent_path.c_str(), -1, source.c_str(), -1, TRUE) != CSTR_EQUAL) {
        CloseHandle(parent);
        fail(ErrorCategory::Filesystem, "Handoff parent mismatch");
    }
    config.write_atomic("handoff-" + token + ".ack", "ready");
    const bool exited = WaitForSingleObject(parent, 10000) == WAIT_OBJECT_0;
    CloseHandle(parent);
#else
    if (static_cast<std::uint64_t>(getppid()) != pid ||
        fs::read_symlink(fs::path("/proc") / std::to_string(pid) / "exe") != source)
        fail(ErrorCategory::Filesystem, "Handoff parent mismatch");
    config.write_atomic("handoff-" + token + ".ack", "ready");
    bool exited = false;
    for (unsigned i = 0; i < 100; ++i) {
        // Reparenting also detects an exited parent that has not been reaped yet.
        if (static_cast<std::uint64_t>(getppid()) != pid ||
            (kill(static_cast<pid_t>(pid), 0) < 0 && errno == ESRCH)) {
            exited = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
    if (exited && sha256_file(source) == expected)
        source_root.remove(path_utf8(source.filename()));
    config.remove("handoff-" + token + ".json");
}
} // namespace launcher
