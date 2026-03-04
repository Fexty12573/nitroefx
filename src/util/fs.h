#pragma once

#include <types.h>

#include <filesystem>
#include <fstream>
#include <span>


namespace FSUtil {

inline std::vector<u8> readToBytes(const std::filesystem::path& path) {
    if (!exists(path) || !is_regular_file(path)) {
        return {};
    }

    const auto size = std::filesystem::file_size(path);
    std::vector<u8> data(size);
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (stream.is_open()) {
        stream.read(reinterpret_cast<char*>(data.data()), data.size());
    }

    return data;
}

inline void writeBytes(const std::filesystem::path& path, std::span<const u8> data) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream stream(path, std::ios::out | std::ios::binary);
    if (stream.is_open()) {
        stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

}
