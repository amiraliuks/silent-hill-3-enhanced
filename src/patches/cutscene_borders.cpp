#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <cstdint>

namespace sh3e::patches
{
namespace
{
    float g_zeroFloat = 0.0f;
}

bool InstallCutsceneBordersPatch(HMODULE executable)
{
    const auto pattern = BytePattern::Parse(
        "D9 05 ?? ?? ?? ?? C7 05 ?? ?? ?? ?? 00 00 00 00 D8 C9");
    if (!pattern)
        return false;

    const auto matches = FindPattern(executable, *pattern);
    if (matches.size() != 1)
    {
        log::Write("WARN", "[Cutscene borders] Signature must match exactly once; found " + std::to_string(matches.size()));
        return false;
    }

    if (!detail::RedirectFloatOperand(matches.front(), &g_zeroFloat))
    {
        log::Write("ERROR", "[Cutscene borders] Could not remove the cutscene border offsets.");
        return false;
    }

    log::Write("INFO", "[Cutscene borders] Removal patch installed at sh3.exe+" + detail::Hex(matches.front().rva));
    return true;
}
}
