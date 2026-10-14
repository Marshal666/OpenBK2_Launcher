#pragma once
#include "launcher/installer.hpp"
#include "launcher/mod_manager.hpp"
#include "launcher/platform_services.hpp"
#include "launcher/settings.hpp"
#include <thread>
namespace launcher {
struct ApplicationOptions {
    bool smoke_test = false, auto_install = false;
    std::string capture, progress_capture, handoff, fixture_release, fixture_public_key, fixture_package_base;
    std::filesystem::path config_override, root_override;
};
struct UiSnapshot {
    Settings settings;
    std::shared_ptr<const Manifest> latest;
    std::shared_ptr<const InstalledState> installed;
    std::vector<LocalMod> mods;
    Progress progress;
    std::string status = "Starting", error;
    bool busy = false, checking = false, initialized = false, ready = false, root_missing = false,
         exit_requested = false;
    std::uint64_t download_bytes = 0, working_bytes = 0;
};
class Controller {
    ApplicationOptions options_;
    SafeRoot config_;
    TrustedKeys keys_;
    ManifestPolicy policy_;
    mutable std::mutex mutex_;
    UiSnapshot state_;
    std::jthread worker_, checker_;
    GameLauncher game_;
    void work(std::function<void(std::stop_token)> action);
    void refresh_local(std::stop_token stop);
    void problem(std::string message);
    std::filesystem::path fetch_package(const PackageRecord &, const std::filesystem::path &root,
                                        std::stop_token, ProgressSink);

  public:
    explicit Controller(ApplicationOptions options);
    ~Controller();
    UiSnapshot snapshot() const;
    void initialize();
    void check_updates(bool automatic = false);
    void configure(Settings settings);
    void install(InstallMode mode = InstallMode::Update);
    void reset_initial_data();
    void download_mod(std::string id);
    void remove_mod(std::string id);
    void play();
    void shortcut();
    void cancel();
    bool game_running() {
        return game_.running();
    }
};
} // namespace launcher
