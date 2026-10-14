#include "json.hpp"
#include "launcher/platform_services.hpp"
#include "publisher_config.hpp"
#include "publisher_settings.hpp"
#include "font.hpp"
#include "../assets/launcher_icon.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
#include <regex>

namespace {
namespace fs = std::filesystem;
using launcher::path_utf8;
using launcher::utf8_path;
namespace json = launcher::json;

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}
struct Options {
    bool smoke = false, update_tab = false;
    std::string capture, config, check_job;
};
struct Dialog {
    enum class Action { Field, SaveSettings, LoadSettings } action = Action::Field;
    std::mutex mutex;
    bool pending = false, ready = false;
    std::string field, path, error;
};
void SDLCALL selected(void *userdata, const char *const *files, int) {
    std::unique_ptr<std::shared_ptr<Dialog>> owner(static_cast<std::shared_ptr<Dialog> *>(userdata));
    auto &dialog = **owner;
    std::scoped_lock lock(dialog.mutex);
    if (!files)
        dialog.error = SDL_GetError();
    else if (files[0])
        dialog.path = files[0];
    dialog.pending = false;
    dialog.ready = true;
}
std::string read_file(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read " + path_utf8(path));
    std::string data((std::istreambuf_iterator<char>(file)), {});
    if (data.size() > 16 * 1024 * 1024)
        throw std::runtime_error("JSON file is too large");
    return data;
}
int normalize_path(ImGuiInputTextCallbackData *data) {
#ifdef _WIN32
    for (int i = 0; i < data->BufTextLen; ++i)
        if (data->Buf[i] == '\\') {
            data->Buf[i] = '/';
            data->BufDirty = true;
        }
#else
    (void)data;
#endif
    return 0;
}

struct Publisher : publisher::Settings {
    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>();
    std::unique_ptr<launcher::SafeRoot> config;
    SDL_Process *process = nullptr;
    std::string token, log, status = "Ready", result_file, request_file;
    std::string settings_path;
    bool completed = false, success = false, scroll_log = false, select_tab = true;
    float scale = 1;

