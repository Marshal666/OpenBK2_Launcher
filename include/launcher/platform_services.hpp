#pragma once
#include "launcher/local_state.hpp"
#include <memory>
#include <mutex>
namespace launcher {
// UTF-8 text for display and persistence, with '/' directory separators.
// Literal backslashes in POSIX filenames remain intact.
std::string path_utf8(const std::filesystem::path &path);
std::filesystem::path executable_path();
std::filesystem::path config_directory();
std::filesystem::path default_game_directory();
std::filesystem::path desktop_directory();
std::string native_platform();
std::string desktop_exec_quote(std::string_view argument);
void create_desktop_shortcut(const std::filesystem::path &executable, const std::filesystem::path &desktop);
bool game_process_running(const std::filesystem::path &executable);
class ChildProcess {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess &&) noexcept;
    ChildProcess &operator=(ChildProcess &&) noexcept;
    void start(const std::filesystem::path &executable, const std::filesystem::path &cwd,
               const std::vector<std::string> &args,
               const std::map<std::string, std::string> &environment = {});
    bool running();
};
class GameLauncher {
    ChildProcess process_;
    std::mutex mutex_;

  public:
    void launch(const SafeRoot &root, const InstalledState &state);
    bool running();
};
// Relocation never overwrites an unrelated executable. The destination receives
// only a nonce; cleanup paths are validated from a per-user handoff record.
std::filesystem::path prepare_relocation(const SafeRoot &install, const SafeRoot &config,
                                         const std::vector<std::string> &arguments = {});
void complete_handoff(const SafeRoot &config, const std::string &token);
} // namespace launcher
