#pragma once

#include "keybind.h"
#include "util/crc32.h"

#include <map>


struct ApplicationSettings {
    std::map<u32, Keybind> keybinds;
    bool checkForUpdates;
    bool showReleaseCandidates;
    float uiScale = 1.0f;
    std::vector<std::string> indexIgnores;
    bool toolbarCentered;

    static ApplicationSettings getDefault();
};

using namespace crc::literals;

struct ApplicationAction {
    // application/File
    static constexpr u32 NewFile = "application/File/NewFile"_crc;
    static constexpr u32 OpenProject = "application/File/OpenProject"_crc;
    static constexpr u32 OpenFile = "application/File/OpenFile"_crc;
    static constexpr u32 Save = "application/File/Save"_crc;
    static constexpr u32 SaveAll = "application/File/SaveAll"_crc;
    static constexpr u32 Close = "application/File/Close"_crc;
    static constexpr u32 CloseAll = "application/File/CloseAll"_crc;
    static constexpr u32 Exit = "application/File/Exit"_crc;

    // application/Edit
    static constexpr u32 Undo = "application/Edit/Undo"_crc;
    static constexpr u32 Redo = "application/Edit/Redo"_crc;
    static constexpr u32 PlayEmitter = "application/Edit/PlayEmitter"_crc;
    static constexpr u32 PlayEmitterLooped = "application/Edit/PlayEmitterLooped"_crc;
    static constexpr u32 PlayAllEmitters = "application/Edit/PlayAllEmitters"_crc;
    static constexpr u32 KillEmitters = "application/Edit/KillEmitters"_crc;
    static constexpr u32 ResetCamera = "application/Edit/ResetCamera"_crc;

    // project
    static constexpr u32 QuickOpen = "project/QuickOpen"_crc;

    static inline const std::map<u32, const char*> Names = {
        { NewFile, "New File" },
        { OpenProject, "Open Project" },
        { OpenFile, "Open File" },
        { Save, "Save" },
        { SaveAll, "Save All" },
        { Close, "Close" },
        { CloseAll, "Close All" },
        { Exit, "Exit" },

        { Undo, "Undo" },
        { Redo, "Redo" },
        { PlayEmitter, "Play Emitter" },
        { PlayEmitterLooped, "Play Emitter (Looped)" },
        { PlayAllEmitters, "Play All Emitters" },
        { KillEmitters, "Kill Emitters" },
        { ResetCamera, "Reset Camera" },

        { QuickOpen, "Quick Open" }
    };
};
