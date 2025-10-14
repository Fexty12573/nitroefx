#include "application.h"
#include "application_colors.h"
#include "fonts/IconsFontAwesome6.h"
#include "imgui/extensions.h"

#include <curl/curl.h>
#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <fmt/compile.h>
#include <fmt/ranges.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <stb_image.h>
#include <nlohmann/json.hpp>
#include <battery/embed.hpp>
#include <zlib.h>
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip_rw.h>
#include <microtar.h>
#include <chrono>
#include <ranges>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <ShObjIdl.h>
#else
#include <fcntl.h>
#endif
#include <tinyfiledialogs.h>
#include "util/wsl.h"

#define KEYBINDSTR(name) getKeybind(ApplicationAction::name)->toString().c_str()

static void debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        spdlog::error("OpenGL Error: {}", message);
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        spdlog::warn("OpenGL Error: {}", message);
        break;
    case GL_DEBUG_SEVERITY_LOW:
        spdlog::info("OpenGL Error: {}", message);
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        spdlog::debug("OpenGL Info: {}", message);
        break;
    default:
        break;
    }
}

Application::Application() {
    if (g_application) {
        spdlog::error("Application already exists");
        throw std::runtime_error("Application already exists");
    }

    g_application = this;

    m_sortedActions = {
        ApplicationAction::NewFile,
        ApplicationAction::OpenProject,
        ApplicationAction::OpenFile,
        ApplicationAction::Save,
        ApplicationAction::SaveAll,
        ApplicationAction::Close,
        ApplicationAction::CloseAll,
        ApplicationAction::Exit,
        ApplicationAction::Undo,
        ApplicationAction::Redo,
        ApplicationAction::PlayEmitter,
        ApplicationAction::PlayEmitterLooped,
        ApplicationAction::PlayAllEmitters,
        ApplicationAction::KillEmitters,
        ApplicationAction::ResetCamera,
        ApplicationAction::QuickOpen,
    };

    m_modifierKeys = {
        SDLK_LCTRL, SDLK_RCTRL,
        SDLK_LSHIFT, SDLK_RSHIFT,
        SDLK_LALT, SDLK_RALT,
        SDLK_LGUI, SDLK_RGUI,
        SDLK_LMETA, SDLK_RMETA,
        SDLK_LHYPER, SDLK_RHYPER,
    };
}

int Application::run(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::error("SDL_Init Error: {}", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    // Use a compatibility profile so legacy (immediate mode) APIs are available when needed
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    constexpr auto windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    m_window = SDL_CreateWindow("NitroEFX", 1280, 720, windowFlags);
    if (m_window == nullptr) {
        spdlog::error("SDL_CreateWindow Error: {}", SDL_GetError());
        return 1;
    }

    m_context = SDL_GL_CreateContext(m_window);
    SDL_GL_MakeCurrent(m_window, m_context);

    if (!SDL_GL_SetSwapInterval(-1)) { // Try to enable adaptive vsync
        spdlog::warn("Adaptive vsync not supported, falling back to standard vsync");
        SDL_GL_SetSwapInterval(1); // Enable vsync
    }

    glewExperimental = GL_TRUE;
    const GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        spdlog::error("GLEW Error: {}", (const char*)glewGetErrorString(glewError));
        return 1;
    }

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

#ifdef _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);
#else
    glDisable(GL_DEBUG_OUTPUT);
#endif

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

#ifdef _WIN32
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#else
    // Multi-viewport doesn't work very well under WSL
    if (!WSLUtil::isRunningUnderWSL()) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
#endif

    m_iniFilename = (getConfigPath() / "nitroefx.ini").string();
    io.IniFilename = m_iniFilename.c_str();

    m_editor = std::make_unique<Editor>();
    m_settings = ApplicationSettings::getDefault();
    g_projectManager->init(m_editor.get());

    clearTempDir();
    loadConfig();
    loadFonts();
    loadIcon();
    setColors();

    m_versionCheckResult = checkForUpdates();

    // loadConfig might change the window size so we create the window
    // in a hidden state and show it after loading the config
    SDL_ShowWindow(m_window);

    ImGui_ImplSDL3_InitForOpenGL(m_window, m_context);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_preferencesWindowId = ImHashStr("Preferences##Application");
    m_aboutWindowId = ImHashStr("About##Application");
    m_updateWindowId = ImHashStr("Update##Application");
    m_welcomeWindowId = ImHashStr("Welcome##Application");


    using clock = std::chrono::high_resolution_clock;
    auto lastFrame = clock::now();

    while (m_running) {
        auto now = clock::now();
        auto delta = std::chrono::duration<float>(now - lastFrame).count();
        m_deltaTime = delta;

        const bool minimized = isWindowMinimizedOrHidden();
        const bool focused = isWindowFocused();
        const bool activeEmitters = hasActiveEmitters();

        bool idle = false;
        if (minimized) idle = true;
        else if (!focused && !activeEmitters) idle = true;

        pollEvents();

        if (m_reloadFonts) {
            loadFonts();
            m_reloadFonts = false;
        }

        if (!idle || activeEmitters) {
            m_editor->updateParticles(delta);
        } else if (idle && !activeEmitters) {
            m_idleAccumulator += delta;
            if (m_idleAccumulator > 0.5f) {
                m_editor->updateParticles(m_idleAccumulator);
                m_idleAccumulator = 0.0f;
            }
        }

        if (!minimized) {
            m_editor->renderParticles();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Build default docking layout once if no ini file exists
            if (!m_layoutInitialized) {
                initDefaultDockingLayout();
                checkArgs(argc, argv);
            }

            ImGui::DockSpaceOverViewport(ImGui::GetID("DockSpace"));

            renderMenuBar();
            g_projectManager->render();
            m_editor->render();

            if (m_preferencesOpen) {
                const auto center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });

                renderPreferences();
            }

            if (m_performanceWindowOpen) {
                renderPerformanceWindow();
            }

            if (m_aboutWindowOpen) {
                renderAboutWindow();
            }

            if (m_firstFrame) {
                ImGui::PushOverrideID(m_welcomeWindowId);
                ImGui::OpenPopup("Welcome to NitroEFX");
                ImGui::PopID();
                m_firstFrame = false;
            }

            renderWelcomeWindow();

            renderUpdateWindow();

            renderRestartPopup();

            ImGui::Render();
            glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
            glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
                SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                SDL_GL_MakeCurrent(currentWindow, currentContext);
            }

            SDL_GL_SwapWindow(m_window);
        } else {
            // When minimized, small sleep to avoid busy loop
            // and skip all rendering
            SDL_Delay(10);
        }

        lastFrame = now;
    }

    g_projectManager->closeProject(true);

    if (m_updateOnClose) {
        const auto archivePath = downloadLatestArchive();
        const auto binaryPath = extractLatestArchive(archivePath);
        applyUpdateNow(binaryPath, false);
    } 

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    saveConfig();

    return 0;
}

int Application::runCli(argparse::ArgumentParser& parser) {
    if (parser.is_subcommand_used("export")) {
        const auto& cmd = parser.at<argparse::ArgumentParser>("export");
        const auto output = cmd.get<std::string>("--output");
        const auto indices = cmd.get<std::vector<int>>("--index");

        std::filesystem::path filePath(cmd.get<std::string>("path"));
        if (!SPLArchive::isValid(filePath)) {
            spdlog::error("Invalid SPL file: {}", filePath.string());
            return 1;
        }

        SPLArchive archive(filePath, /* createGpuTextures */ false);

        std::filesystem::path outputPath = output.empty() ? std::filesystem::current_path() : std::filesystem::path(output);

        if (indices.empty()) {
            std::filesystem::create_directories(outputPath);
            archive.exportTextures(outputPath);
            spdlog::info("Exported {} textures to {}", archive.getTextureCount(), outputPath.string());
            return 0;
        }

        if (indices.size() == 1) {
            const int index = indices[0];
            if (index < 0 || static_cast<size_t>(index) >= archive.getTextureCount()) {
                spdlog::error("Invalid texture index: {}", index);
                return 1;
            }

            if (std::filesystem::is_directory(outputPath)) {
                std::string filename = fmt::format("texture_{}.png", index);
                outputPath /= filename;
            }

            archive.exportTexture(index, outputPath);
            spdlog::info("Exported texture {} to {}", index, outputPath.string());
            return 0;
        }

        if (std::filesystem::is_regular_file(outputPath)) {
            spdlog::error("Output path must be a directory when exporting multiple textures");
            return 1;
        }

        std::filesystem::create_directories(outputPath);
        archive.exportTextures(outputPath);
    } else if (parser.is_subcommand_used("info")) {
        const auto& cmd = parser.at<argparse::ArgumentParser>("info");
        std::filesystem::path filePath(cmd.get<std::string>("path"));
        if (!SPLArchive::isValid(filePath)) {
            spdlog::error("Invalid SPL file: {}", filePath.string());
            return 1;
        }

        SPLArchive archive(filePath, /* createGpuTextures */ false);
        archive.printInfo(filePath.filename().string());
    } else {
        spdlog::error("No subcommand specified");
        return 1;
    }

    return 0;
}

