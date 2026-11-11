#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sh3e::patches
{
    bool PreparePS2CostumeArchiveRedirects(const std::wstring& gameDirectory,
        std::vector<std::pair<std::wstring, std::wstring>>& redirects);
    bool InstallPS2CostumeResourcePaths(void* executable);
    bool InstallPS2CostumePasswordTable(void* executable);
}