    explicit Publisher(const Options &options) {
        const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
        const auto base = fs::path(home ? utf8_path(home) : fs::current_path()) / "OpenBK2-Publishing";
        fields = {{"source_dir", publisher_build::source}, {"python", publisher_build::python},
                  {"cmake", publisher_build::cmake}, {"generator", publisher_build::generator},
                  {"owner", ""}, {"repo", "OpenBK2-Game-Releases"}, {"manifest_repo", ""},
                  {"manifest_branch", "main"}, {"key_id", "release-key-2026-01"}, {"public_key", ""},
                  {"key_file", path_utf8(base.parent_path() / "OpenBK2-Secrets/release.pem")},
                  {"shared_root", path_utf8(base / "input/shared")},
                  {"shared_init_data", ""},
                  {"windows_root", path_utf8(base / "input/windows")},
                  {"linux_root", path_utf8(base / "input/linux")},
                  {"languages_dir", path_utf8(base / "input/languages")}, {"mods_dir", ""},
                  {"output_root", path_utf8(base / "releases")}, {"previous_release", ""},
                  {"version", "0.1.0"}, {"sequence", "1"}, {"managed_files", ""}, {"excludes", ""}};
        // Prefer a checkout next to the executable, then the configured developer checkout.
        for (auto candidate : {fs::current_path(), utf8_path(SDL_GetBasePath()).parent_path(),
                               utf8_path(SDL_GetBasePath()).parent_path().parent_path()}) {
            if (fs::is_regular_file(candidate / "tools/publisher.py")) {
                fields["source_dir"] = path_utf8(candidate);
                break;
            }
        }
        if (!fs::is_regular_file(utf8_path(fields["python"])))
#ifdef _WIN32
            fields["python"] = "python";
#else
            fields["python"] = "python3";
#endif
        if (!fs::is_regular_file(utf8_path(fields["cmake"])))
            fields["cmake"] = "cmake";
        first_time = !options.update_tab;
        if (!options.smoke) {
            std::string directory = options.config;
            if (directory.empty()) {
                char *pref = SDL_GetPrefPath("OpenBK2", "Publisher");
                check(pref != nullptr, "Find publisher settings folder");
                directory = pref;
                SDL_free(pref);
            }
            config = std::make_unique<launcher::SafeRoot>(fs::absolute(utf8_path(directory)), true);
            if (config->exists("publisher.json")) {
                try {
                    load(config->read("publisher.json", 1024 * 1024));
                } catch (const std::exception &error) {
                    status = "Saved settings could not be loaded";
                    append(error.what());
                }
            }
        }
    }
    ~Publisher() {
        if (process)
            SDL_DestroyProcess(process);
    }
    void append(std::string text) {
        log += text;
        if (log.empty() || log.back() != '\n')
            log += '\n';
        if (!token.empty())
            for (size_t pos = 0; (pos = log.find(token, pos)) != std::string::npos; pos += 10)
                log.replace(pos, token.size(), "[redacted]");
        if (log.size() > 1024 * 1024)
            log.erase(0, log.size() - 1024 * 1024);
        scroll_log = true;
    }
    void save() {
        if (config)
            config->write_atomic("publisher.json", serialize());
    }
    void previous_selected() {
        try {
            auto doc = json::parse(read_file(utf8_path(fields["previous_release"]) / "manifest.payload.json"));
            auto release = json::get(yyjson_doc_get_root(doc.get()), "release");
            fields["sequence"] = std::to_string(json::number(json::get(release, "sequence")) + 1);
            auto version = json::string(json::get(release, "version"));
            std::smatch match;
            if (std::regex_match(version, match, std::regex(R"((\d{1,6})\.(\d{1,6})\.(\d{1,6})(-.*)?)")))
                fields["version"] = match[1].str() + "." + match[2].str() + "." +
                                    std::to_string(std::stoul(match[3].str()) + 1);
            resume = false;
            status = "Next version and sequence suggested from previous release";
        } catch (const std::exception &error) {
            status = "Select a retained publisher output folder";
            append(error.what());
        }
    }
    void start() {
        completed = false;
        success = false;
        log.clear();
        try {
            auto script = utf8_path(fields["source_dir"]) / "tools/publisher.py";
            if (!script.is_absolute() || !fs::is_regular_file(script))
                throw std::runtime_error("Choose the launcher source folder containing tools/publisher.py in More options.");
            save();
            const auto job = "jobs/" + launcher::unique_id();
            request_file = job + "/request.json";
            result_file = job + "/result.json";
            config->write_atomic(request_file, serialize());
            std::vector<std::string> arguments{fields["python"], "-u", path_utf8(script), "--request",
                path_utf8(config->path() / utf8_path(request_file)), "--result",
                path_utf8(config->path() / utf8_path(result_file))};
            std::vector<const char *> argv;
            for (const auto &arg : arguments)
                argv.push_back(arg.c_str());
            argv.push_back(nullptr);
            auto env = SDL_CreateEnvironment(true);
            check(env != nullptr, "Create publisher environment");
            if (!token.empty())
                SDL_SetEnvironmentVariable(env, "GITHUB_TOKEN", token.c_str(), true);
            SDL_SetEnvironmentVariable(env, "PYTHONIOENCODING", "utf-8", true);
            auto props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#ifdef _WIN32
            // SDL's background option suppresses a console window; result.json carries success/failure.
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
            process = SDL_CreateProcessWithProperties(props);
            SDL_DestroyProperties(props);
            SDL_DestroyEnvironment(env);
            check(process != nullptr, "Start Python (select Python 3.11+ in More options)");
            status = local_only ? "Building and checking locally..." : "Publishing...";
        } catch (const std::exception &error) {
            completed = true;
            status = "Could not start";
            append(error.what());
        }
    }
    void poll() {
        std::string path, field, error;
        auto action = Dialog::Action::Field;
        {
            std::scoped_lock lock(dialog->mutex);
            if (dialog->ready) {
                path = std::move(dialog->path);
                field = dialog->field;
                error = std::move(dialog->error);
                action = dialog->action;
                dialog->ready = false;
            }
        }
        if (!error.empty()) {
            status = "Could not open file picker";
            append(error);
        }
        if (!path.empty()) {
            try {
                auto chosen = utf8_path(path);
                if (action == Dialog::Action::SaveSettings) {
                    save_file(chosen);
                    settings_path = path_utf8(chosen);
                    status = "Settings saved: " + settings_path;
                } else if (action == Dialog::Action::LoadSettings) {
                    load_file(chosen);
                    select_tab = true;
                    settings_path = path_utf8(chosen);
                    save();
                    status = "Settings loaded: " + settings_path;
                } else {
                    fields.at(field) = path_utf8(chosen);
                    if (field == "previous_release")
                        previous_selected();
                }
            } catch (const std::exception &failure) {
                status = "Could not " + std::string(action == Dialog::Action::SaveSettings ? "save" : "load") +
                         " settings - see the log below";
                append(failure.what());
            }
        }
        if (!process)
            return;
        auto drain = [&] {
            auto stream = SDL_GetProcessOutput(process);
            std::array<char, 8192> buffer;
            // Bound each frame's work while draining enough to keep the child unblocked.
            for (int i = 0; i < 128; ++i) {
                const auto count = SDL_ReadIO(stream, buffer.data(), buffer.size());
                if (!count)
                    break;
                // Preserve partial lines between reads.
                log.append(buffer.data(), count);
                scroll_log = true;
            }
            if (log.size() > 1024 * 1024)
                log.erase(0, log.size() - 1024 * 1024);
        };
        drain();
        int exit_code = 0;
        if (!SDL_WaitProcess(process, false, &exit_code))
            return;
        drain();
        SDL_DestroyProcess(process);
        process = nullptr;
        completed = true;
        try {
            if (!config->exists(result_file))
                throw std::runtime_error("Python stopped without a result. Check the log and Python dependencies.");
            auto doc = json::parse(config->read(result_file, 1024 * 1024));
            auto root = yyjson_doc_get_root(doc.get());
            success = yyjson_is_true(json::get(root, "success")) && exit_code == 0;
            if (success) {
                fields["public_key"] = json::string(json::get(root, "public_key"));
                const bool published = yyjson_is_true(json::get(root, "published"));
                status = published ? "Published successfully" : "Local check passed - nothing uploaded";
                if (published) {
                    fields["previous_release"] = json::string(json::get(root, "release_dir"));
                    previous_selected();
                    status = "Published successfully - next update values prepared";
                    first_time = false;
                    select_tab = true;
                } else {
                    resume = true;
                }
                save();
            } else {
                status = "Stopped - see the log below";
            }
        } catch (const std::exception &error) {
            success = false;
            status = "Stopped - see the log below";
            append(error.what());
        }
        config->write_atomic("last-run.log", log);
    }