void Application::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == SDL_GetWindowID(m_window)) {
                m_running = false;
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            saveConfig(); // Save the window size
            break;

        case SDL_EVENT_KEY_DOWN:
            handleKeydown(event);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            handleMouseDown(event);
            break;

        default:
            break;
        }

        dispatchEvent(event);
    }
}

void Application::handleKeydown(const SDL_Event& event) {
    const auto io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (m_listeningForInput) {
        if (m_modifierKeys.contains(event.key.key)) {
            return; // Ignore modifier keys
        }

        if (event.key.key == SDLK_ESCAPE) {
            m_listeningForInput = false;
            m_listeningKeybind = nullptr;
            m_exitKeybindListening = true;
            return;
        }

        m_listeningKeybind->type = KeybindType::Key;
        m_listeningKeybind->key = event.key.key;
        m_listeningKeybind->modifiers = Keybind::normalizeModifiers(event.key.mod);
        m_listeningForInput = false;
        m_listeningKeybind = nullptr;
        m_exitKeybindListening = true;
        return;
    }

    const SDL_Keycode evKey = event.key.key;
    const SDL_Keymod evMod = Keybind::normalizeModifiers(event.key.mod);

    // Exact match
    for (const auto& [action, keybind] : m_settings.keybinds) {
        if (keybind.type != KeybindType::Key) continue;
        const SDL_Keymod kbMod = Keybind::normalizeModifiers(keybind.modifiers);
        if (evKey == keybind.key && evMod == kbMod) {
            return executeAction(action);
        }
    }

    const auto bitCount = [](SDL_Keymod m) {
        int c = 0;
        if (m & SDL_KMOD_CTRL) ++c;
        if (m & SDL_KMOD_SHIFT) ++c;
        if (m & SDL_KMOD_ALT) ++c;
        if (m & SDL_KMOD_GUI) ++c;
        return c;
    };

    // Stupid heuristic for best match because I couldn't figure out a better way
    u32 bestAction = 0;
    int bestScore = -1;
    for (const auto& [action, keybind] : m_settings.keybinds) {
        if (keybind.type != KeybindType::Key || evKey != keybind.key) {
            continue;
        }

        const SDL_Keymod kbMod = Keybind::normalizeModifiers(keybind.modifiers);
        if ((evMod & kbMod) == kbMod) {
            const int score = bitCount(kbMod);
            if (score > bestScore) {
                bestScore = score;
                bestAction = action;
            }
        }
    }

    if (bestScore >= 0) {
        return executeAction(bestAction);
    }
}

void Application::handleMouseDown(const SDL_Event& event) {
    if (m_listeningForInput) {
        if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
            return; // Don't allow remapping left/right mouse buttons
        }

        m_listeningKeybind->type = KeybindType::Mouse;
        m_listeningKeybind->button = event.button.button;
        m_listeningForInput = false;
        m_listeningKeybind = nullptr;
        m_exitKeybindListening = true;
        return;
    }

    for (const auto& [action, keybind] : m_settings.keybinds) {
        if (keybind.type == KeybindType::Mouse) {
            if (event.button.button == keybind.button) {
                executeAction(action);
                return;
            }
        }
    }
}

void Application::dispatchEvent(const SDL_Event& event) {
    g_projectManager->handleEvent(event);
    m_editor->handleEvent(event);
}

void Application::renderMenuBar() {
    const bool hasProject = g_projectManager->hasProject();
    const bool hasActiveEditor = g_projectManager->hasActiveEditor();
    const bool hasOpenEditors = g_projectManager->hasOpenEditors();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItemIcon(ICON_FA_FOLDER_PLUS, "Project", "Ctrl+Shift+N", false, AppColors::DarkBeige)) {
                    spdlog::warn("New Project not implemented");
                }

                if (ImGui::MenuItemIcon(ICON_FA_FILE_CIRCLE_PLUS, "SPL File", "Ctrl+N")) {
                    g_projectManager->openEditor();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Open")) {
                if (ImGui::MenuItemIcon(ICON_FA_FOLDER_OPEN, "Project", KEYBINDSTR(OpenProject), false, AppColors::DarkBeige)) {
                    const auto path = openDirectory();
                    if (!path.empty()) {
                        addRecentProject(path);
                        g_projectManager->openProject(path);
                    }
                }

                if (ImGui::MenuItemIcon(ICON_FA_FILE, "SPL File", KEYBINDSTR(OpenFile))) {
                    const std::filesystem::path filePath = openFile();
                    tryOpenEditor(filePath);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Open Recent")) {
                ImGui::SeparatorText("Projects");
                if (m_recentProjects.empty()) {
                    ImGui::MenuItem("No Recent Projects", nullptr, false, false);
                }

                std::string toOpen;
                for (const auto& path : m_recentProjects) {
                    if (ImGui::MenuItem(path.c_str())) {
                        toOpen = path;
                    }
                }

                if (!toOpen.empty()) {
                    addRecentProject(toOpen);
                    g_projectManager->openProject(toOpen);
                }

                ImGui::SeparatorText("Files");
                if (m_recentFiles.empty()) {
                    ImGui::MenuItem("No Recent Files", nullptr, false, false);
                }

                toOpen.clear();
                for (const auto& path : m_recentFiles) {
                    if (ImGui::MenuItem(path.c_str())) {
                        toOpen = path;
                    }
                }

                if (!toOpen.empty()) {
                    tryOpenEditor(toOpen);
                }

                ImGui::EndMenu();
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save", KEYBINDSTR(Save), false, AppColors::LightBlue, hasActiveEditor)) {
                m_editor->save();
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save As...", nullptr, false, AppColors::LightBlue, hasActiveEditor)) {
                const auto path = saveFile();
                if (!path.empty()) {
                    m_editor->saveAs(path);
                    addRecentFile(path);
                }
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save All", KEYBINDSTR(SaveAll), false, AppColors::LightBlue, hasOpenEditors)) {
                g_projectManager->saveAllEditors();
            }

            if (ImGui::MenuItemIcon(ICON_FA_XMARK, "Close", KEYBINDSTR(Close), false, 0, hasActiveEditor)) {
                g_projectManager->closeEditor(g_projectManager->getActiveEditor());
            }

            if (ImGui::MenuItemIcon(ICON_FA_XMARK, "Close All", KEYBINDSTR(CloseAll), false, 0, hasOpenEditors)) {
                g_projectManager->closeAllEditors();
            }

            if (ImGui::MenuItemIcon(ICON_FA_XMARK, "Close Project", nullptr, false, 0, hasProject)) {
                g_projectManager->closeProject();
            }

            if (ImGui::MenuItemIcon(ICON_FA_RIGHT_FROM_BRACKET, "Exit", KEYBINDSTR(Exit))) {
                m_running = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItemIcon(ICON_FA_ROTATE_LEFT, "Undo", KEYBINDSTR(Undo), false, 0, m_editor->canUndo())) {
                m_editor->undo();
            }

            if (ImGui::MenuItemIcon(ICON_FA_ROTATE_RIGHT, "Redo", KEYBINDSTR(Redo), false, 0, m_editor->canRedo())) {
                m_editor->redo();
            }

            if (ImGui::MenuItemIcon(ICON_FA_PLAY, "Play Emitter", KEYBINDSTR(PlayEmitter), false, AppColors::LightGreen, hasActiveEditor)) {
                m_editor->playEmitter(EmitterSpawnType::SingleShot);
            }

            if (ImGui::MenuItemIcon(ICON_FA_REPEAT, "Play Looped Emitter", KEYBINDSTR(PlayEmitterLooped), false, AppColors::LightGreen2, hasActiveEditor)) {
                m_editor->playEmitter(EmitterSpawnType::Looped);
            }

            if (ImGui::MenuItemIcon(ICON_FA_PLAY, "Play All Emitters", KEYBINDSTR(PlayAllEmitters), false, AppColors::LightGreen, hasActiveEditor)) {
                m_editor->playAllEmitters(EmitterSpawnType::SingleShot);
            }

            if (ImGui::MenuItemIcon(ICON_FA_STOP, "Kill Emitters", KEYBINDSTR(KillEmitters), false, AppColors::LightRed, hasActiveEditor)) {
                m_editor->killEmitters();
            }

            if (ImGui::MenuItemIcon(ICON_FA_CAMERA_ROTATE, "Reset Camera", KEYBINDSTR(ResetCamera), false, 0, hasActiveEditor)) {
                m_editor->resetCamera();
            }

            if (ImGui::MenuItemIcon(ICON_FA_WRENCH, "Preferences")) {
                m_preferencesOpen = true;
                m_uiScaleChanged = false;

                m_prefButtonsClicked.reset();

                ImGui::PushOverrideID(m_preferencesWindowId);
                ImGui::OpenPopup("Preferences##Application");
                ImGui::PopID();
            }

            m_editor->renderMenu("Edit");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItemIcon(ICON_FA_FOLDER_TREE, "Project Manager")) {
                g_projectManager->open();
            }

            if (ImGui::MenuItemIcon(ICON_FA_WRENCH, "Resource Picker")) {
                m_editor->openPicker();
            }

            if (ImGui::MenuItemIcon(ICON_FA_IMAGES, "Texture Manager")) {
                m_editor->openTextureManager();
            }

            if (ImGui::MenuItemIcon(ICON_FA_SLIDERS, "Resource Editor")) {
                m_editor->openEditor();
            }

            ImGui::MenuItemIcon(ICON_FA_GAUGE, "Performance", nullptr, &m_performanceWindowOpen);

            ImGui::Separator();

            m_editor->renderMenu("View");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            m_editor->renderMenu("Help");

            if (ImGui::MenuItemIcon(ICON_FA_CODE_BRANCH, "GitHub Repository", nullptr, false, AppColors::Yellow)) {
                SDL_OpenURL("https://github.com/Fexty12573/nitroefx");
            }

            if (ImGui::MenuItemIcon(ICON_FA_BUG, "Report Issue", nullptr, false, AppColors::Red)) {
                SDL_OpenURL("https://github.com/Fexty12573/nitroefx/issues/new");
            }

            if (ImGui::MenuItemIcon(ICON_FA_CIRCLE_INFO, "About NitroEFX", nullptr, false, AppColors::LightBlue2)) {
                ImGui::PushOverrideID(m_aboutWindowId);
                ImGui::OpenPopup("About NitroEFX");
                ImGui::PopID();

                m_aboutWindowOpen = true;
            }

            ImGui::EndMenu();
        }

        if (m_versionCheckResult.updateAvailable) {
            if (ImGui::IconButton(ICON_FA_ARROW_UP, "Update Available", AppColors::Turquoise)) {
                ImGui::PushOverrideID(m_updateWindowId);
                ImGui::OpenPopup("Update Available");
                ImGui::PopID();
            }
        }

        ImGui::EndMainMenuBar();
    }

    const auto viewport = (ImGuiViewportP*)ImGui::GetMainViewport();
    constexpr auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    const float framePaddingY = 4.0f * m_settings.uiScale;
    const float itemHeight = 24.0f * m_settings.uiScale;
    const float barHeight = itemHeight + 2.0f;
    const ImVec2 size = { itemHeight, itemHeight };

    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AppColors::DarkGray);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AppColors::DarkGray2);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.5f, 0.5f });
    ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 4.0f); // Cut item spacing in half
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 2.0f, framePaddingY });
    
    if (ImGui::BeginViewportSideBar("##SecondaryMenuBar", viewport, ImGuiDir_Up, barHeight, flags)) {
        if (ImGui::BeginMenuBar()) {

            if (m_settings.toolbarCentered && m_lastToolbarWidth > 0.0f) {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float offset = (avail - m_lastToolbarWidth) * 0.5f;
                if (offset > 0.0f) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
                }
            }

            float startX = ImGui::GetCursorPosX();

            if (ImGui::IconButton(ICON_FA_FILE, size)) {
                const auto file = openFile();
                if (!file.empty()) {
                    addRecentFile(file);
                    g_projectManager->openEditor(file);
                }
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Open SPL File");
                ImGui::EndTooltip();
            }

            if (ImGui::IconButton(ICON_FA_FOLDER_OPEN, size, AppColors::DarkBeige)) {
                const auto project = openDirectory();
                if (!project.empty()) {
                    addRecentProject(project);
                    g_projectManager->openProject(project);
                }
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Open Project");
                ImGui::EndTooltip();
            }

            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_FLOPPY_DISK, size, AppColors::LightBlue, hasActiveEditor)) {
                m_editor->save();
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Save");
                ImGui::EndTooltip();
            }

            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_ROTATE_LEFT, size, 0, m_editor->canUndo())) {
                m_editor->undo();
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Undo");
                ImGui::EndTooltip();
            }

            if (ImGui::IconButton(ICON_FA_ROTATE_RIGHT, size, 0, m_editor->canRedo())) {
                m_editor->redo();
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Redo");
                ImGui::EndTooltip();
            }
            
            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_PLAY, size, AppColors::LightGreen, hasActiveEditor)) {
                m_editor->playEmitter(EmitterSpawnType::SingleShot);
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Play Emitter");
                ImGui::EndTooltip();
            }

            if (ImGui::IconButton(ICON_FA_REPEAT, size, AppColors::LightGreen2, hasActiveEditor)) {
                m_editor->playEmitter(EmitterSpawnType::Looped);
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Play Looped Emitter");
                ImGui::EndTooltip();
            }

            if (ImGui::IconButton(ICON_FA_STOP, size, AppColors::LightRed, hasActiveEditor)) {
                m_editor->killEmitters();
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Kill Emitters");
                ImGui::EndTooltip();
            }

            if (ImGui::IconButton(ICON_FA_CAMERA_ROTATE, size, 0, hasActiveEditor)) {
                m_editor->resetCamera();
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Reset Camera");
                ImGui::EndTooltip();
            }

            m_editor->renderToolbar(itemHeight);

            m_lastToolbarWidth = ImGui::GetCursorPosX() - startX;

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

