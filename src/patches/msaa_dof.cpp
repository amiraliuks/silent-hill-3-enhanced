#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <string>

namespace sh3e::patches
{
namespace
{
    volatile LONG g_disableMsaaForDof = 1;
    volatile LONG g_loggedMsaaClamp = 0;
}

void SetMsaaDoFFixEnabled(bool enabled)
{
    InterlockedExchange(&g_disableMsaaForDof, enabled ? 1 : 0);
    InterlockedExchange(&g_loggedMsaaClamp, 0);
}

bool EnforceNoMultisampling(DWORD* multiSampleType)
{
    if (!multiSampleType || InterlockedCompareExchange(&g_disableMsaaForDof, 0, 0) == 0)
        return false;

    const DWORD requestedType = *multiSampleType;
    if (requestedType == 0)
        return true;

    *multiSampleType = 0; // D3DMULTISAMPLE_NONE
    if (InterlockedCompareExchange(&g_loggedMsaaClamp, 1, 0) == 0)
    {
        log::Write("INFO", "[MSAA / DoF] Multisample type " + std::to_string(requestedType) +
            " changed to NONE during Direct3D 8 device creation/reset.");
    }

    return true;
}
}