    void settings_dialog(SDL_Window *window, bool saving) {
        {
            std::scoped_lock lock(dialog->mutex);
            dialog->pending = true;
            dialog->ready = false;
            dialog->action = saving ? Dialog::Action::SaveSettings : Dialog::Action::LoadSettings;
            dialog->path.clear();
            dialog->error.clear();
        }
        static const SDL_DialogFileFilter filters[] = {{"Publisher settings", "json"}, {"All files", "*"}};
        const auto initial = settings_path.empty() ? path_utf8(fs::current_path() / "publisher-settings.json") : settings_path;
        auto state = new std::shared_ptr<Dialog>(dialog);
        if (saving)
            SDL_ShowSaveFileDialog(selected, state, window, filters, 2, initial.c_str());
        else
            SDL_ShowOpenFileDialog(selected, state, window, filters, 2, initial.c_str(), false);
    }
    void row(SDL_Window *window, const char *label, const char *name, int picker = 0,
             const char *hint = "") {
        ImGui::PushID(name);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(picker ? -(80 * scale + ImGui::GetStyle().ItemSpacing.x) : -1);
        const auto flags = picker ? ImGuiInputTextFlags_CallbackEdit : ImGuiInputTextFlags_None;
        ImGui::InputTextWithHint("##value", hint, &fields[name], flags, picker ? normalize_path : nullptr);
        if (std::string_view(name) == "previous_release" && ImGui::IsItemDeactivatedAfterEdit())
            previous_selected();
        if (picker) {
            ImGui::SameLine();
            if (ImGui::Button("Browse", {80 * scale, 0})) {
                {
                    std::scoped_lock lock(dialog->mutex);
                    dialog->pending = true;
                    dialog->ready = false;
                    dialog->action = Dialog::Action::Field;
                    dialog->field = name;
                    dialog->path.clear();
                    dialog->error.clear();
                }
                const auto initial = fields[name];
                auto state = new std::shared_ptr<Dialog>(dialog);
                if (picker == 1)
                    SDL_ShowOpenFolderDialog(selected, state, window, initial.empty() ? nullptr : initial.c_str(), false);
                else
                    SDL_ShowOpenFileDialog(selected, state, window, nullptr, 0, initial.empty() ? nullptr : initial.c_str(), false);
            }
        }
        ImGui::PopID();
    }
    bool table(const char *id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 180 * scale);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }
    void connection(SDL_Window *window) {
        if (table("connection")) {
            row(window, "GitHub owner", "owner", 0, "Account or organization");
            row(window, "Game repository", "repo");
            row(window, "Private key file", "key_file", 2);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("GitHub token");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##token", "Session only; blank uses GITHUB_TOKEN", &token, ImGuiInputTextFlags_Password);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Use an existing public repository with a main branch.");
        if (first_time)
            ImGui::TextDisabled("A missing key is generated once. Browse selects an existing key to reuse.");
    }
    void form(SDL_Window *window) {
        if (first_time)
            connection(window);
        else if (ImGui::CollapsingHeader("Publishing account and key"))
            connection(window);
        ImGui::SeparatorText("Release");
        if (table("release")) {
            if (!first_time)
                row(window, "Previous release", "previous_release", 1);
            row(window, "Game version", "version", 0, "0.1.0");
            row(window, "Sequence", "sequence", 0, "1");
            row(window, "Releases folder", "output_root", 1);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Tag: v%s   |   Output: %s/%s", fields["version"].c_str(),
                           fields["output_root"].c_str(), fields["version"].c_str());
        ImGui::SeparatorText("Unpacked game files");
        if (table("inputs")) {
            row(window, "Shared files", "shared_root", 1, "Optional");
            row(window, "Init shared files", "shared_init_data", 1, "Optional; folder containing Editor/, Profiles/, etc.");
            row(window, "Windows files", "windows_root", 1, "Leave blank if not shipping Windows");
            row(window, "Linux files", "linux_root", 1, "Leave blank if not shipping Linux");
            row(window, "Language packs", "languages_dir", 1, "Folder containing texts-en.pak, etc.");
            row(window, "Optional mods", "mods_dir", 1, "Optional; one subfolder per mod");
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Supply complete input folders, including unchanged files.");
        ImGui::TextDisabled("Init shared files become user-owned after installation (launcher 0.3+).");
        if (ImGui::CollapsingHeader("More options")) {
            if (table("options")) {
                row(window, "Launcher source", "source_dir", 1);
                row(window, "Python 3.11+", "python", 2);
                row(window, "Key ID", "key_id");
                row(window, "Public key", "public_key", 0, "Filled from the private key on first successful run");
                row(window, "Manifest repository", "manifest_repo", 0, "Blank = game repository; otherwise owner/repo");
                row(window, "Manifest branch", "manifest_branch");
                row(window, "CMake executable", "cmake", 2);
                row(window, "CMake generator", "generator", 0, "Blank = CMake default");
                ImGui::EndTable();
            }
            ImGui::TextUnformatted("Official Profiles/MODs files to manage (one exact path per line)");
            ImGui::InputTextMultiline("##managed", &fields["managed_files"], {-1, 62 * scale});
            ImGui::TextUnformatted("Exclude patterns (one per line, e.g. bin/*.log)");
            ImGui::InputTextMultiline("##excludes", &fields["excludes"], {-1, 62 * scale});
            ImGui::Checkbox("Reuse signed output (retry publication)", &resume);
            ImGui::TextWrapped("Reuse keeps the existing packages exactly as signed. Input changes require a new version and sequence.");
        }
    }
};

int run(const Options &options) {
    check(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL");
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    const float scale = std::max(1.f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Rect usable{0, 0, 1100, 950};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Window window(SDL_CreateWindow("OpenBK2 Publisher", std::min(int(950 * scale), usable.w),
                  std::min(int(900 * scale), usable.h), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                  (options.smoke ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
    check(bool(window), "Create publisher window");
    SDL_SetWindowMinimumSize(window.get(), int(660 * scale), int(520 * scale));
    Renderer renderer(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    check(bool(renderer), "Create renderer");
    SDL_SetRenderVSync(renderer.get(), 1);
    Surface icon(SDL_CreateSurfaceFrom(launcher::art::icon::width, launcher::art::icon::height,
        SDL_PIXELFORMAT_RGBA32, const_cast<unsigned char *>(launcher::art::icon::rgba),
        launcher::art::icon::width * 4), SDL_DestroySurface);
    if (icon)
        SDL_SetWindowIcon(window.get(), icon.get());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    struct ImGuiCleanup {
        ~ImGuiCleanup() {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
    } imgui_cleanup;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImFontConfig font;
    font.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(embedded_font, sizeof(embedded_font), 17 * scale, &font);
    check(ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get()), "Initialize ImGui SDL");
    check(ImGui_ImplSDLRenderer3_Init(renderer.get()), "Initialize ImGui renderer");
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = {20, 16};
    style.FramePadding = {9, 6};
    style.ItemSpacing = {10, 8};
    style.FrameRounding = 4;
    style.WindowBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = {.045f, .06f, .08f, 1};
    style.Colors[ImGuiCol_Button] = {.10f, .30f, .29f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {.14f, .43f, .39f, 1};
    style.ScaleAllSizes(scale);
    Publisher app(options);
    app.scale = scale;
    bool running = true, check_started = false;
    int frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (app.process)
                    app.status = "Please wait for the current operation to finish before closing";
                else
                    running = false;
            }
        }
        app.poll();
        if (!options.check_job.empty() && !check_started) {
            app.load(read_file(utf8_path(options.check_job)));
            app.local_only = true; // This test entry point never publishes or runs CMake.
            app.build_launcher = false;
            app.start();
            check_started = true;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Publisher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored({.48f, .85f, .76f, 1}, "OpenBK2 Publisher");
        ImGui::SameLine();
        ImGui::TextDisabled("Build, sign and publish game releases");
        bool dialog_pending;
        {
            std::scoped_lock lock(app.dialog->mutex);
            dialog_pending = app.dialog->pending;
        }
        const bool disabled = app.process || dialog_pending;
        ImGui::BeginDisabled(disabled);
        if (ImGui::SmallButton("Save settings..."))
            app.settings_dialog(window.get(), true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load settings..."))
            app.settings_dialog(window.get(), false);
        ImGui::SameLine();
        ImGui::TextDisabled("Token is never saved");
        if (ImGui::BeginTabBar("workflow")) {
            const bool select_first = app.select_tab && app.first_time;
            const bool select_update = app.select_tab && !app.first_time;
            if (ImGui::BeginTabItem("First-time setup", nullptr, select_first ? ImGuiTabItemFlags_SetSelected : 0)) {
                app.first_time = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("New update", nullptr, select_update ? ImGuiTabItemFlags_SetSelected : 0)) {
                app.first_time = false;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            app.select_tab = false;
        }
        const float footer = 250 * scale;
        ImGui::BeginChild("fields", {0, std::max(120 * scale, ImGui::GetContentRegionAvail().y - footer)});
        app.form(window.get());
        ImGui::EndChild();
        ImGui::Checkbox("Local check only (no upload)", &app.local_only);
        ImGui::SameLine();
        ImGui::Checkbox("Build production launcher for this OS", &app.build_launcher);
        const char *action = app.local_only ? "Build and verify locally" :
                             app.first_time ? "Set up and publish" : "Publish update";
        if (ImGui::Button(action, {250 * scale, 38 * scale}))
            app.start();
        ImGui::SameLine();
        if (app.resume)
            ImGui::TextDisabled("Reusing signed output");
        else
            ImGui::TextDisabled("New packages > signature > verification > %s", app.local_only ? "local check" : "GitHub");
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", app.status.c_str());
        ImGui::Separator();
        if (ImGui::SmallButton("Copy log"))
            SDL_SetClipboardText(app.log.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Progress log");
        ImGui::BeginChild("log", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(app.log.empty() ? "Choose your folders and release settings, then start." : app.log.c_str());
        if (app.scroll_log) {
            ImGui::SetScrollHereY(1.f);
            app.scroll_log = false;
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderScale(renderer.get(), io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer.get(), 12, 16, 20, 255);
        SDL_RenderClear(renderer.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        if (!options.capture.empty() && frames == 2) {
            Surface capture(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
            check(bool(capture), "Capture publisher");
            check(SDL_SaveBMP(capture.get(), options.capture.c_str()), "Save publisher capture");
        }
        SDL_RenderPresent(renderer.get());
        ++frames;
        if (options.smoke && frames >= 3)
            running = false;
        if (check_started && app.completed)
            running = false;
        SDL_Delay(16);
    }
    app.save();
    return check_started && !app.success ? 1 : 0;
}
} // namespace

int main(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-test")
            options.smoke = true;
        else if (arg == "--update-tab")
            options.update_tab = true;
        else if (arg == "--capture" && i + 1 < argc)
            options.capture = argv[++i];
        else if (arg == "--config-dir" && i + 1 < argc)
            options.config = argv[++i];
        else if (arg == "--check-job" && i + 1 < argc)
            options.check_job = argv[++i];
        else
            return 2;
    }
    try {
        if (options.smoke && !options.check_job.empty())
            throw std::runtime_error("Use --smoke-test or --check-job, not both.");
        return run(options);
    } catch (const std::exception &error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        if (!options.smoke && options.check_job.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenBK2 Publisher", error.what(), nullptr);
        return 1;
    }
}