void Application::renderPreferences() {
    ImGui::PushOverrideID(m_preferencesWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    const auto maybeDisabledButton = [](const char* label, bool disabled) {
        ImGui::BeginDisabled(disabled);
        const bool clicked = ImGui::Button(label);
        ImGui::EndDisabled();

        return clicked;
    };

    bool wasOpen = m_preferencesOpen;
    if (ImGui::BeginPopupModal("Preferences##Application", &m_preferencesOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("##preferences_layout", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();

            // Left: Updates, Interface, Indexing, Clear...
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText("Updates");
            ImGui::Checkbox("Check for updates on startup", &m_settings.checkForUpdates);
            ImGui::Checkbox("Include pre-release versions", &m_settings.showReleaseCandidates);

            ImGui::Spacing();
            ImGui::SeparatorText("Interface");
            m_uiScaleChanged |= ImGui::SliderFloat("UI Scale", &m_settings.uiScale, 0.5f, 3.0f, "%.1fx");
            ImGui::Checkbox("Center Toolbar", &m_settings.toolbarCentered);

            ImGui::Spacing();
            ImGui::SeparatorText("Indexing");
            ImGui::InputText("Ignored Directories", &m_indexIgnoresStr);
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("';'-separated list of directory names to ignore when indexing a project.");
                ImGui::Text("Example: 'build;temp;cache'");
                ImGui::Text("Clearing cache after changing this is recommended.");
                ImGui::EndTooltip();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Clear...");
            if (maybeDisabledButton("Cache", m_prefButtonsClicked.test(PrefButton::Cache))) {
                clearCache();
                m_prefButtonsClicked.set(PrefButton::Cache);
            }

            ImGui::SameLine();
            if (maybeDisabledButton("Temporary Files", m_prefButtonsClicked.test(PrefButton::TempFiles))) {
                clearTempDir();
                m_prefButtonsClicked.set(PrefButton::TempFiles);
            }

            if (maybeDisabledButton("Recent Projects", m_prefButtonsClicked.test(PrefButton::ClearRecentProjects))) {
                m_recentProjects.clear();
                m_prefButtonsClicked.set(PrefButton::ClearRecentProjects);
            }

            ImGui::SameLine();
            if (maybeDisabledButton("Recent Files", m_prefButtonsClicked.test(PrefButton::ClearRecentFiles))) {
                m_recentFiles.clear();
                m_prefButtonsClicked.set(PrefButton::ClearRecentFiles);
            }

            // Right: Clear... and Keybinds
            ImGui::TableSetColumnIndex(1);
            ImGui::SeparatorText("Keybinds");

            if (ImGui::BeginTable("Keybinds##Application", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Keybind", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                
                for (auto action : m_sortedActions) {
                    auto& keybind = m_settings.keybinds[action];

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(ApplicationAction::Names.at(action));
                    
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    constexpr auto flags = ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_NoAutoClosePopups;
                    if (ImGui::Selectable(keybind.toString().c_str(), false, flags)) {
                        m_listeningForInput = true;
                        m_listeningKeybind = &keybind;
                        ImGui::OpenPopup("Keybind##Application");
                    }
                }

                if (m_listeningForInput) {
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
                    // Place the popup in the center of the window
                    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                    ImGui::SetNextWindowSize({ 350, 200 }, ImGuiCond_Always);

                    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
                    if (ImGui::BeginPopupModal("Keybind##Application", nullptr, flags)) {
                        const auto drawList = ImGui::GetWindowDrawList();

                        const auto windowPos = ImGui::GetWindowPos();
                        const auto windowSize = ImGui::GetWindowSize();
                        const auto textSize = ImGui::CalcTextSize("Press any key or button to bind");

                        const auto textPos = windowPos + ImVec2((windowSize.x - textSize.x) / 2, (windowSize.y - textSize.y) / 2);
                        drawList->AddText(
                            textPos, 
                            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 
                            "Press any key or button to bind"
                        );

                        if (m_exitKeybindListening) {
                            ImGui::CloseCurrentPopup();
                            m_exitKeybindListening = false;
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopStyleVar();
                }

                ImGui::EndTable();
            }

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
    
    ImGui::PopStyleVar(2);
    ImGui::PopID();

    if (wasOpen && !m_preferencesOpen) {
        // Preferences window was just closed
        if (m_uiScaleChanged) {
            ImGui::OpenPopup("Restart Required##Application");
            m_uiScaleChanged = false;
        }

        using std::operator""s;

        m_settings.indexIgnores.clear();
        for (const auto dir : std::views::split(m_indexIgnoresStr, ';')) {
            m_settings.indexIgnores.emplace_back(std::string_view(dir));
        }
    }
}

void Application::renderPerformanceWindow() {
    if (ImGui::Begin("Performance", &m_performanceWindowOpen)) {
        ImGui::SeparatorText("Application");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Delta Time: %.3f ms", m_deltaTime * 1000.0f);
        ImGui::Text("Frame Time: %.3f ms", ImGui::GetIO().DeltaTime * 1000.0f);

        ImGui::SeparatorText("Current Editor");
        m_editor->renderStats();
    }

    ImGui::End();
}

void Application::renderAboutWindow() {
    ImGui::PushOverrideID(m_aboutWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    if (ImGui::BeginPopupModal("About NitroEFX", &m_aboutWindowOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto windowSize = ImGui::GetWindowSize();
        if (m_icon) {
            constexpr auto iconSize = 128.0f;
            ImGui::SetCursorPosX((windowSize.x - iconSize) * 0.5f);
            ImGui::Image(m_icon->getHandle(), { iconSize, iconSize });
        }

        ImGui::PushFont(getFont("Large"));

        const auto appStr = fmt::format("NitroEFX {}", Application::VERSION);
        const auto size = ImGui::CalcTextSize(appStr.c_str());
        ImGui::SetCursorPosX((windowSize.x - size.x) * 0.5f);

        ImGui::TextUnformatted(appStr.c_str());

        ImGui::PopFont();

        ImGui::Separator();
        ImGui::Text("A particle editor for the Nintendo DS Pokémon games.");
        ImGui::Text("Created by Fexty12573");
        ImGui::TextLinkOpenURL("https://github.com/Fexty12573/nitroefx");

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void Application::renderUpdateWindow() {
    ImGui::PushOverrideID(m_updateWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    if (ImGui::BeginPopup("Update Available", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("A new version of NitroEFX is available!");
        ImGui::Text("Current version: %s", Application::VERSION);
        ImGui::Text("Latest version: %s", m_versionCheckResult.remoteTag.c_str());

        ImGui::Separator();

        if (ImGui::IconButton(ICON_FA_DOWNLOAD, "Update Now", AppColors::Turquoise, !g_projectManager->hasUnsavedEditors())) {
            ImGui::CloseCurrentPopup();
            m_versionCheckResult.updateAvailable = false;

            const auto archivePath = downloadLatestArchive();
            const auto binaryPath = extractLatestArchive(archivePath);
            applyUpdateNow(binaryPath, true);
        }

        if (g_projectManager->hasUnsavedEditors()) {
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("You have unsaved changes in your editors.");
                ImGui::Text("Please save or close them before updating.");
                ImGui::EndTooltip();
            }
        }

        ImGui::SameLine();

        if (ImGui::IconButton(ICON_FA_ARROW_RIGHT_FROM_BRACKET, "Update on Exit", AppColors::LightGreen)) {
            ImGui::CloseCurrentPopup();
            m_versionCheckResult.updateAvailable = false;
            m_updateOnClose = true;
        }

        if (ImGui::BeginItemTooltip()) {
            ImGui::Text("This will download and apply the update when you exit NitroEFX.");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        if (ImGui::IconButton(ICON_FA_CLOCK_ROTATE_LEFT, "Remind Me Later", AppColors::Yellow)) {
           ImGui::CloseCurrentPopup();
           m_versionCheckResult.updateAvailable = false;
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void Application::renderWelcomeWindow() {
    ImGui::PushOverrideID(m_welcomeWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    const auto popupPos = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing, { 0.5f, 0.5f });

    if (ImGui::BeginPopup("Welcome to NitroEFX", ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto windowSize = ImGui::GetWindowSize();
        if (m_icon) {
            constexpr auto iconSize = 96.0f;
            ImGui::SetCursorPosX((windowSize.x - iconSize) * 0.5f);
            ImGui::Image(m_icon->getHandle(), { iconSize, iconSize });
        }

        ImGui::PushFont(getFont("Large"));
        const auto appStr = fmt::format("NitroEFX {}", Application::VERSION);
        const auto size = ImGui::CalcTextSize(appStr.c_str());
        ImGui::SetCursorPosX((windowSize.x - size.x) * 0.5f);
        ImGui::TextUnformatted(appStr.c_str());
        ImGui::PopFont();

        ImGui::Separator();

        // Layout content in two columns
        if (ImGui::BeginTable("##welcome_layout", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            // Left: Quick Start + Recents
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            ImGui::SeparatorText("Get Started");
            if (ImGui::IconButton(ICON_FA_FILE_CIRCLE_PLUS, "New SPL File", AppColors::LightBlue)) {
                g_projectManager->openEditor();
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IconButton(ICON_FA_FILE, "Open SPL File", AppColors::LightGreen)) {
                const auto file = openFile();
                if (!file.empty()) {
                    addRecentFile(file);
                    tryOpenEditor(file);
                    ImGui::CloseCurrentPopup();
                }
            }

            if (ImGui::IconButton(ICON_FA_FOLDER_OPEN, "Open Project", AppColors::DarkBeige)) {
                const auto project = openDirectory();
                if (!project.empty()) {
                    addRecentProject(project);
                    g_projectManager->openProject(project);
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Recent Projects");
            if (m_recentProjects.empty()) {
                ImGui::TextDisabled("No recent projects");
            } else {
                int count = 0;
                std::string toOpen;
                for (const auto& path : m_recentProjects) {
                    if (count++ >= 5) break;
                    if (ImGui::Selectable(path.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        toOpen = path;
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!toOpen.empty()) {
                    addRecentProject(toOpen);
                    g_projectManager->openProject(toOpen);
                }
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Recent Files");
            if (m_recentFiles.empty()) {
                ImGui::TextDisabled("No recent files");
            } else {
                int count = 0;
                std::string toOpen;
                for (const auto& path : m_recentFiles) {
                    if (count++ >= 5) break;
                    if (ImGui::Selectable(path.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        toOpen = path;
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!toOpen.empty()) {
                    tryOpenEditor(toOpen);
                }
            }

            // Right: Resources + Shortcuts
            ImGui::TableSetColumnIndex(1);

            ImGui::SeparatorText("Resources");
            ImGui::TextLinkOpenURL("GitHub Repository", "https://github.com/Fexty12573/nitroefx");
            ImGui::TextLinkOpenURL("Report an Issue", "https://github.com/Fexty12573/nitroefx/issues/new");
            ImGui::TextLinkOpenURL("Latest Releases", "https://github.com/Fexty12573/nitroefx/releases");

            ImGui::Spacing();
            ImGui::SeparatorText("Shortcuts");
            if (ImGui::BeginTable("##welcome_shortcuts", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("New File");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(NewFile));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Open Project");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(OpenProject));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Open File");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(OpenFile));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Save");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(Save));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Save All");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(SaveAll));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Play Emitter");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(PlayEmitter));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Kill Emitters");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(KillEmitters));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Reset Camera");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(KEYBINDSTR(ResetCamera));

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Info");
            if (m_versionCheckResult.updateAvailable) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(AppColors::Turquoise),
                    ICON_FA_ARROW_UP " Update available: %s", m_versionCheckResult.remoteTag.c_str());
            } else {
                ImGui::Text(ICON_FA_CIRCLE_CHECK " You are up-to-date");
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        constexpr auto buttonWidth = 100.0f;
        ImGui::SetCursorPosX((windowSize.x - buttonWidth) * 0.5f);
        if (ImGui::Button("Close", { buttonWidth, 0.0f })) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void Application::renderRestartPopup() {
    if (ImGui::BeginPopupModal("Restart Required##Application", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Changing the UI scale requires a restart to take effect.");
        ImGui::Separator();

        if (ImGui::Button("Restart Now")) {
            saveConfig();
            restart();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Later")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::setColors() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowMinSize = ImVec2(32.0f, 32.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 2.0f;
    style.PopupRounding = 2.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(11.0f, 4.0f);
    style.FrameRounding = 3.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 16.0f;
    style.ScrollbarRounding = 2.4f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 2.2f;
    style.TabRounding = 2.0f;
    style.TabBorderSize = 0.0f;
    style.TabCloseButtonMinWidthSelected = 0.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                       = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
    colors[ImGuiCol_TextDisabled]               = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]                   = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_ChildBg]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_Border]                     = ImVec4(0.33f, 0.33f, 0.33f, 0.45f);
    colors[ImGuiCol_BorderShadow]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                    = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]             = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]              = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]                    = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive]              = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]           = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_MenuBarBg]                  = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]                = ImVec4(0.12f, 0.12f, 0.13f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]              = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]       = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]        = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]                  = ImVec4(0.52f, 0.36f, 0.67f, 1.00f);
    colors[ImGuiCol_SliderGrab]                 = ImVec4(0.52f, 0.36f, 0.67f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]           = ImVec4(0.58f, 0.29f, 0.85f, 1.00f);
    colors[ImGuiCol_Button]                     = ImVec4(0.31f, 0.31f, 0.31f, 0.55f);
    colors[ImGuiCol_ButtonHovered]              = ImVec4(0.33f, 0.33f, 0.33f, 0.65f);
    colors[ImGuiCol_ButtonActive]               = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_Header]                     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_HeaderHovered]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderActive]               = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_Separator]                  = ImVec4(0.50f, 0.50f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]           = ImVec4(0.33f, 0.33f, 0.33f, 0.78f);
    colors[ImGuiCol_SeparatorActive]            = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ResizeGrip]                 = ImVec4(0.44f, 0.44f, 0.44f, 0.09f);
    colors[ImGuiCol_ResizeGripHovered]          = ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]           = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_TabHovered]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_Tab]                        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TabSelected]                = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_TabDimmed]                  = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_DockingPreview]             = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]                  = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]           = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]              = ImVec4(0.58f, 0.13f, 0.82f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]       = ImVec4(0.67f, 0.21f, 0.93f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]              = ImVec4(0.14f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]          = ImVec4(0.31f, 0.31f, 0.35f, 0.50f);
    colors[ImGuiCol_TableBorderLight]           = ImVec4(0.23f, 0.23f, 0.25f, 0.50f);
    colors[ImGuiCol_TableRowBg]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]              = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink]                   = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]             = ImVec4(0.26f, 0.98f, 0.91f, 0.35f);
    colors[ImGuiCol_DragDropTarget]             = ImVec4(0.52f, 0.37f, 0.67f, 0.90f);
    colors[ImGuiCol_NavCursor]                  = ImVec4(0.67f, 0.67f, 0.67f, 0.84f);
    colors[ImGuiCol_NavWindowingHighlight]      = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]          = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
}

