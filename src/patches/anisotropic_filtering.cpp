#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sh3e::patches
{
namespace
{
    using GetDeviceCapsFunction = HRESULT(WINAPI*)(void*, void*);
    using GetTextureStageStateFunction = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD*);
    using SetTextureStageStateFunction = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD);

    constexpr std::size_t kGetDeviceCapsVtableOffset = 0x1C;
    // IDirect3DDevice8::SetTextureStageState is vtable slot 63 (3 IUnknown
    // slots plus 60 device methods), immediately before ValidateDevice.
    constexpr std::size_t kSetTextureStageStateVtableOffset = 0xFC;
    constexpr std::size_t kGetTextureStageStateVtableOffset = 0xF8;
    constexpr DWORD kD3dRasterCapsAnisotropy = 0x00020000;
    constexpr DWORD kD3dTextureFilterCapsMinAnisotropic = 0x00000400;
    constexpr DWORD kD3dTextureStageStateMinFilter = 17;
    constexpr DWORD kD3dTextureStageStateMaxAnisotropy = 21;
    constexpr DWORD kD3dTextureFilterAnisotropic = 3;
    constexpr DWORD kD3dTextureFilterLinear = 2;
    constexpr DWORD kMaximumD3d8TextureStages = 8;

    SetTextureStageStateFunction g_originalSetTextureStageState = nullptr;
    DWORD g_maxAnisotropy = 1;
    DWORD g_textureStageCount = 0;
    DWORD g_originalMinFilter[kMaximumD3d8TextureStages]{};
    DWORD g_originalMaxAnisotropy[kMaximumD3d8TextureStages]{};
    volatile LONG g_anisotropicEnabled = 0;
    void* g_activeDevice = nullptr;
    volatile LONG g_loggedStatus = 0;

    bool ReplaceVtableEntry(void* instance, std::size_t byteOffset, void* replacement, void** original)
    {
        if (!instance || (byteOffset % sizeof(void*)) != 0)
            return false;

        auto** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable)
            return false;

        auto** slot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtable) + byteOffset);
        if (*slot == replacement)
            return true;

        DWORD oldProtection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection))
            return false;

        if (original)
            *original = *slot;

        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), replacement);
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
        return true;
    }

    HRESULT WINAPI AnisotropicSetTextureStageState(void* device, DWORD stage, DWORD state, DWORD value)
    {
        if (stage < g_textureStageCount)
        {
            const bool enabled = InterlockedCompareExchange(&g_anisotropicEnabled, 0, 0) != 0;
            if (state == kD3dTextureStageStateMinFilter)
            {
                g_originalMinFilter[stage] = value;
                if (enabled) value = kD3dTextureFilterAnisotropic;
            }
            else if (state == kD3dTextureStageStateMaxAnisotropy)
            {
                g_originalMaxAnisotropy[stage] = value;
                if (enabled) value = g_maxAnisotropy;
            }
        }

        return g_originalSetTextureStageState
            ? g_originalSetTextureStageState(device, stage, state, value)
            : E_FAIL;
    }

    bool SetAnisotropicStates(void* device)
    {
        if (!device || !g_originalSetTextureStageState || g_textureStageCount == 0)
            return false;

        const bool enabled = InterlockedCompareExchange(&g_anisotropicEnabled, 0, 0) != 0;
        if (enabled && g_maxAnisotropy <= 1)
            return false;

        bool applied = true;
        for (DWORD stage = 0; stage < g_textureStageCount; ++stage)
        {
            const auto maxResult = g_originalSetTextureStageState(
                device, stage, kD3dTextureStageStateMaxAnisotropy,
                enabled ? g_maxAnisotropy : g_originalMaxAnisotropy[stage]);
            const auto minResult = g_originalSetTextureStageState(
                device, stage, kD3dTextureStageStateMinFilter,
                enabled ? kD3dTextureFilterAnisotropic : g_originalMinFilter[stage]);
            applied = applied && SUCCEEDED(maxResult) && SUCCEEDED(minResult);
        }

        return applied;
    }
}

