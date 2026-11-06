#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <sstream>

namespace sh3e::patches
{
namespace
{
    volatile LONG g_originalProcessAffinityMask = 0;
}

bool ApplyCpuAffinity(std::uint32_t affinityMask)
{
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
    {
        log::Write("WARN", "[CPU affinity] Could not read the available processor mask; leaving Windows scheduling unchanged.");
        return false;
    }

    const auto originalMask = static_cast<DWORD_PTR>(InterlockedCompareExchange(
        &g_originalProcessAffinityMask, static_cast<LONG>(processMask), 0));
    const auto defaultMask = originalMask != 0 ? originalMask : processMask;

    if (affinityMask == 0)
    {
        if (processMask != defaultMask && !SetProcessAffinityMask(GetCurrentProcess(), defaultMask))
        {
            const auto error = GetLastError();
            log::Write("WARN", "[CPU affinity] Could not restore the original process mask; error=" +
                std::to_string(error) + ".");
            return false;
        }

        log::Write("INFO", "[CPU affinity] Using the original Windows scheduling mask=" + detail::Hex(defaultMask));
        return true;
    }

    const auto requestedMask = static_cast<DWORD_PTR>(affinityMask);
    if (requestedMask == 0 || (requestedMask & systemMask) != requestedMask)
    {
        log::Write("WARN", "[CPU affinity] Configured mask " + detail::Hex(affinityMask) +
            " includes unavailable processors; leaving Windows scheduling unchanged.");
        return false;
    }

    if (!SetProcessAffinityMask(GetCurrentProcess(), requestedMask))
    {
        const auto error = GetLastError();
        log::Write("WARN", "[CPU affinity] Windows rejected mask " + detail::Hex(affinityMask) +
            "; error=" + std::to_string(error) + ". Leaving the current affinity unchanged.");
        return false;
    }

    std::ostringstream message;
    message << "[CPU affinity] Applied process mask " << detail::Hex(affinityMask)
            << " (previous mask " << detail::Hex(processMask) << ").";
    log::Write("INFO", message.str());
    return true;
}
}