#include "fonts/tahoma_font.h"
#include "fonts/tahoma_italic_font.h"
#include "fonts/icon_font.h"

void Application::loadFonts() {
    const ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = true;
    config.FontDataOwnedByAtlas = false;

    io.Fonts->AddFontFromMemoryCompressedTTF(
        g_tahoma_compressed_data, 
        g_tahoma_compressed_size, 
        18.0f * m_settings.uiScale, 
        &config
    );

    config.MergeMode = true;
    constexpr ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        g_icon_font_compressed_data, 
        g_icon_font_compressed_size, 
        18.0f * m_settings.uiScale, 
        &config,
        iconRanges
    );

    config.MergeMode = false;

    m_fonts["Italic"] = io.Fonts->AddFontFromMemoryCompressedTTF(
        g_tahoma_italic_compressed_data,
        g_tahoma_italic_compressed_size,
        18.0f * m_settings.uiScale,
        &config
    );

    m_fonts["Large"] = io.Fonts->AddFontFromMemoryCompressedTTF(
        g_tahoma_compressed_data,
        g_tahoma_compressed_size,
        24.0f * m_settings.uiScale,
        &config
    );

    io.Fonts->Build();
}

void Application::loadConfig() {
    const auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) {
        spdlog::info("Config path does not exist, creating: {}", configPath.string());
        std::filesystem::create_directories(configPath);
    }

    const auto configFile = configPath / "config.json";
    if (!std::filesystem::exists(configFile)) {
        spdlog::info("Config file does not exist, creating: {}", configFile.string());
        nlohmann::json config;
        config["recentFiles"] = nlohmann::json::array();
        config["recentProjects"] = nlohmann::json::array();
        std::ofstream outFile(configFile);

        outFile << config.dump(4);
    }

    std::ifstream inFile(configFile);
    if (!inFile) {
        spdlog::error("Failed to open config file: {}", configFile.string());
        return;
    }

    nlohmann::json config = nlohmann::json::object();
    try {
        inFile >> config;
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("Failed to parse config file: {}", e.what());
        return;
    }

    for (const auto& file : config["recentFiles"]) {
        m_recentFiles.push_back(file.get<std::string>());
    }

    for (const auto& project : config["recentProjects"]) {
        m_recentProjects.push_back(project.get<std::string>());
    }
    
    if (config.contains("keybinds")) {
        for (const auto& keybind : config["keybinds"]) {
            Keybind bind{};
            bind.type = static_cast<KeybindType>(keybind["type"].get<int>());
            if (bind.type == KeybindType::Key) {
                bind.key = keybind.value<SDL_Keycode>("key", SDLK_UNKNOWN);
                bind.modifiers = keybind.value<SDL_Keymod>("modifiers", 0);
            }
            else if (bind.type == KeybindType::Mouse) {
                bind.button = keybind.value<Uint8>("button", SDL_BUTTON_X1);
            }

            m_settings.keybinds[keybind["id"].get<u32>()] = bind;
        }
    }

    if (config.contains("windowPos")) {
        const auto& pos = config["windowPos"];
        SDL_SetWindowPosition(
            m_window,
            pos.value<int>("x", SDL_WINDOWPOS_CENTERED),
            pos.value<int>("y", SDL_WINDOWPOS_CENTERED)
        );
    }

    if (config.contains("windowSize")) {
        const auto& size = config["windowSize"];
        if (size.value("maximized", false)) {
            SDL_MaximizeWindow(m_window);
        } else {
            SDL_SetWindowSize(m_window, size["w"].get<int>(), size["h"].get<int>());
        }
    }

    m_settings.checkForUpdates = config.value("checkForUpdates", m_settings.checkForUpdates);
    m_settings.showReleaseCandidates = config.value("showReleaseCandidates", m_settings.showReleaseCandidates);
    m_settings.uiScale = config.value("uiScale", m_settings.uiScale);

    if (config.contains("indexIgnores") && config["indexIgnores"].is_array()) {
        for (const auto& ignore : config["indexIgnores"]) {
            m_settings.indexIgnores.push_back(ignore.get<std::string>());
        }
    }

    m_indexIgnoresStr = fmt::format("{}", fmt::join(m_settings.indexIgnores, ";"));

    m_settings.toolbarCentered = config.value("toolbarCentered", m_settings.toolbarCentered);
    
    m_editor->loadConfig(config);
}

