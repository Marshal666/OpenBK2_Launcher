#include "launcher/application.hpp"
#include "../assets/hero.hpp"
#include "../assets/launcher_icon.hpp"
#include "font.hpp"
#include "launcher/window_layout.hpp"
#include "window_shape.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <memory>
#include <stdexcept>

namespace launcher {
namespace {
void require(bool ok, const char *action) {
    if (!ok)
        throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}
struct SdlLifetime {
    SdlLifetime() {
        require(SDL_Init(SDL_INIT_VIDEO), "Initialize video");
    }
    ~SdlLifetime() {
        SDL_Quit();
    }
};
struct ImGuiLifetime {
    ImGuiLifetime(SDL_Window *window, SDL_Renderer *renderer) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImFontConfig font;
        font.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(embedded_font, sizeof(embedded_font), 18, &font);
        if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) || !ImGui_ImplSDLRenderer3_Init(renderer))
            throw std::runtime_error("Initialize ImGui backends");
    }
    ~ImGuiLifetime() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
};
SDL_HitTestResult SDLCALL hit_test(SDL_Window *window, const SDL_Point *point, void *) {
    if (ImGui::GetCurrentContext() &&
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return SDL_HITTEST_NORMAL;
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    return window_layout(float(w), float(h), SDL_GetWindowDisplayScale(window))
                   .draggable(float(point->x), float(point->y))
               ? SDL_HITTEST_DRAGGABLE
               : SDL_HITTEST_NORMAL;
}
struct DialogState {
    std::mutex mutex;
    bool pending = false, complete = false;
    std::string path, error;
};
// This callback may run on another thread. Only data crosses into the UI loop.
void SDLCALL folder_selected(void *userdata, const char *const *files, int) {
    std::unique_ptr<std::shared_ptr<DialogState>> owner(
        static_cast<std::shared_ptr<DialogState> *>(userdata));
    auto &state = **owner;
    std::scoped_lock lock(state.mutex);
    if (!files)
        state.error = "Folder selection failed";
    else if (files[0])
        state.path = files[0];
    state.pending = false;
    state.complete = true;
}
std::string bytes(std::uint64_t n) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.1f MiB", double(n) / (1024 * 1024));
    return text;
}
const char *tr(std::string_view locale, const char *english) {
    static constexpr std::pair<const char *, const char *> german[] = {
        {"Settings", "Einstellungen"},
        {"Back to launcher", "Zurück zum Launcher"},
        {"Mods", "Mods"},
        {"Download", "Herunterladen"},
        {"Remove", "Entfernen"},
        {"Play", "Spielen"},
        {"Install", "Installieren"},
        {"Update", "Aktualisieren"},
        {"Up to date", "Aktuell"},
        {"Repair", "Reparieren"},
        {"Full reinstall", "Neu installieren"},
        {"Cancel", "Abbrechen"},
        {"Browse", "Durchsuchen"},
        {"Save", "Speichern"},
        {"Close", "Schließen"},
        {"Check for updates", "Updates suchen"},
        {"Game language", "Spielsprache"},
        {"Install OpenBK2", "OpenBK2 installieren"},
        {"Installation folder", "Installationsordner"},
        {"Move launcher into installation folder", "Launcher in Installationsordner verschieben"},
        {"Create desktop shortcut", "Desktop-Verknüpfung erstellen"},
        {"Apply language", "Sprache anwenden"},
        {"Adopt existing installation", "Vorhandene Installation übernehmen"}};
    if (locale == "de")
        for (const auto &[key, value] : german)
            if (english == std::string_view(key))
                return value;
    return english;
}
bool action(const char *title, bool enabled, const char *explanation, ImVec2 size = {}) {
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(title, size);
    ImGui::EndDisabled();
    if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", explanation);
    return clicked;
}
// Returns false when navigation or a submitted operation returns to the launcher.
bool settings_page(SDL_Window *window, Controller &controller, const UiSnapshot &state, Settings &draft,
                   std::array<char, 32768> &folder, const std::shared_ptr<DialogState> &dialog, bool game,
                   bool &pending_install) {
    const auto locale = draft.requested_language;
    auto t = [&](const char *value) { return tr(locale, value); };
    bool stay = true;
    const float back_width = ImGui::CalcTextSize(t("Back to launcher")).x + 24;
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, 26);
    ImGui::TextUnformatted(t(state.settings.configured ? "Settings" : "Install OpenBK2"));
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - back_width);
    if (ImGui::Button(t("Back to launcher")))
        return false;
    const float s = ImGui::GetStyle().FontScaleDpi;
    ImGui::Separator();
    ImGui::BeginDisabled(state.busy);
    ImGui::TextUnformatted(t("Installation folder"));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 135 * s);
    ImGui::InputText("##folder", folder.data(), folder.size());
    ImGui::SameLine();
    bool dialog_pending;
    {
        std::scoped_lock lock(dialog->mutex);
        dialog_pending = dialog->pending;
    }
    if (action(t("Browse"), !dialog_pending, "Folder dialog is open")) {
        {
            std::scoped_lock lock(dialog->mutex);
            dialog->pending = true;
        }
        SDL_ShowOpenFolderDialog(folder_selected, new std::shared_ptr<DialogState>(dialog), window,
                                 folder[0] ? folder.data() : nullptr, false);
    }
    ImGui::TextWrapped("Selecting an existing folder adopts its verified files. Changing this "
                       "path does not move game data.");
    ImGui::TextUnformatted(t("Game language"));
    ImGui::SetNextItemWidth(230 * s);
    if (ImGui::BeginCombo("##language", draft.requested_language.c_str())) {
        if (state.latest)
            for (const auto &[code, language] : state.latest->languages) {
                const auto label = language.label + " (" + code + ")";
                if (ImGui::Selectable(label.c_str(), code == draft.requested_language))
                    draft.requested_language = code;
            }
        else
            ImGui::TextDisabled("English / waiting for signed release");
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("UI: English / partial Deutsch; other game languages use English UI.");
    ImGui::Checkbox(t("Move launcher into installation folder"), &draft.relocate);
    ImGui::Checkbox(t("Create desktop shortcut"), &draft.shortcut);
    ImGui::TextWrapped("Download: up to %s. Estimated working space: %s. Space and writability "
                       "are checked before changes.",
                       bytes(state.download_bytes).c_str(), bytes(state.working_bytes).c_str());
    {
        std::scoped_lock lock(dialog->mutex);
        if (!dialog->error.empty())
            ImGui::TextWrapped("%s", dialog->error.c_str());
    }
    ImGui::Separator();
    const bool path_changed = path_utf8(state.settings.root) != folder.data();
    if (ImGui::Button(t("Save"))) {
        draft.root = utf8_path(folder.data());
        controller.configure(draft);
        stay = false;
    }
    ImGui::SameLine();
    if (action(t(state.settings.configured ? "Apply language" : "Install"),
               bool(state.latest) && !state.root_missing && !game,
               "Wait for a verified release, or save a reselected folder first")) {
        draft.root = utf8_path(folder.data());
        controller.configure(draft);
        pending_install = true;
        stay = false;
    }
    if (state.settings.configured && !path_changed) {
        if (action(t("Repair"), bool(state.latest) && !game, "Verified release required")) {
            controller.install(InstallMode::Repair);
            stay = false;
        }
        ImGui::SameLine();
        if (action(t("Full reinstall"), bool(state.latest) && !game, "Verified release required"))
            ImGui::OpenPopup("Confirm reinstall");
        if (ImGui::BeginPopupModal("Confirm reinstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restore all managed files. Modified files are backed up; "
                                   "user files are preserved.");
            if (ImGui::Button("Continue")) {
                controller.install(InstallMode::Reinstall);
                stay = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(t("Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(t("Create desktop shortcut")))
            controller.shortcut();
    }
    if (action(t("Check for updates"), !state.checking, "Already checking"))
        controller.check_updates();
    if (state.installed)
        ImGui::TextDisabled("Installed: %s / %s", state.installed->manifest.version.c_str(),
                            state.installed->language.c_str());
    ImGui::EndDisabled();
    if (!state.error.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored({1, .65f, .5f, 1}, "%s", state.error.c_str());
        ImGui::PopTextWrapPos();
    } else if (state.checking)
        ImGui::TextDisabled("Checking for updates...");
    else
        ImGui::TextWrapped("%s", state.status.c_str());
    return stay;
}
void popup_geometry(ImVec2 viewport, float scale, ImVec2 initial) {
    const ImVec2 maximum{std::max(1.f, viewport.x - 24 * scale), std::max(1.f, viewport.y - 32 * scale)};
    ImGui::SetNextWindowSize({std::min(initial.x * scale, maximum.x), std::min(initial.y * scale, maximum.y)},
                             ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints({std::min(560 * scale, maximum.x), std::min(360 * scale, maximum.y)},
                                        maximum);
    // Position only on opening, so dragging the resize grip has a fixed origin.
    ImGui::SetNextWindowPos({viewport.x * .5f, viewport.y * .5f}, ImGuiCond_Appearing, {.5f, .5f});
}
} // namespace
int run_application(const ApplicationOptions &options) {
    SdlLifetime sdl;
    float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Rect usable{0, 0, 1000, 600};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
    Window window(SDL_CreateWindow("OpenBK2 Launcher", std::min(int(1000 * scale), usable.w),
                                   std::min(int(600 * scale), usable.h),
                                   SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY |
#ifndef _WIN32
                                       SDL_WINDOW_TRANSPARENT |
#endif
                                       (options.smoke_test ? SDL_WINDOW_HIDDEN : 0)),
                  SDL_DestroyWindow);
    require(bool(window), "Create window");
    Renderer renderer(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    require(bool(renderer), "Create renderer");
    SDL_SetRenderVSync(renderer.get(), 1);
    const bool drag_available = SDL_SetWindowHitTest(window.get(), hit_test, nullptr);
    WindowShape shape;
    Surface artwork(SDL_CreateSurfaceFrom(art::width, art::height, SDL_PIXELFORMAT_ARGB8888,
                                          const_cast<std::uint32_t *>(art::hero.data()), art::width * 4),
                    SDL_DestroySurface);
    require(bool(artwork), "Embedded artwork");
    Surface icon(SDL_CreateSurfaceFrom(art::icon::width, art::icon::height, SDL_PIXELFORMAT_RGBA32,
                                       const_cast<unsigned char *>(art::icon::rgba), art::icon::width * 4),
                 SDL_DestroySurface);
    require(bool(icon), "Embedded launcher icon");
    // Some compositors do not support setting a window icon; the desktop entry
    // also supplies it on Linux.
    SDL_SetWindowIcon(window.get(), icon.get());
    Texture hero(SDL_CreateTextureFromSurface(renderer.get(), artwork.get()), SDL_DestroyTexture);
    require(bool(hero), "Upload artwork");
    SDL_SetTextureScaleMode(hero.get(), SDL_SCALEMODE_LINEAR);
    ImGuiLifetime imgui(window.get(), renderer.get());
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 10;
    style.FrameRounding = 6;
    style.WindowBorderSize = 0;
    style.FramePadding = {12, 8};
    style.ItemSpacing = {12, 10};
    style.WindowPadding = {20, 18};
    style.Colors[ImGuiCol_WindowBg] = {.035f, .047f, .063f, 1};
    style.Colors[ImGuiCol_Button] = {.12f, .21f, .22f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {.18f, .36f, .34f, 1};
    style.Colors[ImGuiCol_PlotHistogram] = {.32f, .8f, .61f, 1};
    std::unique_ptr<Controller> controller;
    if (!options.smoke_test) {
        controller = std::make_unique<Controller>(options);
        controller->initialize();
    }
    auto dialog = std::make_shared<DialogState>();
    std::array<char, 32768> folder{};
    Settings draft;
    bool draft_loaded = false, show_settings = false, show_mods = false, first_shown = false;
    std::string remove_mod;
    bool running = true, closing = false, close_confirmed = false, check_started = false,
         pending_install = false, automated_started = false, captured = false, progress_captured = false;
    int frames = 0, result = 0;
    while (running) {
        SDL_Event event;
        auto process_event = [&](const SDL_Event &e) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                closing = true;
        };
        if (!options.smoke_test && SDL_WaitEventTimeout(&event, 32))
            process_event(event);
        while (SDL_PollEvent(&event))
            process_event(event);
        auto state = controller ? controller->snapshot() : UiSnapshot{};
        if (state.exit_requested)
            break;
        if (controller && state.initialized && !check_started) {
            controller->check_updates();
            check_started = true;
        }
        if (pending_install && !state.busy) {
            pending_install = false;
            if (state.error.empty())
                controller->install();
        }
        if (options.auto_install && state.initialized && !state.busy && !state.checking && check_started) {
            if (!automated_started && state.latest) {
                controller->install();
                automated_started = true;
            } else if (automated_started) {
                if (!state.ready || !state.error.empty())
                    result = 1;
                closing = true;
            } else if (!state.error.empty()) {
                result = 1;
                closing = true;
            }
        }
        if (closing && !state.busy && !options.smoke_test &&
            (!options.auto_install || captured || options.capture.empty()))
            break;
        auto load_draft = [&] {
            draft = state.settings;
            const auto text = path_utf8(draft.root);
            std::snprintf(folder.data(), folder.size(), "%s", text.c_str());
            draft_loaded = true;
        };
        if (!draft_loaded && (state.initialized || options.smoke_test))
            load_draft();
        if (!first_shown && state.initialized && !state.settings.configured && !options.auto_install) {
            show_settings = true;
            first_shown = true;
        }
        {
            std::scoped_lock lock(dialog->mutex);
            if (dialog->complete) {
                if (!dialog->path.empty())
                    std::snprintf(folder.data(), folder.size(), "%s", dialog->path.c_str());
                dialog->complete = false;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        const float s = std::max(1.f, SDL_GetWindowDisplayScale(window.get()));
        shape.update(window.get(), 10 * s);
        style.FontScaleDpi = s;
        ImGui::NewFrame();
        const auto size = ImGui::GetIO().DisplaySize;
        const auto locale = show_settings ? draft.requested_language : state.settings.requested_language;
        auto t = [&](const char *value) { return tr(locale, value); };
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin("OpenBK2", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        ImGui::SetCursorPos({24 * s, 14 * s});
        ImGui::TextUnformatted("OPENBK2");
        ImGui::SetCursorPos({size.x - 396 * s, 6 * s});
        if (action(t("Mods"), !state.busy && bool(state.installed), "Install the game first",
                   {126 * s, 36 * s}))
            show_mods = true;
        ImGui::SetCursorPos({size.x - 258 * s, 6 * s});
        if (show_settings)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .32f, 1));
        const bool open_settings =
            action(t("Settings"), !state.busy, "Wait for the current operation", {126 * s, 36 * s});
        if (show_settings)
            ImGui::PopStyleColor();
        if (open_settings && !show_settings) {
            load_draft();
            show_settings = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("_", {42 * s, 36 * s}))
            SDL_MinimizeWindow(window.get());
        ImGui::SameLine();
        if (ImGui::Button("X", {42 * s, 36 * s}))
            closing = true;
        const bool game = controller && controller->game_running();
        if (show_settings) {
            ImGui::GetWindowDrawList()->AddLine({0, 52 * s}, {size.x, 52 * s},
                                                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::SetCursorPos({32 * s, 64 * s});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 6});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 6});
            // A borderless content region keeps scrolling on small screens local
            // to the form, while the app header and footer stay in place.
            if (ImGui::BeginChild("settings-content",
                                  {std::max(1.f, size.x - 64 * s), std::max(1.f, size.y - 108 * s)}))
                show_settings = settings_page(window.get(), *controller, state, draft, folder, dialog, game,
                                              pending_install);
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        } else {
            const float bottom = std::max(220 * s, size.y - 165 * s);
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(hero.get())), {0, 52 * s},
                           {size.x, bottom});
            draw->AddRectFilledMultiColor({0, 52 * s}, {size.x, bottom}, IM_COL32(4, 8, 12, 15),
                                          IM_COL32(4, 8, 12, 15), IM_COL32(4, 8, 12, 235),
                                          IM_COL32(4, 8, 12, 235));
            ImGui::SetCursorPos({34 * s, bottom - 124 * s});
            ImGui::PushFont(nullptr, 42);
            ImGui::TextUnformatted("OPENBK2");
            ImGui::PopFont();
            ImGui::SetCursorPos({36 * s, bottom - 66 * s});
            ImGui::TextUnformatted("Your next campaign starts here.");
            ImGui::SetCursorPos({36 * s, bottom - 35 * s});
            ImGui::TextColored({.5f, .85f, .7f, 1}, "%s",
                               OBK_FIXTURE_BUILD ? "DEVELOPMENT BUILD  /  SIGNED FIXTURES"
                                                 : "INDEPENDENT COMMUNITY LAUNCHER");
            const bool current = state.ready && state.latest && state.installed &&
                                 state.latest->sequence == state.installed->manifest.sequence &&
                                 state.settings.requested_language == state.installed->language;
            ImGui::SetCursorPos({32 * s, bottom + 14 * s});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .32f, 1));
            if (action(t("Play"), state.ready && !state.busy && !game,
                       "Finish installation or repair first; close an already running game",
                       {160 * s, 44 * s}))
                controller->play();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            const char *install_label = current ? "Up to date" : (state.installed ? "Update" : "Install");
            if (action(t(install_label),
                       !state.busy && !game && bool(state.latest) && !current && !state.root_missing,
                       "A verified release and available game folder are required", {160 * s, 44 * s})) {
                if (!state.settings.configured) {
                    load_draft();
                    show_settings = true;
                } else
                    controller->install();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (state.installed)
                ImGui::Text("%s  |  %s", state.installed->manifest.version.c_str(),
                            state.installed->language.c_str());
            else
                ImGui::TextUnformatted("No managed installation");
            ImGui::TextDisabled("%s", state.checking ? "Checking for updates..."
                                                     : (game ? "Game is running" : state.status.c_str()));
            ImGui::EndGroup();
            ImGui::SetCursorPos({32 * s, bottom + 72 * s});
            if (state.busy) {
                const auto &p = state.progress;
                ImGui::Text("%s  %s / %s  %s/s", p.stage.c_str(), bytes(p.completed).c_str(),
                            bytes(p.total).c_str(),
                            bytes(std::uint64_t(std::max(0., p.bytes_per_second))).c_str());
                if (p.total)
                    ImGui::ProgressBar(float(double(p.completed) / double(p.total)),
                                       {size.x - 170 * s, 7 * s}, "");
                else
                    ImGui::TextDisabled("Working...");
                ImGui::SameLine();
                if (ImGui::SmallButton(t("Cancel")))
                    controller->cancel();
            } else if (!state.error.empty()) {
                ImGui::PushTextWrapPos(size.x - 28 * s);
                ImGui::TextColored({1, .65f, .5f, 1}, "%s", state.error.c_str());
                ImGui::PopTextWrapPos();
            } else if (state.installed && state.settings.requested_language != state.installed->language)
                ImGui::TextWrapped("Pending game language: %s. Offline Play uses %s.",
                                   state.settings.requested_language.c_str(),
                                   state.installed->language.c_str());
            else if (!state.ready)
                ImGui::TextWrapped("%s", state.status.c_str());
        }
        ImGui::SetCursorPos({32 * s, size.y - 27 * s});
        ImGui::TextDisabled("v%s%s", OBK_VERSION,
                            drag_available ? "" : "  |  Use the desktop window menu to move this window");
        ImGui::End();
        if (show_mods) {
            popup_geometry(size, s, {710, 520});
            ImGui::OpenPopup("Mods###mods");
            if (ImGui::BeginPopupModal("Mods###mods", nullptr,
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextWrapped(
                    "The game loads one mod at a time. Remove the current mod before downloading another.");
                ImGui::BeginChild("mod-list", {0, -125 * s});
                ImGui::SeparatorText("Installed folders");
                if (state.mods.empty())
                    ImGui::TextDisabled("No mod installed");
                for (const auto &mod : state.mods) {
                    ImGui::PushID(mod.id.c_str());
                    ImGui::TextUnformatted(mod.id.c_str());
                    ImGui::SameLine();
                    if (action(t("Remove"), !state.busy && !game && !mod.managed_by_game,
                               "Close the game first. Base-game folders cannot be removed here."))
                        remove_mod = mod.id;
                    ImGui::PopID();
                }
                ImGui::SeparatorText("Available downloads");
                if (!state.latest || state.latest->mods.empty())
                    ImGui::TextWrapped("No downloadable mods in the current signed release.");
                if (state.latest)
                    for (const auto &[id, mod] : state.latest->mods) {
                        ImGui::PushID(id.c_str());
                        std::uint64_t download = 0;
                        for (const auto &pid : mod.packages)
                            download += state.latest->packages.at(pid).size;
                        ImGui::Text("%s (%s)", mod.label.c_str(), bytes(download).c_str());
                        ImGui::SameLine();
                        const bool matching = state.installed && state.latest->payload_hash ==
                                                                     state.installed->manifest.payload_hash;
                        if (action(
                                t("Download"),
                                !state.busy && !game && state.ready && state.mods.empty() && matching,
                                "Update or repair the game, close it, and remove any installed mod first."))
                            controller->download_mod(id);
                        ImGui::PopID();
                    }
                ImGui::EndChild();
                if (!remove_mod.empty())
                    ImGui::OpenPopup("Remove mod?");
                if (ImGui::BeginPopupModal("Remove mod?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Remove %s?", remove_mod.c_str());
                    ImGui::TextUnformatted("The whole folder, including your edits, will be moved\nto "
                                           ".launcher/backups/removed-mods. Nothing is deleted.");
                    if (ImGui::Button(t("Remove"))) {
                        controller->remove_mod(remove_mod);
                        remove_mod.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(t("Cancel"))) {
                        remove_mod.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                if (state.busy) {
                    ImGui::Text("%s: %s / %s", state.progress.stage.c_str(),
                                bytes(state.progress.completed).c_str(), bytes(state.progress.total).c_str());
                    if (ImGui::Button(t("Cancel")))
                        controller->cancel();
                } else if (!state.error.empty())
                    ImGui::TextWrapped("%s", state.error.c_str());
                else
                    ImGui::TextWrapped("%s", state.status.c_str());
                if (ImGui::Button(t("Close"))) {
                    show_mods = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        if (closing && state.busy && !close_confirmed) {
            ImGui::OpenPopup("Close launcher?");
            if (ImGui::BeginPopupModal("Close launcher?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Cancel the operation and exit? Downloads remain resumable.\nAn "
                                       "active commit will finish safely before exit.");
                if (ImGui::Button("Cancel and exit")) {
                    controller->cancel();
                    close_confirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep open")) {
                    closing = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::Render();
        SDL_SetRenderScale(renderer.get(), ImGui::GetIO().DisplayFramebufferScale.x,
                           ImGui::GetIO().DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer.get(), 9, 12, 16, 255);
        SDL_RenderClear(renderer.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        shape.clip(renderer.get());
        if (!options.progress_capture.empty() && !progress_captured &&
            state.progress.stage == "Downloading" && state.progress.completed > 0) {
            Surface capture(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
            require(bool(capture), "Capture progress");
            require(SDL_SaveBMP(capture.get(), options.progress_capture.c_str()), "Save progress capture");
            progress_captured = true;
        }
        if (!options.capture.empty() && !captured &&
            ((!options.auto_install && frames == 2) || (options.auto_install && closing))) {
            Surface capture(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
            require(bool(capture), "Capture frame");
            require(SDL_SaveBMP(capture.get(), options.capture.c_str()), "Save capture");
            captured = true;
        }
        require(SDL_RenderPresent(renderer.get()), "Present frame");
        ++frames;
        if (options.smoke_test && frames == 3)
            running = false;
    }
    return result;
}
} // namespace launcher
