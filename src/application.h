#pragma once

#include "application_settings.h"
#include "editor/editor.h"
#include "editor/project_manager.h"

#include <argparse/argparse.hpp>
#include <SDL3/SDL_events.h>
#include <string_view>

#include <optional>
#include <set>


class Application {
public:
    Application();

    int run(int argc, char** argv);
    int runCli(argparse::ArgumentParser& cli);

    void saveConfig();
    ImFont* getFont(const std::string& name);

    std::optional<Keybind> getKeybind(u32 action) const;
    std::optional<Keybind> getKeybind(const std::string_view& name) const;

    Editor* getEditor() const {
        return m_editor.get();
    }

    static std::string openFile();
    static std::string saveFile(const std::string& default_path = {});
    static std::string openDirectory(const char* title = nullptr);

    static std::filesystem::path getConfigPath();
    static std::filesystem::path getTempPath();

private:
    void pollEvents();
    void handleKeydown(const SDL_Event& event);
    void handleMouseDown(const SDL_Event& event);
    void dispatchEvent(const SDL_Event& event);
    void renderMenuBar();
    void renderPreferences();
    void renderPerformanceWindow();
    void setColors();
    void loadFonts();
    void loadConfig();
    void clearTempDir();
    void executeAction(u32 action);

    void addRecentFile(const std::string& path);
    void addRecentProject(const std::string& path);

    void tryOpenEditor(const std::filesystem::path& path);

    // Initialize a default docking layout if no ImGui ini file exists
    void initDefaultDockingLayout();

private:
    bool m_running = true;
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::unique_ptr<Editor> m_editor;

    std::deque<std::string> m_recentFiles;
    std::deque<std::string> m_recentProjects;

    std::map<std::string, ImFont*> m_fonts;
    
    ApplicationSettings m_settings;
    std::vector<u32> m_sortedActions;
    int m_preferencesWindowId = 0;
    bool m_preferencesOpen = false;
    bool m_listeningForInput = false;
    bool m_exitKeybindListening = false;
    Keybind* m_listeningKeybind = nullptr;

    std::set<SDL_Keycode> m_modifierKeys;

    bool m_performanceWindowOpen = false;
    float m_deltaTime = 0.0f;

    // Tracks whether we've already attempted to initialize the layout
    bool m_layoutInitialized = false;
};

inline Application* g_application = nullptr;