void Application::loadIcon() {
    const auto iconFile = b::embed<"data/nitroefx.png">().vec();

    int w, h, c;
    const auto rgba = stbi_load_from_memory(iconFile.data(), (int)iconFile.size(), &w, &h, &c, 4);
    if (rgba) {
        m_icon = std::make_shared<GLTexture>(w, h, rgba);
    } else {
        spdlog::error("Failed to load icon");
    }
}

void Application::clearTempDir() {
    spdlog::info("Clearing temporary directory...");

    const auto tempPath = getTempPath();
    if (!std::filesystem::exists(tempPath)) {
        spdlog::info("Temp path does not exist, creating: {}", tempPath.string());
        std::filesystem::create_directories(tempPath);

        return;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(tempPath)) {
        std::filesystem::remove_all(entry.path());
    }
}

void Application::executeAction(u32 action) {
    spdlog::info("Executing Action: {}", ApplicationAction::Names.at(action));

    switch (action) {
    case ApplicationAction::NewFile:
        g_projectManager->openEditor();
        break;
    case ApplicationAction::OpenProject: {
        const auto projectPath = openDirectory();
        if (!projectPath.empty()) {
            addRecentProject(projectPath);
            g_projectManager->openProject(projectPath);
        }
    } break;
    case ApplicationAction::OpenFile: {
        const std::filesystem::path filePath = openFile();
        tryOpenEditor(filePath);
    } break;
    case ApplicationAction::Save:
        m_editor->save();
        break;
    case ApplicationAction::SaveAll:
        g_projectManager->saveAllEditors();
        break;
    case ApplicationAction::Close:
        if (g_projectManager->hasActiveEditor()) {
            g_projectManager->closeEditor(g_projectManager->getActiveEditor());
        }
        break;
    case ApplicationAction::CloseAll:
        if (g_projectManager->hasOpenEditors()) {
            g_projectManager->closeAllEditors();
        }
        break;
    case ApplicationAction::Undo:
        if (g_projectManager->hasActiveEditor()) {
            g_projectManager->getActiveEditor()->undo();
        }
        break;
    case ApplicationAction::Redo:
        if (g_projectManager->hasActiveEditor()) {
            g_projectManager->getActiveEditor()->redo();
        }
        break;
    case ApplicationAction::Exit:
        m_running = false;
        break;
    case ApplicationAction::PlayEmitter:
        m_editor->playEmitter(EmitterSpawnType::SingleShot);
        break;
    case ApplicationAction::PlayEmitterLooped:
        m_editor->playEmitter(EmitterSpawnType::Looped);
        break;
    case ApplicationAction::PlayAllEmitters:
        m_editor->playAllEmitters(EmitterSpawnType::SingleShot);
        break;
    case ApplicationAction::KillEmitters:
        m_editor->killEmitters();
        break;
    case ApplicationAction::ResetCamera:
        m_editor->resetCamera();
        break;
    case ApplicationAction::QuickOpen:
        g_projectManager->openFileSearch();
        break;
    default:
        spdlog::warn("Unhandled action: {}", action);
        break;
    }
}

