#include "launcher/application.hpp"
#include "../assets/launcher_icon.hpp"
#include "font.hpp"
#include "hero.hpp"
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
int folder_path_edited(ImGuiInputTextCallbackData *data) {
#ifdef _WIN32
    // Normalize typed and pasted separators inside ImGui's active editing buffer.
    for (int i = 0; i < data->BufTextLen; ++i)
        if (data->Buf[i] == '\\') {
            data->Buf[i] = '/';
            data->BufDirty = true;
        }
#else
    (void)data; // A backslash can be part of a Linux filename.
#endif
    return 0;
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
        {"Minimize", "Minimieren"},
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
enum class WindowControl { Minimize, Close };
bool window_control(const char *title, WindowControl control, ImVec2 position, float scale) {
    const float diameter = 28 * scale;
    ImGui::SetCursorPos(position);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(title, {diameter, diameter}, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    auto color = ImGui::GetColorU32(held && hovered ? ImGuiCol_ButtonActive
                                    : hovered       ? ImGuiCol_ButtonHovered
                                                    : ImGuiCol_Button);
    if (control == WindowControl::Close && hovered)
        color = ImGui::GetColorU32(held ? ImVec4(.52f, .12f, .15f, 1) : ImVec4(.7f, .18f, .22f, 1));
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 center{origin.x + diameter * .5f, origin.y + diameter * .5f};
    draw->AddCircleFilled(center, diameter * .5f, color);
    const auto symbol = ImGui::GetColorU32(ImGuiCol_Text);
    const float stroke = 1.5f * scale;
    // Geometric glyphs keep both symbols centered, independent of font baselines.
    if (control == WindowControl::Close) {
        const float arm = 4 * scale;
        draw->AddLine({center.x - arm, center.y - arm}, {center.x + arm, center.y + arm}, symbol, stroke);
        draw->AddLine({center.x - arm, center.y + arm}, {center.x + arm, center.y - arm}, symbol, stroke);
    } else {
        const float arm = 5 * scale;
        draw->AddLine({center.x - arm, center.y}, {center.x + arm, center.y}, symbol, stroke);
    }
    ImGui::SetItemTooltip("%s", title);
    return clicked;
}
ImVec4 cover_uv(ImVec2 source, ImVec2 destination) {
    const float scale = std::max(destination.x / source.x, destination.y / source.y);
    const ImVec2 visible{destination.x / (source.x * scale), destination.y / (source.y * scale)};
    return {(1 - visible.x) * .5f, (1 - visible.y) * .5f, (1 + visible.x) * .5f, (1 + visible.y) * .5f};
}
// Bottom-align status above the action strip. Long errors can scroll within this
// transparent region without covering the header or hiding the action buttons.
float installation_status(Controller *controller, const UiSnapshot &state, bool game, ImVec2 bottom_left,
                          ImVec2 maximum_size, float scale) {
    std::string text;
    if (state.busy) {
        const auto &p = state.progress;
        text = p.stage + "\n" + bytes(p.completed) + " / " + bytes(p.total) + "  " +
               bytes(std::uint64_t(std::max(0., p.bytes_per_second))) + "/s";
    } else if (!state.error.empty())
        text = state.error;
    else
        text = state.checking ? "Checking for updates..." : (game ? "Game is running" : state.status);
    if (!state.busy && state.error.empty() && state.installed &&
        state.settings.requested_language != state.installed->language)
        text += "\nPending game language: " + state.settings.requested_language + ". Offline Play uses " +
                state.installed->language + ".";
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 6});
    const float wrap = std::max(1.f, maximum_size.x - 16 * scale);
    const float extra = state.busy ? ImGui::GetFrameHeightWithSpacing() + 14 * scale : 0;
    const float height = std::min(
        maximum_size.y, ImGui::GetTextLineHeightWithSpacing() +
                            ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y + extra + 8 * scale);
    const float top = bottom_left.y - height;
    ImGui::SetCursorPos({bottom_left.x, top});
    if (ImGui::BeginChild("installation-status", {maximum_size.x, height}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground)) {
        ImGui::TextColored({.75f, .8f, .82f, 1}, "Installation status");
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(!state.busy && !state.error.empty() ? ImVec4(1, .65f, .5f, 1) : ImVec4(1, 1, 1, 1),
                           "%s", text.c_str());
        ImGui::PopTextWrapPos();
        if (state.busy) {
            const auto &p = state.progress;
            if (p.total)
                ImGui::ProgressBar(float(double(p.completed) / double(p.total)), {-1, 7 * scale}, "");
            if (ImGui::SmallButton(tr(state.settings.requested_language, "Cancel")))
                controller->cancel();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return top;
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
    ImGui::InputText("##folder", folder.data(), folder.size(), ImGuiInputTextFlags_CallbackEdit,
                     folder_path_edited);
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
    Surface artwork(SDL_LoadBMP_IO(SDL_IOFromConstMem(embedded_hero_bmp, sizeof(embedded_hero_bmp)), true),
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
                if (!dialog->path.empty()) {
                    const auto text = path_utf8(utf8_path(dialog->path));
                    std::snprintf(folder.data(), folder.size(), "%s", text.c_str());
                }
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
        // This is a fixed canvas. Only child panels may scroll; hidden scrollbars
        // alone do not prevent wheel or keyboard navigation from moving the shell.
        ImGui::SetNextWindowScroll({0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("OpenBK2", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
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
        // Anchor the small window controls separately from Mods and Settings.
        // 10-unit outer inset, 28-unit circles and an 8-unit gap between them.
        if (window_control(t("Minimize"), WindowControl::Minimize, {size.x - 74 * s, 10 * s}, s))
            SDL_MinimizeWindow(window.get());
        if (window_control(t("Close"), WindowControl::Close, {size.x - 38 * s, 10 * s}, s))
            closing = true;
        const bool game = controller && controller->game_running();
        const float footer_height = std::max(56 * s, (size.y - 52 * s) * .1f);
        const float footer_top = size.y - footer_height;
        const float button_gap = 12 * s;
        const float button_width = std::min(160 * s, std::max(1.f, (size.x - 64 * s - button_gap) * .5f));
        const float buttons_width = 2 * button_width + button_gap;
        const float buttons_left = size.x - 32 * s - buttons_width;
        if (show_settings) {
            ImGui::GetWindowDrawList()->AddLine({0, 52 * s}, {size.x, 52 * s},
                                                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::SetCursorPos({32 * s, 64 * s});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 6});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 6});
            // A borderless content region keeps scrolling on small screens local
            // to the form, while the app header and footer stay in place.
            if (ImGui::BeginChild("settings-content",
                                  {std::max(1.f, size.x - 64 * s), std::max(1.f, footer_top - 72 * s)}))
                show_settings = settings_page(window.get(), *controller, state, draft, folder, dialog, game,
                                              pending_install);
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        } else {
            const ImVec2 image_begin{0, 52 * s}, image_end{size.x, footer_top};
            const ImVec2 image_size{image_end.x, image_end.y - image_begin.y};
            const auto uv = cover_uv({float(artwork->w), float(artwork->h)}, image_size);
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(hero.get())), image_begin, image_end,
                           {uv.x, uv.y}, {uv.z, uv.w});
            // Shade only the lower part so overlay text remains legible without
            // obscuring the artwork across the rest of the content area.
            draw->AddRectFilledMultiColor({0, std::max(image_begin.y, footer_top - 210 * s)}, image_end,
                                          IM_COL32(4, 8, 12, 0), IM_COL32(4, 8, 12, 0),
                                          IM_COL32(4, 8, 12, 245), IM_COL32(4, 8, 12, 245));
            const float status_top =
                installation_status(controller.get(), state, game, {buttons_left, footer_top - 16 * s},
                                    {buttons_width, std::max(1.f, image_size.y - 32 * s)}, s);
            ImGui::PushFont(nullptr, 42);
            const float title_width = ImGui::CalcTextSize("OPENBK2").x;
            const float title_top =
                buttons_left > title_width + 64 * s ? footer_top - 66 * s : status_top - 60 * s;
            ImGui::SetCursorPos({32 * s, std::max(image_begin.y + 16 * s, title_top)});
            ImGui::TextUnformatted("OPENBK2");
            ImGui::PopFont();
            const bool current = state.ready && state.latest && state.installed &&
                                 state.latest->sequence == state.installed->manifest.sequence &&
                                 state.settings.requested_language == state.installed->language;
            ImGui::SetCursorPos({buttons_left, footer_top + (footer_height - 44 * s) * .5f});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .32f, 1));
            if (action(t("Play"), state.ready && !state.busy && !game,
                       "Finish installation or repair first; close an already running game",
                       {button_width, 44 * s}))
                controller->play();
            ImGui::PopStyleColor();
            ImGui::SameLine(0, button_gap);
            const char *install_label = current ? "Up to date" : (state.installed ? "Update" : "Install");
            if (action(t(install_label),
                       !state.busy && !game && bool(state.latest) && !current && !state.root_missing,
                       "A verified release and available game folder are required", {button_width, 44 * s})) {
                if (!state.settings.configured) {
                    load_draft();
                    show_settings = true;
                } else
                    controller->install();
            }
        }
        ImGui::SetCursorPos({32 * s, footer_top + (footer_height - ImGui::GetTextLineHeight()) * .5f});
        ImGui::PushClipRect({0, footer_top}, {show_settings ? size.x : buttons_left - 12 * s, size.y}, true);
        ImGui::TextDisabled("Launcher version: %s", OBK_VERSION);
        if (!drag_available && ImGui::IsItemHovered())
            ImGui::SetTooltip("Use the desktop window menu to move this window");
        if (state.installed) {
            ImGui::SameLine(0, 24 * s);
            ImGui::TextColored({.5f, .85f, .7f, 1}, "Game version: %s",
                               state.installed->manifest.version.c_str());
        }
        ImGui::PopClipRect();
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
