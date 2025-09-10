#pragma once

#include "application_settings.h"
#include "editor/editor.h"
#include "editor/project_manager.h"
#include "gfx/gl_texture.h"

#include <argparse/argparse.hpp>
#include <SDL3/SDL_events.h>
#include <string_view>

#include <optional>
#include <set>
#include <filesystem>
#include <unordered_map>
#include <span>


struct AppVersion {
    int major;
    int minor;
    int patch;
    bool isRC;
    int rc;
    std::string str;
};

struct HttpCacheEntry {
    std::string etag;
    std::string lastModified;
    std::string body;
};

struct HttpResponse {
    long status;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct VersionCheckResult {
    bool ok;
    bool updateAvailable;
    std::string remoteTag;
    bool remoteIsRC;
};

class Application {
public:
    Application();

    int run(int argc, char** argv);
    int runCli(argparse::ArgumentParser& cli);

    void saveConfig();
    ImFont* getFont(const std::string& name);

    std::shared_ptr<GLTexture> getIcon() const {
        return m_icon;
    }

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
    static std::filesystem::path getExecutablePath();

    static std::optional<AppVersion> parseVersion(const std::string& versionStr);

    static int update(const std::filesystem::path& srcPath, const std::filesystem::path& dstPath, unsigned long pid, bool relaunch);

    static constexpr auto VERSION = "v1.2.1";

private:
    void pollEvents();
    void handleKeydown(const SDL_Event& event);
    void handleMouseDown(const SDL_Event& event);
    void dispatchEvent(const SDL_Event& event);
    void renderMenuBar();
    void renderPreferences();
    void renderPerformanceWindow();
    void renderAboutWindow();
    void renderUpdateWindow();
    void renderWelcomeWindow();
    void renderRestartPopup();
    void setColors();
    void loadFonts();
    void loadConfig();
    void loadIcon();
    void clearTempDir();
    void executeAction(u32 action);

    void restart();

    void addRecentFile(const std::string& path);
    void addRecentProject(const std::string& path);

    void tryOpenEditor(const std::filesystem::path& path);

    void initDefaultDockingLayout();

    // Update checking
    bool isVersionNewer(const AppVersion& current, const AppVersion& other) const;
    nlohmann::json loadCache();
    void saveCache(const nlohmann::json& cache);
    std::optional<HttpResponse> getWithCache(const std::string& url, const std::string& cacheKey);
    std::optional<AppVersion> getNewestVersion(std::span<const AppVersion> versions);
    VersionCheckResult checkForUpdates();

    std::optional<AppVersion> findLatestVersion();

    static size_t writeBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t writeHeaderCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t writeFileCallback(char* ptr, size_t sz, size_t nm, void* ud);

    // Updating
    void applyUpdateNow(const std::filesystem::path& binaryPath, bool relaunch);
    std::filesystem::path downloadLatestArchive();
    std::filesystem::path extractLatestArchive(const std::filesystem::path& archive);

    std::optional<nlohmann::json> getUpdateAsset(const std::string& tag);
    bool downloadToFile(const std::string& url, const std::string& outPath);
    bool extractSingleFile(const std::filesystem::path& archivePath, const std::string& wantedName, const std::filesystem::path& outPath);

private:
    bool m_running = true;
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::unique_ptr<Editor> m_editor;

    std::deque<std::string> m_recentFiles;
    std::deque<std::string> m_recentProjects;

    std::map<std::string, ImFont*> m_fonts;

    std::string m_iniFilename;

    VersionCheckResult m_versionCheckResult;
    bool m_updateOnClose = false;

    std::shared_ptr<GLTexture> m_icon;

    ApplicationSettings m_settings;
    std::vector<u32> m_sortedActions;
    int m_preferencesWindowId = 0;
    int m_aboutWindowId = 0;
    int m_updateWindowId = 0;
    int m_welcomeWindowId = 0;
    bool m_preferencesOpen = false;
    bool m_aboutWindowOpen = false;
    bool m_listeningForInput = false;
    bool m_exitKeybindListening = false;
    bool m_firstFrame = true;
    bool m_uiScaleChanged = false;
    bool m_reloadFonts = false;
    Keybind* m_listeningKeybind = nullptr;

    std::set<SDL_Keycode> m_modifierKeys;

    bool m_performanceWindowOpen = false;
    float m_deltaTime = 0.0f;

    // Tracks whether we've already attempted to initialize the layout
    bool m_layoutInitialized = false;

    std::string m_downloadedArchivePath;
    std::string m_extractedBinaryPath;
};

inline Application* g_application = nullptr;