void Application::checkArgs(int argc, char** argv) {
    if (argc > 1) {
        const std::filesystem::path arg = argv[1];
        if (std::filesystem::is_directory(arg)) {
            g_projectManager->openProject(arg);
        }
        else if (arg.extension() == ".spa") {
            g_projectManager->openEditor(arg);
        }
        else {
            spdlog::warn("Invalid argument: {}", arg.string());
        }
    }
}

void Application::clearCache() {
    spdlog::info("Clearing cache directory...");
    const auto cachePath = getCachePath();
    if (!std::filesystem::exists(cachePath)) {
        spdlog::info("Cache path does not exist, creating: {}", cachePath.string());
        std::filesystem::create_directories(cachePath);
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(cachePath)) {
        std::filesystem::remove_all(entry.path());
    }
}

void Application::restart() {
    const auto exePath = getExecutablePath();
#ifdef _WIN32
    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, exePath.wstring().data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) == 0) {
        spdlog::error("Failed to restart application: {}", GetLastError());
    }
#else
    auto exePathString = exePath.string();
    char* const argv[] = { exePathString.data(), nullptr };
    ::execv(exePathString.c_str(), argv);
#endif

    m_running = false;
}

std::optional<AppVersion> Application::parseVersion(const std::string& versionStr) {
    static const std::regex re(R"(^v(\d+)\.(\d+)\.(\d+)(?:-rc(\d+))?$)");
    std::smatch m;

    if (!std::regex_match(versionStr, m, re)) {
        spdlog::error("Invalid version format: {}", versionStr);
        return std::nullopt;
    }

    AppVersion version;
    version.major = std::stoi(m[1].str());
    version.minor = std::stoi(m[2].str());
    version.patch = std::stoi(m[3].str());
    version.isRC = m[4].matched;
    version.rc = version.isRC ? std::stoi(m[4].str()) : 0;
    version.str = versionStr;

    return version;
}

int Application::update(const std::filesystem::path& srcPath, const std::filesystem::path& dstPath, unsigned long pid, bool relaunch) {
#ifdef _WIN32
    if (!std::filesystem::exists(dstPath)) {
        spdlog::error("Destination path does not exist: {}", dstPath.string());
        return 1;
    }

    const HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
        WaitForSingleObject(h, 60'000);
        CloseHandle(h);
    } else {
        Sleep(1000);
    }

    const auto srcPathStr = srcPath.string();
    const auto dstPathStr = dstPath.string();

    int attempt;
    for (attempt = 0; attempt < 20; attempt++) {
        if (MoveFileExA(srcPathStr.c_str(), dstPathStr.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED)) {
            spdlog::info("Successfully moved update file from {} to {}", srcPathStr, dstPathStr);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (attempt == 20) {
        spdlog::error("Failed to move update file after 20 attempts: {}", GetLastError());
        return 1;
    }

    if (relaunch) {
        STARTUPINFO si{};
        si.cb = sizeof(STARTUPINFO);

        PROCESS_INFORMATION pi{};

        auto cmd = fmt::format("\"{}\"", dstPath.string());
        CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
#endif

    return 0;
}

void Application::saveConfig() {
    const auto configPath = getConfigPath();
    if (!std::filesystem::exists(configPath)) {
        spdlog::info("Config path does not exist, creating: {}", configPath.string());
        std::filesystem::create_directories(configPath);
    }

    const auto configFile = configPath / "config.json";
    nlohmann::json config;

    for (const auto& file : m_recentFiles) {
        config["recentFiles"].push_back(file);
    }

    for (const auto& project : m_recentProjects) {
        config["recentProjects"].push_back(project);
    }

    auto keybinds = nlohmann::json::array();
    for (const auto& keybind : m_settings.keybinds) {
        auto b = nlohmann::json::object();
        b["id"] = keybind.first;
        b["type"] = keybind.second.type;
        if (keybind.second.type == KeybindType::Key) {
            b["key"] = keybind.second.key;
            b["modifiers"] = keybind.second.modifiers;
        } else if (keybind.second.type == KeybindType::Mouse) {
            b["button"] = keybind.second.button;
        }

        keybinds.push_back(b);
    }

    config["keybinds"] = keybinds;

    int x, y;
    SDL_GetWindowPosition(m_window, &x, &y);
    config["windowPos"] = {
        { "x", x },
        { "y", y }
    };

    int width, height;
    SDL_GetWindowSize(m_window, &width, &height);
    config["windowSize"] = {
        { "w", width },
        { "h", height },
        { "maximized", !!(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) }
    };

    config["checkForUpdates"] = m_settings.checkForUpdates;
    config["showReleaseCandidates"] = m_settings.showReleaseCandidates;
    config["uiScale"] = m_settings.uiScale;

    config["indexIgnores"] = nlohmann::json::array();
    for (const auto& ignore : m_settings.indexIgnores) {
        config["indexIgnores"].push_back(ignore);
    }

    config["toolbarCentered"] = m_settings.toolbarCentered;

    m_editor->saveConfig(config);

    std::ofstream outFile(configFile);
    if (!outFile) {
        spdlog::error("Failed to open config file for writing: {}", configFile.string());
        return;
    }
    
    try {
        outFile << config.dump(4);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to write config file: {}", e.what());
        return;
    }
}

ImFont* Application::getFont(const std::string& name) {
    const auto it = m_fonts.find(name);
    return it != m_fonts.end() ? it->second : nullptr;
}

std::optional<Keybind> Application::getKeybind(u32 action) const {
    if (!m_settings.keybinds.contains(action)) {
        return std::nullopt;
    }

    return m_settings.keybinds.at(action);
}

std::optional<Keybind> Application::getKeybind(const std::string_view& name) const {
    return getKeybind(crc::crc32(name.data(), name.size()));
}

void Application::addRecentFile(const std::string& path) {
    if (m_recentFiles.size() >= 10) {
        m_recentFiles.pop_back();
    }

    const auto existing = std::ranges::find(m_recentFiles, path);
    if (existing != m_recentFiles.end()) {
        m_recentFiles.erase(existing);
    }

    m_recentFiles.push_front(path);

    saveConfig();
}

void Application::addRecentProject(const std::string& path) {
    if (m_recentProjects.size() >= 10) {
        m_recentProjects.pop_back();
    }

    const auto existing = std::ranges::find(m_recentProjects, path);
    if (existing != m_recentProjects.end()) {
        m_recentProjects.erase(existing);
    }

    m_recentProjects.push_front(path);

    saveConfig();
}

void Application::tryOpenEditor(const std::filesystem::path& path) {
    if (!path.empty()) {
        addRecentFile(path.string());
        const auto extension = path.extension();
        if (SPLArchive::isValid(path)) {
            g_projectManager->openEditor(path);
        } else if (extension == ".narc" || extension == ".arc") {
            g_projectManager->openNarcProject(path);
        }
    }
}

std::filesystem::path Application::getConfigPath() {
#ifdef _WIN32
    char* buffer;
    size_t size;
    if (_dupenv_s(&buffer, &size, "APPDATA") != 0 || !buffer) {
        spdlog::error("Failed to get APPDATA environment variable");
        return {};
    }

    std::filesystem::path configPath = std::filesystem::path(buffer) / "nitroefx";
    free(buffer);

    return configPath;
#else
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig) {
        return std::filesystem::path(xdgConfig) / "nitroefx";
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::filesystem::path(home) / ".config" / "nitroefx";
        } else {
            spdlog::error("Failed to get XDG_CONFIG_HOME or HOME environment variable");
            return {};
        }
    }
#endif
}

std::filesystem::path Application::getTempPath() {
    return std::filesystem::temp_directory_path() / "nitroefx";
}

std::filesystem::path Application::getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buffer, MAX_PATH) == 0) {
        spdlog::error("Failed to get executable path");
        return {};
    }

    return std::filesystem::path(buffer);
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        spdlog::error("Failed to get executable path");
        return {};
    }

    buf[len] = '\0';
    return std::filesystem::path(buf);
#endif
}

std::filesystem::path Application::getCachePath() {
    return getConfigPath() / "cache";
}

std::string Application::openFile() {
    const char* filters[] = { "*.spa", "*.bin", "*. APS", "*._APS", "*.APS", "*.narc" };
    const char* result = tinyfd_openFileDialog(
        "Open File", 
        "", 
        6, 
        filters, 
        "SPL/NARC Files", 
        false
    );
    
    return result ? result : "";
}

std::string Application::saveFile(const std::string& default_path) {
    const char* filters[] = { "*.spa" };
    const char* result = tinyfd_saveFileDialog(
        "Save File",
        default_path.c_str(),
        1,
        filters,
        "SPL Files"
    );

    return result ? result : "";
}

