#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sh3e::patches
{
    // Returns true when preparation succeeded. A missing pack directory is a
    // normal, silently disabled state; on success, redirects contains the AFS
    // request suffixes and rebuilt cache paths to install in the file hook.
    bool PrepareAudioEnhancementPack(const std::wstring& gameDirectory,
        std::vector<std::pair<std::wstring, std::wstring>>& redirects);
}
