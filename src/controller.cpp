#include "launcher/controller.hpp"
#include "build_config.hpp"
#include "launcher/error.hpp"
#include "launcher/release_provider.hpp"

namespace launcher {
Controller::Controller(ApplicationOptions options)
    : options_(std::move(options)),
      config_(options_.config_override.empty() ? config_directory() : options_.config_override, true) {
    try {
        state_.settings = load_settings(config_);
    } catch (const std::exception &error) {
        state_.settings.root = default_game_directory();
        state_.error = error.what();
    }
    if (!options_.root_override.empty()) {
        if (state_.settings.root != options_.root_override)
            state_.settings.configured = false;
        state_.settings.root = std::filesystem::absolute(options_.root_override);
    }
#if OBK_FIXTURE_BUILD
    if (!options_.fixture_public_key.empty())
        keys_.emplace("fixture-only", public_key_from_hex(options_.fixture_public_key));
    policy_.allow_loopback_http = true;
    if (options_.auto_install) {
        state_.settings.relocate = false;
        state_.settings.shortcut = false;
    }
#else
    keys_.emplace(std::string(build::key_id), public_key_from_hex(build::public_key));
#endif
}
Controller::~Controller() {
    worker_.request_stop();
    checker_.request_stop();
    if (worker_.joinable())
        worker_.join();
    if (checker_.joinable())
        checker_.join();
}
UiSnapshot Controller::snapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
}
void Controller::problem(std::string message) {
    append_log(config_, message);
    std::scoped_lock lock(mutex_);
    state_.error = std::move(message);
}
void Controller::work(std::function<void(std::stop_token)> action) {
    {
        std::scoped_lock lock(mutex_);
        if (state_.busy)
            return;
        state_.busy = true;
        state_.error.clear();
    }
    worker_ = std::jthread([this, action = std::move(action)](std::stop_token stop) {
        try {
            action(stop);
        } catch (const std::exception &error) {
            problem(error.what());
            try {
                refresh_local({});
            } catch (...) {
                std::scoped_lock lock(mutex_);
                state_.ready = false;
            }
        }
        std::scoped_lock lock(mutex_);
        state_.busy = false;
    });
}
void Controller::refresh_local(std::stop_token stop) {
    const auto settings = snapshot().settings;
    if (!std::filesystem::exists(settings.root)) {
        std::scoped_lock lock(mutex_);
        state_.installed.reset();
        state_.ready = false;
        state_.root_missing = settings.configured;
        state_.status = settings.configured ? "Installation folder is missing. Reselect it in Settings."
                                            : "Choose a folder and install.";
        state_.initialized = true;
        return;
    }
    Installer installer(settings.root, keys_, policy_);
    auto recovery = installer.recover({}, [this](const Progress &p) {
        std::scoped_lock lock(mutex_);
        state_.progress = p;
        state_.ready = false;
    });
    for (const auto &warning : recovery.warnings)
        append_log(config_, warning);
    auto local = load_local(installer.root(), keys_, policy_);
    const bool ready = local.installed && verify_installation(installer.root(), *local.installed, stop);
    std::scoped_lock lock(mutex_);
    state_.installed =
        local.installed ? std::make_shared<InstalledState>(std::move(*local.installed)) : nullptr;
    state_.ready = ready;
    state_.root_missing = false;
    state_.initialized = true;
    state_.status = ready ? "Ready to play offline"
                          : (state_.installed ? "Managed files need repair"
                                              : "Existing files will be preserved during adoption.");
    state_.progress = {ready ? "Ready" : "Checking", 0, 0, 0};
}
void Controller::initialize() {
    work([this](std::stop_token stop) { refresh_local(stop); });
}
void Controller::check_updates() {
    {
        std::scoped_lock lock(mutex_);
        if (state_.checking)
            return;
        state_.checking = true;
    }
    const auto selected = snapshot().settings.root;
    checker_ = std::jthread([this, selected](std::stop_token stop) {
        try {
            std::string envelope;
#if OBK_FIXTURE_BUILD
            if (stop.stop_requested())
                fail(ErrorCategory::Cancelled, "Update check cancelled");
            if (options_.fixture_release.empty() || keys_.empty())
                fail(ErrorCategory::Manifest,
                     "Development build: supply a signed fixture release and public key.");
            SafeRoot source(utf8_path(options_.fixture_release));
            envelope = source.read("manifest.envelope.json", 16 * 1024 * 1024);
#else
            HttpDownloader downloader;
            GitHubReleaseProvider provider(std::string(build::manifest_url), downloader);
            envelope = provider.fetch_latest_envelope(stop);
#endif
            auto latest = std::make_shared<Manifest>(verify_manifest(envelope, keys_, policy_));
            auto current = snapshot();
            if (current.installed)
                enforce_sequence(*latest, current.installed->manifest.sequence,
                                 current.installed->manifest.payload_hash);
            std::uint64_t bytes = 0, working = 0;
            const auto platform = native_platform();
            if (!latest->platforms.contains(platform))
                fail(ErrorCategory::UnsupportedPlatform, "Latest release does not support this platform");
            const auto language = latest->languages.contains(current.settings.requested_language)
                                      ? current.settings.requested_language
                                      : latest->languages.begin()->first;
            for (const auto &id : latest->platforms.at(platform).snapshots) {
                const auto &p = latest->packages.at(id);
                bytes += p.size;
                working += p.size + 3 * p.unpacked_size;
            }
            const auto &p = latest->packages.at(latest->languages.at(language).package_id);
            bytes += p.size;
            working += p.size + 3 * p.unpacked_size;
            std::scoped_lock lock(mutex_);
            if (state_.settings.root == selected) {
                state_.latest = std::move(latest);
                state_.download_bytes = bytes;
                state_.working_bytes = working + 16 * 1024 * 1024;
                if (!state_.latest->languages.contains(state_.settings.requested_language))
                    state_.settings.requested_language = language;
            }
        } catch (const std::exception &error) {
            problem(std::string("Update check: ") + error.what());
        }
        std::scoped_lock lock(mutex_);
        state_.checking = false;
    });
}
void Controller::configure(Settings settings) {
    work([this, settings = std::move(settings)](std::stop_token stop) mutable {
        if (!settings.root.is_absolute())
            fail(ErrorCategory::Filesystem, "Choose an absolute installation folder");
        const auto before = snapshot().settings;
        if (before.root != settings.root)
            settings.configured = false;
        save_settings(config_, settings);
        {
            std::scoped_lock lock(mutex_);
            state_.settings = settings;
        }
        refresh_local(stop);
    });
}
void Controller::install(InstallMode mode) {
    work([this, mode](std::stop_token stop) {
        const auto current = snapshot();
        if (!current.latest)
            fail(ErrorCategory::Manifest, "A verified release is required before installation");
        if (current.root_missing)
            fail(ErrorCategory::Filesystem, "Reselect the missing installation folder first");
        const auto &profile = current.latest->platforms.at(native_platform()).launch;
        if (game_.running() || game_process_running(current.settings.root / utf8_path(profile.executable)))
            fail(ErrorCategory::GameRunning, "Close the game before changing game files");
        Installer installer(current.settings.root, keys_, policy_);
        SafeRoot cache(current.settings.root / ".launcher/downloads", true);
        DownloadOptions download_options;
        download_options.policy = policy_;
        HttpDownloader downloader(download_options);
        auto progress = [this](const Progress &p) {
            std::scoped_lock lock(mutex_);
            state_.progress = p;
            state_.ready = false;
        };
        auto fetch = [&](const PackageRecord &package, std::stop_token token, ProgressSink report) {
#if OBK_FIXTURE_BUILD
            if (options_.fixture_package_base.empty())
                return utf8_path(options_.fixture_release) / utf8_path(package.id + ".zip");
            auto resolved = package;
            resolved.url = options_.fixture_package_base + "/" + package.id + ".zip";
            return downloader.download(resolved, cache, token, report);
#else
            return downloader.download(package, cache, token, report);
#endif
        };
        InstallOptions install_options;
        install_options.mode = mode;
        auto result =
            installer.install(current.latest->envelope, native_platform(),
                              current.settings.requested_language, fetch, install_options, stop, progress);
        auto settings = current.settings;
        settings.configured = true;
        save_settings(config_, settings);
        {
            std::scoped_lock lock(mutex_);
            state_.settings = settings;
        }
        refresh_local({});
        for (const auto &path : result.preserved)
            append_log(config_, "Preserved file: " + path);
        for (const auto &warning : result.warnings)
            append_log(config_, warning);
        if (!result.preserved.empty()) {
            std::scoped_lock lock(mutex_);
            state_.status = "Ready. User files preserved in .launcher/backups (" +
                            std::to_string(result.preserved.size()) + ").";
        }
        auto launcher = executable_path();
        if (settings.relocate) {
            std::vector<std::string> args;
#if OBK_FIXTURE_BUILD
            args = {"--config-dir",           path_utf8(config_.path()), "--fixture-release",
                    options_.fixture_release, "--fixture-public-key",    options_.fixture_public_key};
#endif
            launcher = prepare_relocation(installer.root(), config_, args);
            if (settings.shortcut)
                create_desktop_shortcut(launcher, desktop_directory());
            std::scoped_lock lock(mutex_);
            state_.exit_requested = launcher != executable_path();
        } else if (settings.shortcut)
            create_desktop_shortcut(launcher, desktop_directory());
    });
}
void Controller::play() {
    work([this](std::stop_token) {
        auto current = snapshot();
        if (!current.ready || !current.installed)
            fail(ErrorCategory::PackageIntegrity, "Install or repair the game before Play");
        SafeRoot root(current.settings.root);
        game_.launch(root, *current.installed);
        std::scoped_lock lock(mutex_);
        state_.status = "Game is running";
    });
}
void Controller::shortcut() {
    work([this](std::stop_token) {
        create_desktop_shortcut(executable_path(), desktop_directory());
        std::scoped_lock lock(mutex_);
        state_.status = "Desktop shortcut created";
    });
}
void Controller::cancel() {
    worker_.request_stop();
    checker_.request_stop();
}
} // namespace launcher