std::string Application::openDirectory(const char* title) {
#ifdef _WIN32
#ifdef _DEBUG
#define HRESULT_CHECK(hr) if (FAILED(hr)) { spdlog::error("HRESULT failed @ {}:{}", __FILE__, __LINE__); return ""; }
#else
#define HRESULT_CHECK(hr) if (FAILED(hr)) { spdlog::error("operation failed: {}", hr); return ""; }
#endif

    // Implementing this manually because tinyfiledialog uses the old SHBrowseForFolder API (which sucks)
    IFileOpenDialog* dlg;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        spdlog::error("Failed to initialize COM");
        return "";
    }

    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, 
        IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg)))) {
        spdlog::error("Failed to create file open dialog");
        return "";
    }

    HRESULT_CHECK(dlg->SetTitle(title ? tinyfd_utf8to16(title) : L"Open Project"))
    HRESULT_CHECK(dlg->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST))
    if (dlg->Show(nullptr) != S_OK) {
        spdlog::info("User cancelled dialog");
        return "";
    }

    IShellItem* item;
    HRESULT_CHECK(dlg->GetResult(&item))
    PWSTR path;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        spdlog::error("Failed to get path");
        return "";
    }

    return tinyfd_utf16to8(path);
#else
    const auto folder = tinyfd_selectFolderDialog(title ? title : "Open Project", nullptr);
    return folder ? folder : "";
#endif
}

void Application::initDefaultDockingLayout() {
    if (m_layoutInitialized) return;

    // If an ini file exists, we don't override the user's layout
    const char* ini_filename = ImGui::GetIO().IniFilename;
    if (ini_filename && std::filesystem::exists(ini_filename)) {
        m_layoutInitialized = true;
        return;
    }

    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

    ImGui::DockBuilderRemoveNode(dockspace_id); // Clear existing layout
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    // Split the dockspace into left and right
    ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.19f, nullptr, &dockspace_id);
    const ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.3f, nullptr, &dockspace_id);
    const ImGuiID dock_id_center = dockspace_id; // remaining central

    // On the left, split for bottom panel (Resource Picker)
    const ImGuiID dock_id_left_top = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Up, 0.50f, nullptr, &dock_id_left);
    const ImGuiID dock_id_left_bottom = dock_id_left; // remainder

    // Dock windows
    ImGui::DockBuilderDockWindow("Project Manager##ProjectManager", dock_id_left_top);
    ImGui::DockBuilderDockWindow("Resource Picker##Editor", dock_id_left_bottom);
    ImGui::DockBuilderDockWindow("Texture Manager##Editor", dock_id_left_bottom);
    ImGui::DockBuilderDockWindow("Work Area##Editor", dock_id_center);
    ImGui::DockBuilderDockWindow("Resource Editor##Editor", dock_id_right);

    ImGui::DockBuilderFinish(ImGui::GetID("DockSpace"));

    m_layoutInitialized = true;
}

bool Application::isVersionNewer(const AppVersion& current, const AppVersion& other) const {
    if (current.major != other.major) return current.major < other.major;
    if (current.minor != other.minor) return current.minor < other.minor;
    if (current.patch != other.patch) return current.patch < other.patch;
    if (current.isRC != other.isRC) return current.isRC && !other.isRC;
    if (current.isRC) return current.rc < other.rc;
    return false; // They are equal
}

nlohmann::json Application::loadCache() {
    const auto cachePath = getConfigPath() / "cache.json";
    if (!std::filesystem::exists(cachePath)) {
        spdlog::info("Cache file does not exist, creating: {}", cachePath.string());

        nlohmann::json cache = nlohmann::json::object();

        std::ofstream outFile(cachePath);
        outFile << cache.dump(4);

        return cache;
    }

    std::ifstream inFile(cachePath);
    if (!inFile) {
        spdlog::error("Failed to open cache file: {}", cachePath.string());
        return nlohmann::json::object();
    }

    nlohmann::json cache;
    inFile >> cache;
    
    return cache;
}

void Application::saveCache(const nlohmann::json& cache) {
    const auto cachePath = getConfigPath() / "cache.json";
    std::ofstream outFile(cachePath);
    if (!outFile) {
        spdlog::error("Failed to open cache file for writing: {}", cachePath.string());
        return;
    }

    outFile << cache.dump(4);
}

