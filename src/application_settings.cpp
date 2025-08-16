#include "application_settings.h"

ApplicationSettings ApplicationSettings::getDefault() {
    return {
        .keybinds = {
            { ApplicationAction::NewFile, { KeybindType::Key, { SDLK_N, SDL_KMOD_CTRL } } },
            { ApplicationAction::OpenProject, { KeybindType::Key, { SDLK_O, SDL_KMOD_CTRL } } },
            { ApplicationAction::OpenFile, { KeybindType::Key, { SDLK_O, SDL_KMOD_CTRL | SDL_KMOD_SHIFT } } },
            { ApplicationAction::Save, { KeybindType::Key, { SDLK_S, SDL_KMOD_CTRL } } },
            { ApplicationAction::SaveAll, { KeybindType::Key, { SDLK_S, SDL_KMOD_CTRL | SDL_KMOD_SHIFT } } },
            { ApplicationAction::Close, { KeybindType::Key, { SDLK_W, SDL_KMOD_CTRL } } },
            { ApplicationAction::CloseAll, { KeybindType::Key, { SDLK_W, SDL_KMOD_CTRL | SDL_KMOD_SHIFT } } },
            { ApplicationAction::Exit, { KeybindType::Key, { SDLK_F4, SDL_KMOD_ALT } } },
            { ApplicationAction::Undo, { KeybindType::Key, { SDLK_Z, SDL_KMOD_CTRL } } },
            { ApplicationAction::Redo, { KeybindType::Key, { SDLK_Y, SDL_KMOD_CTRL } } },
            { ApplicationAction::PlayEmitter, { KeybindType::Key, { SDLK_P, SDL_KMOD_CTRL } } },
            { ApplicationAction::PlayEmitterLooped, { KeybindType::Key, { SDLK_P, SDL_KMOD_CTRL | SDL_KMOD_SHIFT } } },
            { ApplicationAction::PlayAllEmitters, { KeybindType::Key, { SDLK_A, SDL_KMOD_CTRL } } },
            { ApplicationAction::KillEmitters, { KeybindType::Key, { SDLK_K, SDL_KMOD_CTRL } } },
            { ApplicationAction::ResetCamera, { KeybindType::Key, { SDLK_R, SDL_KMOD_CTRL } } }
        },
        .checkForUpdates = true,
        .showReleaseCandidates = false
    };
};
