#pragma once

#include "types.h"

#include <SDL3/SDL_events.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>


enum class EditorType {
    Particle,
    MoveAnim,
};


class BaseEditor {
public:
    using Clock = std::chrono::steady_clock;

    virtual ~BaseEditor() = default;

    [[nodiscard]] virtual bool isModified() const = 0;
    [[nodiscard]] virtual bool isTemp() const = 0;
    [[nodiscard]] virtual bool isRecovered() const = 0;
    [[nodiscard]] virtual EditorType getType() const = 0;
    [[nodiscard]] virtual u64 getUniqueID() const = 0;
    [[nodiscard]] virtual std::string getName() const = 0;
    [[nodiscard]] virtual const std::filesystem::path& getPath() const = 0;
    [[nodiscard]] virtual bool notifyClosing() = 0;
    [[nodiscard]] virtual std::optional<size_t> getNarcIndex() const { return std::nullopt; }
    [[nodiscard]] virtual bool isAnimating() const { return false; }

    virtual void makePermanent() = 0;
    virtual void save() = 0;
    virtual void saveAs(const std::filesystem::path& path) = 0;
    virtual void rename(std::string_view name) = 0;

    [[nodiscard]] virtual Clock::time_point getLastBackupTime() const = 0;
    virtual void saveBackup() = 0;

    virtual std::pair<bool, bool> render() = 0;
    virtual void handleEvent(const SDL_Event& event) = 0;

    virtual void update(float dt) {}

    virtual void renderStats() {}
    virtual void renderPanels() {}
};