size_t Application::writeBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data = static_cast<std::string*>(userdata);
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t Application::writeHeaderCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* map = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
    const std::string line(ptr, size * nmemb);
    const auto pos = line.find(':');

    if (pos != std::string::npos) {
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        auto ltrim = [](std::string& s) {
            s.erase(s.begin(), std::ranges::find_if(s, [](int ch) {
                return !std::isspace(ch);
            }));
        };
        auto rtrim = [](std::string& s) {
            s.erase(std::ranges::find_if(std::ranges::reverse_view(s), [](int ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        };

        std::ranges::transform(key, key.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        ltrim(val);
        auto v = val;
        rtrim(v);

        (*map)[key] = v;
    }

    return size * nmemb;
}

size_t Application::writeFileCallback(char* ptr, size_t sz, size_t nm, void* ud) {
    const auto fp = static_cast<FILE*>(ud);
    return std::fwrite(ptr, sz, nm, fp) * sz;
}

std::optional<HttpResponse> Application::getWithCache(const std::string& url, const std::string& cacheKey) {
    HttpResponse response;
    auto cache = loadCache();

    std::string cachedEtag, cachedLM, cachedBody;
    if (cache.contains(cacheKey)) {
        const auto& cached = cache[cacheKey];
        cachedEtag = cached.value<std::string>("etag", "");
        cachedLM = cached.value<std::string>("last_modified", "");
        cachedBody = cached.value<std::string>("body", "");
    }

    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: nitroefx-updater/1.0");
    std::string ifNone; std::string ifSince;
    if (!cachedEtag.empty()) { ifNone = "If-None-Match: " + cachedEtag; headers = curl_slist_append(headers, ifNone.c_str()); }
    if (!cachedLM.empty()) { ifSince = "If-Modified-Since: " + cachedLM; headers = curl_slist_append(headers, ifSince.c_str()); }

    response.headers.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Application::writeBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Application::writeHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 304 -> reuse cached body
    if (response.status == 304) {
        if (cachedBody.empty()) return std::nullopt; // nothing to use
        response.body = cachedBody;
    }

    // Persist fresh cache data on 200
    if (response.status == 200) {
        nlohmann::json entry = nlohmann::json::object();
        if (response.headers.contains("etag")) entry["etag"] = response.headers["etag"];
        if (response.headers.contains("last-modified")) entry["last_modified"] = response.headers["last-modified"];

        entry["body"] = response.body;
        cache[cacheKey] = std::move(entry);

        saveCache(cache);
    }

    return response;
}

std::optional<AppVersion> Application::getNewestVersion(std::span<const AppVersion> versions) {
    if (versions.empty()) {
        spdlog::warn("No versions available to compare.");
        return std::nullopt;
    }

    return *std::ranges::max_element(versions, 
        [this](const AppVersion& a, const AppVersion& b) {
            return isVersionNewer(a, b);
    });
}

VersionCheckResult Application::checkForUpdates() {
    VersionCheckResult result{};
    if (!m_settings.checkForUpdates) {
        spdlog::info("Update check is disabled in settings.");

        result.ok = true;
        result.updateAvailable = false;
        return result; // No updates to check
    }

    const auto localVersion = parseVersion(Application::VERSION);
    const auto latestVersion = findLatestVersion();

    if (!latestVersion) {
        spdlog::error("Failed to fetch latest version.");
        result.ok = false;
        result.updateAvailable = false;
        return result; // Error fetching latest version
    }

    result.ok = true;
    result.remoteTag = latestVersion->str;
    result.remoteIsRC = latestVersion->isRC;
    result.updateAvailable = isVersionNewer(*localVersion, *latestVersion);

    return result;
}

std::optional<AppVersion> Application::findLatestVersion() {
    const std::string url = "https://api.github.com/repos/Fexty12573/nitroefx/tags?per_page=100";
    auto response = getWithCache(url, "Fexty12573/nitroefx/tags");
    if (!response || (response->status != 200 && response->status != 304)) {
        spdlog::error("Failed to fetch latest version: HTTP {}", response ? response->status : 0);
        return std::nullopt;
    }

    auto j = nlohmann::json::parse(response->body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        spdlog::error("Invalid JSON response for tags: {}", response->body);
        return std::nullopt;
    }

    std::vector<AppVersion> versions;
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        if (item.value("draft", false)) continue;

        const std::string tag = item.value("name", "");
        if (tag.contains("-rc") && !m_settings.showReleaseCandidates) continue;

        auto maybeV = parseVersion(tag);
        if (maybeV) versions.push_back(*maybeV);
    }

    return getNewestVersion(versions);
}

void Application::applyUpdateNow(const std::filesystem::path& binaryPath, bool relaunch) {
    const auto currentExecutable = getExecutablePath();

#ifdef _WIN32
    // On Windows, we need to close the application before replacing the executable
    const auto pid = GetCurrentProcessId();

    const auto updaterExecutable = getTempPath() / "nitroefx-updater.exe";
    std::filesystem::copy_file(currentExecutable, updaterExecutable, std::filesystem::copy_options::overwrite_existing);

    // Steps:
    // 1. Launch a new updater process
    // 2. Close the current application
    // 3. The updater will replace the executable and then exit

    auto cmd = fmt::format(
        R"("{}" --apply-update "{}" "{}" {})",
        updaterExecutable.string(),
        binaryPath.string(),
        currentExecutable.string(),
        pid
    );

    if (relaunch) {
        cmd += " --relaunch";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    )) {
        spdlog::error("Failed to launch updater process: {}", GetLastError());
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    m_running = false;

#else
    const auto binaryPathStr = binaryPath.string();
    const auto targetPathStr = currentExecutable.string();

    int fd = ::open(binaryPathStr.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }

    if (::rename(binaryPathStr.c_str(), targetPathStr.c_str()) != 0) {
        spdlog::error("rename() failed when installing update");
        return;
    }

    std::string dir = std::filesystem::path(targetPathStr).parent_path().string();
    int dfd = ::open(dir.c_str(), O_DIRECTORY | O_RDONLY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }

    // If relaunch is requested, we need to exec the new binary
    if (relaunch) {
        // exec new binary with same argv (best-effort: re-use SDL main args)
        // If you have stored argc/argv, use them; otherwise relaunch with no args:
        char* const argv0[] = { const_cast<char*>(targetPathStr.c_str()), nullptr };
        ::execv(targetPathStr.c_str(), argv0);

        spdlog::error("execv failed, manual restart required");
    }
#endif
}

std::filesystem::path Application::downloadLatestArchive() {
    const auto tag = m_versionCheckResult.remoteTag;
    const auto asset = getUpdateAsset(tag);
    if (!asset) {
        spdlog::error("No download URL found for tag {}", tag);
        return {};
    }

    const auto temp = getTempPath();
    std::filesystem::create_directories(temp);

    // We can assume the asset is valid at this point
    const auto url = asset->value("browser_download_url", "");
    auto filename = temp / asset->value("name", "");

    if (!downloadToFile(url, filename.string())) {
        spdlog::error("Failed to download latest archive for tag {}", tag);
        return {};
    }

    return filename;
}

std::filesystem::path Application::extractLatestArchive(const std::filesystem::path& archive) {
    if (!std::filesystem::exists(archive)) {
        spdlog::error("Archive does not exist: {}", archive.string());
        return {};
    }

    const auto temp = getTempPath();
    std::filesystem::create_directories(temp);

#ifdef _WIN32
    const std::string wantedName = "nitroefx.exe";
#else
    const std::string wantedName = "nitroefx";
#endif

    auto extractedFile = temp / wantedName;
    if (!extractSingleFile(archive.string(), wantedName, extractedFile)) {
        spdlog::error("Failed to extract {} from archive {}", wantedName, archive.string());
        return {};
    }

    return extractedFile;
}

std::optional<nlohmann::json> Application::getUpdateAsset(const std::string& tag) {
    const std::string url = "https://api.github.com/repos/Fexty12573/nitroefx/releases/tags/" + tag;
    auto resp = getWithCache(url, "Fexty12573/nitroefx/release-" + tag);
    if (!resp || (resp->status != 200 && resp->status != 304)) {
        spdlog::error("Failed to get release for tag {}: HTTP {}", tag, resp ? resp->status : 0);
        return std::nullopt;
    }

    nlohmann::json j = nlohmann::json::parse(resp->body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("assets")) {
        spdlog::error("Invalid release JSON for tag {}", tag);
        return std::nullopt;
    }

    const auto& assets = j["assets"];
    if (!assets.is_array()) return std::nullopt;

#ifdef _WIN32
    auto looks = [](const std::string& n) {
        return n.ends_with("windows.zip");
    };
#else
    auto looks = [](const std::string& n) {
        return n.ends_with("linux.tar.gz");
    };
#endif

    for (const auto& a : assets) {
        const std::string name = a.value("name", "");
        std::string dlUrl = a.value("browser_download_url", "");
        if (looks(name) && !dlUrl.empty()) return a;
    }

    return std::nullopt;
}

bool Application::downloadToFile(const std::string& url, const std::string& outPath) {
    FILE* f = std::fopen(outPath.c_str(), "wb");
    if (!f) {
        spdlog::error("Failed to open {} for writing", outPath);
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        return false;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: nitroefx-updater/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Application::writeFileCallback);

    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    std::fclose(f);

    if (rc != CURLE_OK || (status != 200)) {
        spdlog::error("Download failed: {} (rc={}, http={})", url, (int)rc, status);
        std::error_code ec;
        std::filesystem::remove(outPath, ec);

        return false;
    }

    return true;
}

bool Application::extractSingleFile(const std::filesystem::path& archivePath, const std::string& wantedName, const std::filesystem::path& outPath) {
    if (archivePath.extension() == ".zip") {
        return extractZip(archivePath, wantedName, outPath);
    } else if (archivePath.extension() == ".gz" || archivePath.extension() == ".tgz") {
        return extractTarGz(archivePath, wantedName, outPath);
    } else {
        spdlog::error("Unsupported archive format: {}", archivePath.string());
        return false;
    }
}

bool Application::extractZip(const std::filesystem::path& archivePath, const std::string& wantedName, const std::filesystem::path& outPath) {
    int err = MZ_OK;

    void* reader = mz_zip_reader_create();
    mz_zip_reader_set_encoding(reader, MZ_ENCODING_UTF8);

    if (mz_zip_reader_open_file(reader, archivePath.string().c_str()) != MZ_OK) {
        spdlog::error("Failed to open zip archive: {}", archivePath.string());
        mz_zip_reader_delete(&reader);
        return false;
    }

    err = mz_zip_reader_locate_entry(reader, wantedName.c_str(), true);
    if (err != MZ_OK) {
        spdlog::error("File '{}' not found in zip archive '{}'", wantedName, archivePath.string());
        mz_zip_reader_delete(&reader);
        return false;
    }

    std::filesystem::create_directories(outPath.parent_path());

    err = mz_zip_reader_entry_save_file(reader, outPath.string().c_str());
    if (err != MZ_OK) {
        spdlog::error("Failed to extract file '{}' from zip archive '{}'", wantedName, archivePath.string());
        mz_zip_reader_delete(&reader);
        return false;
    }

    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);

    return true;
}

bool Application::extractTarGz(const std::filesystem::path& archivePath, const std::string& wantedName, const std::filesystem::path& outPath) {
    std::vector<u8> tarData;
    if (!gunzipFile(archivePath, tarData)) {
        return false;
    }

    mtar_t tar{
        .read = [](mtar_t* tar, void* buf, u32 size) -> int {
            const auto stream = static_cast<std::vector<u8>*>(tar->stream);
            u32 toRead = std::min(size, (u32)stream->size() - tar->pos);
            std::memcpy(buf, stream->data() + tar->pos, toRead);
            return MTAR_ESUCCESS;
        },
        .write = nullptr,
        .seek = [](mtar_t* tar, u32 offset) -> int {
            const auto stream = static_cast<std::vector<u8>*>(tar->stream);
            if (offset > stream->size()) return MTAR_ESEEKFAIL;
            return MTAR_ESUCCESS;
        },
        .close = [](mtar_t* tar) -> int { return MTAR_ESUCCESS; },
        .stream = &tarData
    };

    mtar_header_t header;
    if (mtar_find(&tar, wantedName.c_str(), &header) != MTAR_ESUCCESS) {
        spdlog::error("File '{}' not found in tar archive '{}'", wantedName, archivePath.string());
        return false;
    }
    
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        spdlog::error("Failed to create output file: {}", outPath.string());
        return false;
    }

    std::vector<u8> decompressed(header.size);
    mtar_read_data(&tar, decompressed.data(), header.size);
    out.write((const char*)decompressed.data(), header.size);

    mtar_close(&tar);

#ifndef _WIN32
    std::filesystem::permissions(outPath,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
        std::filesystem::perms::group_exec | std::filesystem::perms::group_read |
        std::filesystem::perms::others_exec | std::filesystem::perms::others_read,
        std::filesystem::perm_options::add);
#endif

    return true;
}

bool Application::gunzipFile(const std::filesystem::path& srcPath, std::vector<u8>& dst) {
    gzFile f = gzopen(srcPath.string().c_str(), "rb");
    if (!f) {
        spdlog::error("Failed to open gzip file: {}", srcPath.string());
        return false;
    }

    constexpr auto bufSize = 64 * 1024u;
    std::vector<u8> buffer(bufSize);
    int r;
    while ((r = gzread(f, buffer.data(), bufSize)) > 0) {
        dst.insert(dst.end(), buffer.data(), buffer.data() + r);
    }

    gzclose(f);
    return r >= 0;
}

bool Application::hasActiveEmitters() const {
    if (!g_projectManager) return false;
    if (!g_projectManager->hasActiveEditor()) return false;
    const auto editor = g_projectManager->getActiveEditor();
    if (!editor) return false;
    // If any emitters exist we treat as active animation
    return !editor->getParticleSystem().getEmitters().empty();
}

bool Application::isWindowMinimizedOrHidden() const {
    if (!m_window) return false;
    const auto flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_MINIMIZED) || (flags & SDL_WINDOW_HIDDEN);
}

bool Application::isWindowFocused() const {
    if (!m_window) return false;
    const auto flags = SDL_GetWindowFlags(m_window);
    return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}
