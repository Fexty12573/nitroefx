#pragma once

#include "types.h"

#include <string>
#include <vector>

#include <fmt/format.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>


enum class KeybindType {
    Key,
    Mouse
};

struct Keybind {
    KeybindType type;
    union {
        struct {
            SDL_Keycode key;
            SDL_Keymod modifiers;
        };
        Uint8 button; // For mouse buttons
    };

    [[nodiscard]] std::string toString() const {
        if (type == KeybindType::Key) {
            return fmt::format("{}{}", getModifierName(normalizeModifiers(modifiers)), SDL_GetKeyName(key));
        }
        else {
            return fmt::format("Mouse{}", button);
        }
    }

    // Collapse left/right variants (L* / R*) into aggregate SDL_KMOD_* flags
    static SDL_Keymod normalizeModifiers(SDL_Keymod mod) {
        SDL_Keymod norm = SDL_KMOD_NONE;
        if (mod & SDL_KMOD_CTRL) norm = static_cast<SDL_Keymod>(norm | SDL_KMOD_CTRL);
        if (mod & SDL_KMOD_SHIFT) norm = static_cast<SDL_Keymod>(norm | SDL_KMOD_SHIFT);
        if (mod & SDL_KMOD_ALT) norm = static_cast<SDL_Keymod>(norm | SDL_KMOD_ALT);
        if (mod & SDL_KMOD_GUI) norm = static_cast<SDL_Keymod>(norm | SDL_KMOD_GUI);
        return norm;
    }

private:
    static std::string getModifierName(SDL_Keymod mod) {
        if (mod == SDL_KMOD_NONE) {
            return "";
        }

        std::string name;
        for (const auto& [key, value] : s_modifierNames) {
            if (mod & key) {
                name += value + "+";
            }
        }

        return name;
    }

    // We treat the L/R modifiers as the same for simplicity
    static inline const std::vector<std::pair<SDL_Keymod, std::string>> s_modifierNames = {
        { SDL_KMOD_NONE, "" },
        { SDL_KMOD_CTRL, "Ctrl" },
        { SDL_KMOD_SHIFT, "Shift" },
        { SDL_KMOD_ALT, "Alt" },
        { SDL_KMOD_GUI, "Win" }
    };
};
