#include "application.h"
#include "fonts/IconsFontAwesome6.h"
#include "imgui/extensions.h"

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <ShObjIdl.h>
#endif
#include <tinyfiledialogs.h>

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
        spdlog::info("OpenGL Warn: {}", message);
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
        ApplicationAction::KillEmitters,
        ApplicationAction::ResetCamera,
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
    SDL_GL_SetSwapInterval(1); // Enable vsync

    glewExperimental = GL_TRUE;
    const GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        spdlog::error("GLEW Error: {}", (const char*)glewGetErrorString(glewError));
        return 1;
    }

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "nitroefx.ini";

    m_editor = std::make_unique<Editor>();
    m_settings = ApplicationSettings::getDefault();

    clearTempDir();
    loadConfig();
    loadFonts();
    setColors();

    // loadConfig might change the window size so we create the window
    // in a hidden state and show it after loading the config
    SDL_ShowWindow(m_window);

    ImGui_ImplSDL3_InitForOpenGL(m_window, m_context);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_preferencesWindowId = ImHashStr("Preferences##Application");

    if (argc > 1) {
        const std::filesystem::path arg = argv[1];
        if (std::filesystem::is_directory(arg)) {
            g_projectManager->openProject(arg);
        } else if (arg.extension() == ".spl") {
            g_projectManager->openEditor(arg);
        } else {
            spdlog::warn("Invalid argument: {}", arg.string());
        }
    }

    std::chrono::time_point<std::chrono::high_resolution_clock> lastFrame = std::chrono::high_resolution_clock::now();

    while (m_running) {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto delta = std::chrono::duration<float>(now - lastFrame).count();
        m_deltaTime = delta;

        pollEvents();

        m_editor->updateParticles(delta);
        m_editor->renderParticles();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Build default docking layout once if no ini file exists
        if (!m_layoutInitialized) {
            initDefaultDockingLayout();
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
        lastFrame = now;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    saveConfig();

    return 0;
}

int Application::runCli(argparse::ArgumentParser &cli) {
    if (!cli.is_subcommand_used("cli")) {
        spdlog::error("This application is not running in CLI mode");
        return 1;
    }

    if (cli.get<bool>("--export")) {
        const auto format = cli.get<std::string>("--format");
        const auto output = cli.get<std::string>("--output");
        const auto indices = cli.get<std::vector<int>>("--index");

        SPLArchive archive(cli.get<std::string>("path"));
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
        m_listeningKeybind->modifiers = event.key.mod;
        m_listeningForInput = false;
        m_listeningKeybind = nullptr;
        m_exitKeybindListening = true;
        return;
    }

    for (const auto& [action, keybind] : m_settings.keybinds) {
        if (keybind.type == KeybindType::Key) {
            if (event.key.key == keybind.key && event.key.mod == keybind.modifiers) {
                executeAction(action);
                return;
            }
        }
    }
    
    switch (event.key.key) {
    case SDLK_N:
        if (event.key.mod & SDL_KMOD_CTRL) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                spdlog::warn("New Project not implemented");
            } else {
                g_projectManager->openEditor();
            }
        }
        break;

    case SDLK_O:
        if (event.key.mod & SDL_KMOD_CTRL) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                const auto path = openDirectory();
                if (!path.empty()) {
                    g_projectManager->openProject(path);
                }
            } else {
                const auto path = openFile();
                if (!path.empty()) {
                    g_projectManager->openEditor(path);
                }
            }
        }
        
        break;

    case SDLK_S:
        if (event.key.mod & SDL_KMOD_CTRL) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                g_projectManager->saveAllEditors();
            } else {
                m_editor->save();
            }
        }
        break;

    case SDLK_W:
        if (event.key.mod & SDL_KMOD_CTRL) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                if (g_projectManager->hasOpenEditors()) {
                    g_projectManager->closeAllEditors();
                }
            } else {
                if (g_projectManager->hasActiveEditor()) {
                    g_projectManager->closeEditor(g_projectManager->getActiveEditor());
                }
            }
        }
        break;

    case SDLK_P:
        if (event.key.mod & SDL_KMOD_CTRL) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                m_editor->playEmitterAction(EmitterSpawnType::Looped);
            } else {
                m_editor->playEmitterAction(EmitterSpawnType::SingleShot);
            }
        }
        break;

    case SDLK_K:
        if (event.key.mod & SDL_KMOD_CTRL) {
            m_editor->killEmitters();
        }
        break;
    
    case SDLK_R:
        if (event.key.mod & SDL_KMOD_CTRL) {
            m_editor->resetCamera();
        }
        break;

    case SDLK_Z:
        if (event.key.mod & SDL_KMOD_CTRL) {
            m_editor->undo();
        }
        break;

    case SDLK_Y:
        if (event.key.mod & SDL_KMOD_CTRL) {
            m_editor->redo();
        }
        break;

    case SDLK_F4:
        if (event.key.mod & SDL_KMOD_ALT) {
            m_running = false;
        }
        break;

    default:
        break;
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
                if (ImGui::MenuItemIcon(ICON_FA_FOLDER_PLUS, "Project", "Ctrl+Shift+N", false, IM_COL32(157, 142, 106, 255))) {
                    spdlog::warn("New Project not implemented");
                }

                if (ImGui::MenuItemIcon(ICON_FA_FILE_CIRCLE_PLUS, "SPL File", "Ctrl+N")) {
                    g_projectManager->openEditor();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Open")) {
                if (ImGui::MenuItemIcon(ICON_FA_FOLDER_OPEN, "Project", KEYBINDSTR(OpenProject), false, IM_COL32(157, 142, 106, 255))) {
                    const auto path = openDirectory();
                    if (!path.empty()) {
                        addRecentProject(path);
                        g_projectManager->openProject(path);
                    }
                }

                if (ImGui::MenuItemIcon(ICON_FA_FILE, "SPL File", KEYBINDSTR(OpenFile))) {
                    const auto path = openFile();
                    if (!path.empty()) {
                        addRecentFile(path);
                        g_projectManager->openEditor(path);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Open Recent")) {
                ImGui::SeparatorText("Projects");
                if (m_recentProjects.empty()) {
                    ImGui::MenuItem("No Recent Projects", nullptr, false, false);
                }

                for (const auto& path : m_recentProjects) {
                    if (ImGui::MenuItem(path.c_str())) {
                        g_projectManager->openProject(path);
                    }
                }

                ImGui::SeparatorText("Files");
                if (m_recentFiles.empty()) {
                    ImGui::MenuItem("No Recent Files", nullptr, false, false);
                }

                for (const auto& path : m_recentFiles) {
                    if (ImGui::MenuItem(path.c_str())) {
                        g_projectManager->openEditor(path);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save", KEYBINDSTR(Save), false, IM_COL32(105, 190, 255, 255), hasActiveEditor)) {
                m_editor->save();
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save As...", nullptr, false, IM_COL32(105, 190, 255, 255), hasActiveEditor)) {
                const auto path = saveFile();
                if (!path.empty()) {
                    m_editor->saveAs(path);
                    addRecentFile(path);
                }
            }

            if (ImGui::MenuItemIcon(ICON_FA_FLOPPY_DISK, "Save All", KEYBINDSTR(SaveAll), false, IM_COL32(105, 190, 255, 255), hasOpenEditors)) {
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

            if (ImGui::MenuItemIcon(ICON_FA_PLAY, "Play Emitter", KEYBINDSTR(PlayEmitter), false, IM_COL32(143, 228, 143, 255), hasActiveEditor)) {
                m_editor->playEmitterAction(EmitterSpawnType::SingleShot);
            }

            if (ImGui::MenuItemIcon(ICON_FA_REPEAT, "Play Looped Emitter", KEYBINDSTR(PlayEmitterLooped), false, IM_COL32(133, 208, 133, 255), hasActiveEditor)) {
                m_editor->playEmitterAction(EmitterSpawnType::Looped);
            }

            if (ImGui::MenuItemIcon(ICON_FA_STOP, "Kill Emitters", KEYBINDSTR(KillEmitters), false, IM_COL32(245, 87, 98, 255), hasActiveEditor)) {
                m_editor->killEmitters();
            }

            if (ImGui::MenuItemIcon(ICON_FA_CAMERA_ROTATE, "Reset Camera", KEYBINDSTR(ResetCamera), false, 0, hasActiveEditor)) {
                m_editor->resetCamera();
            }

            if (ImGui::MenuItemIcon(ICON_FA_WRENCH, "Preferences")) {
                m_preferencesOpen = true;

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

            m_editor->renderMenu("View");

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    const auto viewport = (ImGuiViewportP*)ImGui::GetMainViewport();
    constexpr auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    constexpr float framePaddingY = 4.0f;
    constexpr float itemHeight = 24.0f;
    constexpr float barHeight = itemHeight + 2.0f;
    constexpr ImVec2 size = { itemHeight, itemHeight };

    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(79, 79, 79, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 90, 90, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.5f, 0.5f });
    ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 4.0f); // Cut item spacing in half
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 2.0f, framePaddingY });
    
    if (ImGui::BeginViewportSideBar("##SecondaryMenuBar", viewport, ImGuiDir_Up, barHeight, flags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::IconButton(ICON_FA_FILE, size)) {
                const auto file = openFile();
                if (!file.empty()) {
                    addRecentFile(file);
                    g_projectManager->openEditor(file);
                }
            }

            if (ImGui::IconButton(ICON_FA_FOLDER_OPEN, size, IM_COL32(157, 142, 106, 255))) {
                const auto project = openDirectory();
                if (!project.empty()) {
                    addRecentProject(project);
                    g_projectManager->openProject(project);
                }
            }

            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_FLOPPY_DISK, size, IM_COL32(105, 190, 255, 255), hasActiveEditor)) {
                m_editor->save();
            }

            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_ROTATE_LEFT, size, 0, m_editor->canUndo())) {
                m_editor->undo();
            }

            if (ImGui::IconButton(ICON_FA_ROTATE_RIGHT, size, 0, m_editor->canRedo())) {
                m_editor->redo();
            }
            
            ImGui::VerticalSeparator(itemHeight);

            if (ImGui::IconButton(ICON_FA_PLAY, size, IM_COL32(143, 228, 143, 255), hasActiveEditor)) {
                m_editor->playEmitterAction(EmitterSpawnType::SingleShot);
            }

            if (ImGui::IconButton(ICON_FA_REPEAT, size, IM_COL32(133, 208, 133, 255), hasActiveEditor)) {
                m_editor->playEmitterAction(EmitterSpawnType::Looped);
            }

            if (ImGui::IconButton(ICON_FA_STOP, size, IM_COL32(245, 87, 98, 255), hasActiveEditor)) {
                m_editor->killEmitters();
            }

            if (ImGui::IconButton(ICON_FA_CAMERA_ROTATE, size, 0, hasActiveEditor)) {
                m_editor->resetCamera();
            }

            m_editor->renderToolbar(itemHeight);

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

    if (ImGui::BeginPopupModal("Preferences##Application", &m_preferencesOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Keybinds");

        if (ImGui::BeginTable("Keybinds##Application", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH)) {
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
                ImGui::SetNextItemWidth(300);

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

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();
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
        18.0f, 
        &config
    );

    config.MergeMode = true;
    constexpr ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        g_icon_font_compressed_data, 
        g_icon_font_compressed_size, 
        18.0f, 
        &config,
        iconRanges
    );

    config.MergeMode = false;

    m_fonts["Italic"] = io.Fonts->AddFontFromMemoryCompressedTTF(
        g_tahoma_italic_compressed_data,
        g_tahoma_italic_compressed_size,
        18.0f,
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

    m_editor->loadConfig(config);
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
        const auto filePath = openFile();
        if (!filePath.empty()) {
            addRecentFile(filePath);
            g_projectManager->openEditor(filePath);
        }
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
    case ApplicationAction::Exit:
        m_running = false;
        break;
    case ApplicationAction::PlayEmitter:
        m_editor->playEmitterAction(EmitterSpawnType::SingleShot);
        break;
    case ApplicationAction::PlayEmitterLooped:
        m_editor->playEmitterAction(EmitterSpawnType::Looped);
        break;
    case ApplicationAction::KillEmitters:
        m_editor->killEmitters();
        break;
    case ApplicationAction::ResetCamera:
        m_editor->resetCamera();
        break;
    }
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

std::string Application::openFile() {
    const char* filters[] = { "*.spa" };
    const char* result = tinyfd_openFileDialog(
        "Open File", 
        "", 
        1, 
        filters, 
        "SPL Files", 
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

    // If an ini file already exists, don't override user's layout
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
