#include "wsl.h"

#include <fstream>
#include <cstdio>
#include <ranges>

#ifdef _WIN32
#include <windows.h>
#include <winnetwk.h>
#include <cwctype>

#ifdef CreateFile
#undef CreateFile
#endif

#endif

#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace WSLUtil {

static std::wstring toLower(std::wstring s) {
#ifdef _WIN32
    for (auto& ch : s) {
        ch = (wchar_t)::towlower(ch);
    }
#else
    for (auto& ch : s) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = (wchar_t)(ch - L'A' + L'a');
        }
    }
#endif
    return s;
}

std::optional<WslMapping> detectMapping(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring unc = path.wstring();

    auto startsWithWsl = [](const std::wstring& s)->bool {
        const auto low = toLower(s);
        return low.starts_with(L"\\\\wsl$\\") || low.starts_with(L"\\\\wsl.localhost\\");
    };

    if (!startsWithWsl(unc)) {
        // Resolve mapped drives
        // First, try WNetGetUniversalNameW on the full path
        WCHAR buffer[1024];
        DWORD size = sizeof(buffer);

        auto* info = reinterpret_cast<UNIVERSAL_NAME_INFOW*>(buffer);
        DWORD rc = WNetGetUniversalNameW(unc.c_str(), UNIVERSAL_NAME_INFO_LEVEL, info, &size);
        if (rc == ERROR_MORE_DATA) {
            // Allocate a buffer of the requested size and try again
            std::vector<BYTE> dyn;
            dyn.resize(size);
            auto* infoDyn = reinterpret_cast<UNIVERSAL_NAME_INFOW*>(dyn.data());
            rc = WNetGetUniversalNameW(unc.c_str(), UNIVERSAL_NAME_INFO_LEVEL, infoDyn, &size);
            if (rc == NO_ERROR && infoDyn->lpUniversalName) {
                unc = infoDyn->lpUniversalName;
            }
        } else if (rc == NO_ERROR && info->lpUniversalName) {
            unc = info->lpUniversalName;
        }

        // If still not in UNC form, fall back to WNetGetConnectionW using the drive letter
        if (!startsWithWsl(unc)) {
            if (unc.size() >= 2 && unc[1] == L':') {
                const WCHAR drive[3] = { unc[0], L':', L'\0' };

                DWORD remoteSize = 0;
                DWORD rcConn = WNetGetConnectionW(drive, nullptr, &remoteSize);
                if (rcConn == ERROR_MORE_DATA && remoteSize > 0) {
                    std::vector<WCHAR> remote(remoteSize);

                    rcConn = WNetGetConnectionW(drive, remote.data(), &remoteSize);
                    if (rcConn == NO_ERROR) {
                        const std::wstring remoteName(remote.data());

                        // Append the remainder of the original path after the drive spec
                        std::wstring remainder = unc.substr(2);
                        if (!remoteName.empty() && remoteName.back() == L'\\') {
                            // Ensure only one backslash when concatenating
                            if (!remainder.empty() && remainder.front() == L'\\') {
                                remainder.erase(remainder.begin());
                            }
                        } else {
                            if (remainder.empty() || remainder.front() != L'\\') {
                                remainder.insert(remainder.begin(), L'\\');
                            }
                        }

                        unc = remoteName + remainder;
                    }
                }
            }
        }

        if (!startsWithWsl(unc)) {
            return std::nullopt;
        }
    }

    // Parse \\wsl$\<distro>\rest
    size_t pos = 2;
    while (pos < unc.size() && unc[pos] == L'\\') {
        ++pos;
    }

    const size_t serverEnd = unc.find(L'\\', pos);
    if (serverEnd == std::wstring::npos) {
        return std::nullopt;
    }

    const size_t distroStart = serverEnd + 1;
    const size_t distroEnd = unc.find(L'\\', distroStart);
    if (distroEnd == std::wstring::npos) {
        return std::nullopt;
    }

    const std::wstring distroW = unc.substr(distroStart, distroEnd - distroStart);
    const std::wstring restW = unc.substr(distroEnd);

    std::string rest;
    rest.reserve(restW.size());
    for (const wchar_t ch : restW) {
        rest.push_back(ch == L'\\' ? '/' : (char)ch);
    }

    if (rest.empty() || rest[0] != '/') {
        rest.insert(rest.begin(), '/');
    }

    return WslMapping{
        .distro = std::string(distroW.begin(), distroW.end()),
        .wslRoot = rest,
        .uncRoot = std::filesystem::path(unc)
    };
#else
    (void)path;
    return std::nullopt;
#endif
}

bool enumerateFiles(const WslMapping& mapping, std::vector<std::pair<std::filesystem::path, std::string>>& outFiles) {
#ifdef _WIN32
    auto escapeSingleQuotes = [](std::string_view s){
        std::string out;
        out.reserve(s.size() + 8);

        for (const char c : s) {
            out += (c == '\'') ? "'\\''" : std::string(1, c);
        }

        return out;
    };

    const std::string rootEsc = escapeSingleQuotes(mapping.wslRoot);
    const std::string cmd = fmt::format("wsl.exe -d {} -- sh -c \"find -L '{}' -type f -print0\"", mapping.distro, rootEsc);

    FILE* pipe = _popen(cmd.c_str(), "rb");
    if (!pipe) {
        spdlog::warn("WSLUtil: failed to start wsl.exe find for {}", mapping.wslRoot);
        return false;
    }

    std::string acc;
    std::vector<char> buffer(1 << 16);
    while (!feof(pipe)) {
        const size_t n = fread(buffer.data(), 1, buffer.size(), pipe);
        if (n > 0) {
            acc.append(buffer.data(), n);
        }
    }

    _pclose(pipe);

    std::istringstream files(std::move(acc));
    std::string file;

    while (std::getline(files, file, '\0')) {
        if (file.empty()) {
            continue;
        }

        std::string rel = file;
        if (rel.starts_with(mapping.wslRoot)) {
            rel.erase(0, mapping.wslRoot.size());

            if (!rel.empty() && rel.front() == '/') {
                rel.erase(0, 1);
            }
        }

        std::string relWin;
        std::ranges::replace_copy(rel, std::back_inserter(relWin), '/', '\\');
        
        outFiles.emplace_back(mapping.uncRoot / relWin, rel);
    }

    return true;
#else
    (void)mapping; (void)outFiles; return false;
#endif
}

bool isRunningUnderWSL() {
#ifdef _WIN32
    return false;
#else
    try {
        std::ifstream osrelease("/proc/sys/kernel/osrelease");
        if (!osrelease.is_open()) {
            return false;
        }

        std::string line; std::getline(osrelease, line);
        return line.contains("microsoft") || line.contains("WSL");
    } catch (...) {
        return false;
    }
#endif
}

} // namespace WSLUtil