bool InstallAnisotropicFiltering(void* device, bool enabled)
{
    if (!device)
        return false;

    auto** vtable = *reinterpret_cast<void***>(device);
    if (!vtable)
        return false;

    const auto getCaps = reinterpret_cast<GetDeviceCapsFunction>(
        *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtable) + kGetDeviceCapsVtableOffset));

    if (!getCaps)
        return false;

    // D3DCAPS8 has 53 DWORD-sized fields. These indices follow the Direct3D 8 layout.
    std::array<DWORD, 53> caps{};
    if (FAILED(getCaps(device, caps.data())))
    {
        log::Write("WARN", "[Anisotropic filtering] Could not read Direct3D device capabilities.");
        return false;
    }

    constexpr std::size_t rasterCapsIndex = 9;
    constexpr std::size_t textureFilterCapsIndex = 16;
    constexpr std::size_t maxAnisotropyIndex = 27;
    constexpr std::size_t maxSimultaneousTexturesIndex = 38;
    const auto rasterCaps = caps[rasterCapsIndex];
    const auto textureFilterCaps = caps[textureFilterCapsIndex];
    const auto maxAnisotropy = caps[maxAnisotropyIndex];
    const auto simultaneousTextures = caps[maxSimultaneousTexturesIndex];

    if ((rasterCaps & kD3dRasterCapsAnisotropy) == 0 || (textureFilterCaps & kD3dTextureFilterCapsMinAnisotropic) == 0 || maxAnisotropy <= 1)
    {
        if (InterlockedCompareExchange(&g_loggedStatus, 1, 0) == 0)
            log::Write("INFO", "[Anisotropic filtering] The active Direct3D device does not report anisotropic minification support.");

        return false;
    }

    g_maxAnisotropy = maxAnisotropy;
    g_textureStageCount = simultaneousTextures < kMaximumD3d8TextureStages ? simultaneousTextures : kMaximumD3d8TextureStages;
    if (g_textureStageCount == 0)
    {
        log::Write("WARN", "[Anisotropic filtering] The Direct3D device reports no simultaneous texture stages.");
        return false;
    }

    const auto getTextureStageState = reinterpret_cast<GetTextureStageStateFunction>(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtable) + kGetTextureStageStateVtableOffset));
    if (!getTextureStageState)
        return false;

    g_activeDevice = device;
    for (DWORD stage = 0; stage < g_textureStageCount; ++stage)
    {
        if (FAILED(getTextureStageState(device, stage, kD3dTextureStageStateMinFilter, &g_originalMinFilter[stage])) ||
            FAILED(getTextureStageState(device, stage, kD3dTextureStageStateMaxAnisotropy, &g_originalMaxAnisotropy[stage])))
        {
            log::Write("WARN", "[Anisotropic filtering] Could not capture the device's original sampler states.");
            return false;
        }
    }

    void* original = nullptr;
    if (!ReplaceVtableEntry(device, kSetTextureStageStateVtableOffset, reinterpret_cast<void*>(&AnisotropicSetTextureStageState), &original))
    {
        log::Write("WARN", "[Anisotropic filtering] Could not hook SetTextureStageState.");
        return false;
    }

    if (original)
        g_originalSetTextureStageState = reinterpret_cast<SetTextureStageStateFunction>(original);

    if (!g_originalSetTextureStageState)
        return false;

    InterlockedExchange(&g_anisotropicEnabled, enabled ? 1 : 0);

    if (!SetAnisotropicStates(device))
    {
        log::Write("WARN", "[Anisotropic filtering] The device rejected one or more anisotropic sampler states.");
        return false;
    }

    if (enabled && InterlockedCompareExchange(&g_loggedStatus, 1, 0) == 0)
        log::Write("INFO", "[Anisotropic filtering] Enabled at " + std::to_string(g_maxAnisotropy) + "x across " + std::to_string(g_textureStageCount) + " texture stages.");

    return true;
}

bool SetAnisotropicFilteringLive(bool enabled)
{
    if (!g_activeDevice || !g_originalSetTextureStageState ||
        (enabled && g_maxAnisotropy <= 1))
        return false;

    InterlockedExchange(&g_anisotropicEnabled, enabled ? 1 : 0);
    const bool applied = SetAnisotropicStates(g_activeDevice);

    if (applied)
        log::Write("INFO", std::string("[Anisotropic filtering] Applied live: ") + (enabled ? "enabled." : "disabled."));

    return applied;
}

void ReapplyAnisotropicFiltering(void* device)
{
    if (device)
        g_activeDevice = device;

    if (SetAnisotropicStates(device))
        log::Write("INFO", "[Anisotropic filtering] Sampler states restored after Direct3D device reset.");
}
}