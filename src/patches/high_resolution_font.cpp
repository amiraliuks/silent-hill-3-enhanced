#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sh3e::patches
{
namespace
{
    constexpr std::uintptr_t kFontAtlasPointerAddress = 0x070CA844;
    constexpr std::uintptr_t kFontStyleIdAddress = 0x070C9354;
    constexpr std::uintptr_t kFontLanguageIdAddress = 0x00721708;
    constexpr std::size_t kUploadFunctionLength = 5;
    constexpr int kNativeAtlasWidth = 512;
    constexpr int kNativeAtlasHeight = 512;
    constexpr int kGlyphColumns = 24;
    constexpr int kNativeCellWidth = 21;
    constexpr int kNativeCellHeight = 31;
    constexpr int kGlyphSlots = 384;
    constexpr std::uint32_t kFontPackCoverageScale = 4;
    constexpr char kFontPackMagic[8] = {'S', 'H', '3', 'E', 'F', 'N', 'T', '1'};

#pragma pack(push, 1)
    struct FontPackHeader
    {
        char magic[8];
        std::uint32_t version;
        std::uint32_t coverageScale;
        std::uint32_t glyphCount;
    };

    struct FontGlyphRecordV1
    {
        std::uint16_t glyphId;
        std::uint8_t fontStyleId;
        std::uint8_t width;
        std::uint8_t height;
        std::uint8_t reserved[3];
    };

    struct FontGlyphRecordV2
    {
        std::uint16_t glyphId;
        std::uint8_t fontStyleId;
        std::uint8_t languageBankId;
        std::uint8_t width;
        std::uint8_t height;
        std::uint8_t reserved[2];
    };
#pragma pack(pop)

    struct FontGlyph
    {
        std::uint8_t width = 0;
        std::uint8_t height = 0;
        std::vector<std::uint8_t> coverage;
    };

    struct UploadedGlyph
    {
        std::uint32_t glyphId = 0;
        std::uint32_t languageBankId = 0;
        std::uint32_t fontStyleId = 0;
        bool valid = false;
        std::array<std::uint32_t, kNativeCellWidth * kNativeCellHeight> nativePixels{};
    };

    struct D3DLockedRect
    {
        INT pitch;
        void* bits;
    };

    using CreateTextureFunction = HRESULT(__stdcall*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
    using UploadGlyphFunction = int(__cdecl*)(std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t, const void*, std::int32_t, std::uintptr_t);
    using TextureLockRectFunction = HRESULT(__stdcall*)(void*, UINT, D3DLockedRect*, const RECT*, DWORD);
    using TextureUnlockRectFunction = HRESULT(__stdcall*)(void*, UINT);

    std::unordered_map<std::uint32_t, FontGlyph> g_glyphs;
    CreateTextureFunction g_originalCreateTexture = nullptr;
    UploadGlyphFunction g_originalUploadGlyph = nullptr;
    void* g_d3dDevice = nullptr;
    std::uint8_t g_requestedScale = 0;
    bool g_packLoaded = false;
    bool g_createTextureHooked = false;
    bool g_uploadHookInstalled = false;
    std::array<LONG, 36> g_lookupLogged{};
    std::array<UploadedGlyph, kGlyphSlots> g_uploadedGlyphs{};
    bool g_overlapLogged = false;

    std::uint32_t MakeGlyphKey(std::uint32_t languageBankId, std::uint32_t fontStyleId,
        std::uint32_t glyphId)
    {
        return ((languageBankId & 0xFFu) << 24) | ((fontStyleId & 0xFFu) << 16) | (glyphId & 0xFFFFu);
    }

    void LogGlyphLookup(std::uint32_t languageBankId, std::uint32_t fontStyleId,
        std::uint32_t glyphId, const char* source, std::uint32_t recordBankId)
    {
        const auto bankSlot = languageBankId <= 2 ? languageBankId : 3u;
        const auto styleSlot = fontStyleId <= 1 ? fontStyleId : 2u;
        const auto sourceSlot = std::strcmp(source, "bank") == 0 ? 0u :
            (std::strcmp(source, "shared") == 0 ? 1u : 2u);
        const auto logIndex = bankSlot * 9u + styleSlot * 3u + sourceSlot;
        if (InterlockedCompareExchange(&g_lookupLogged[logIndex], 1, 0) != 0)
            return;

        log::Write("INFO", "[Fonts] Glyph lookup: active bank=" + std::to_string(languageBankId) +
            ", style=" + std::to_string(fontStyleId) + ", glyph=" + std::to_string(glyphId) +
            ", replacement=" + source + (std::strcmp(source, "missing") == 0 ? " (original bitmap used)." :
                " from bank=" + std::to_string(recordBankId) + "."));
    }

    bool ReadFontPack(const std::wstring& path, std::uint8_t requestedScale)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            log::Write("INFO", "[Fonts] No SH3 Enhanced font pack found; retaining the original game font.");
            return false;
        }

        const auto end = input.tellg();
        if (end < static_cast<std::streamoff>(sizeof(FontPackHeader)) ||
            end > static_cast<std::streamoff>(256 * 1024 * 1024))
        {
            log::Write("WARN", "[Fonts] Font pack size is invalid; retaining the original game font.");
            return false;
        }

        input.seekg(0, std::ios::beg);
        FontPackHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input || std::memcmp(header.magic, kFontPackMagic, sizeof(kFontPackMagic)) != 0 ||
            (header.version != 1 && header.version != 2) || header.coverageScale != kFontPackCoverageScale ||
            header.glyphCount > 65536)
        {
            log::Write("WARN", "[Fonts] Font pack header is unsupported or corrupt; retaining the original game font.");
            return false;
        }

        std::unordered_map<std::uint32_t, FontGlyph> loadedGlyphs;
        loadedGlyphs.reserve(header.glyphCount);
        for (std::uint32_t index = 0; index < header.glyphCount; ++index)
        {
            std::uint16_t glyphId = 0;
            std::uint8_t fontStyleId = 0;
            std::uint8_t languageBankId = 0xFF;
            std::uint8_t width = 0;
            std::uint8_t height = 0;
            if (header.version == 1)
            {
                FontGlyphRecordV1 record{};
                input.read(reinterpret_cast<char*>(&record), sizeof(record));
                glyphId = record.glyphId;
                fontStyleId = record.fontStyleId;
                width = record.width;
                height = record.height;
            }
            else
            {
                FontGlyphRecordV2 record{};
                input.read(reinterpret_cast<char*>(&record), sizeof(record));
                glyphId = record.glyphId;
                fontStyleId = record.fontStyleId;
                languageBankId = record.languageBankId;
                width = record.width;
                height = record.height;
            }

            if (!input || fontStyleId > 1 ||
                (header.version == 2 && languageBankId > 2 && languageBankId != 0xFF) ||
                width == 0 || width > kNativeCellWidth || height == 0 || height > 30)
            {
                log::Write("WARN", "[Fonts] Font pack contains an invalid glyph record; retaining the original game font.");
                return false;
            }

            const auto pixelCount = static_cast<std::size_t>(width) * height *
                kFontPackCoverageScale * kFontPackCoverageScale;
            FontGlyph glyph;
            glyph.width = width;
            glyph.height = height;
            glyph.coverage.resize(pixelCount);
            input.read(reinterpret_cast<char*>(glyph.coverage.data()), static_cast<std::streamsize>(pixelCount));
            if (!input)
            {
                log::Write("WARN", "[Fonts] Font pack ended inside a glyph; retaining the original game font.");
                return false;
            }

            loadedGlyphs[MakeGlyphKey(languageBankId, fontStyleId, glyphId)] = std::move(glyph);
        }

        if (input.peek() != std::char_traits<char>::eof())
        {
            log::Write("WARN", "[Fonts] Font pack has unexpected trailing data; retaining the original game font.");
            return false;
        }

        g_glyphs = std::move(loadedGlyphs);
        g_packLoaded = true;
        log::Write("INFO", "[Fonts] Loaded " + std::to_string(g_glyphs.size()) +
            " glyphs from the SH3 Enhanced font pack at " + std::to_string(requestedScale) + "x output scale.");
        for (std::uint32_t bank = 0; bank <= 2; ++bank)
        {
            for (std::uint32_t style = 0; style <= 1; ++style)
            {
                std::size_t count = 0;
                for (const auto& entry : g_glyphs)
                {
                    if (((entry.first >> 24) & 0xFFu) == bank &&
                        ((entry.first >> 16) & 0xFFu) == style)
                        ++count;
                }
                if (count != 0)
                {
                    static constexpr const char* bankNames[] = { "Korean", "Japanese/Western", "Chinese" };
                    static constexpr const char* styleNames[] = { "normal", "small" };
                    log::Write("INFO", std::string("[Fonts] Pack records: ") + bankNames[bank] + " " +
                        styleNames[style] + "=" + std::to_string(count) + ".");
                }
            }
        }
        return true;
    }

    bool ReplaceVtableEntry(void* instance, std::size_t byteOffset, void* replacement, void** original)
    {
        if (!instance || (byteOffset % sizeof(void*)) != 0)
            return false;

        auto** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable)
            return false;

        auto** entry = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtable) + byteOffset);
        if (*entry == replacement)
            return true;

        DWORD oldProtection = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtection))
            return false;

        if (original)
            *original = *entry;
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(entry), replacement);
        DWORD ignored = 0;
        return VirtualProtect(entry, sizeof(*entry), oldProtection, &ignored) != FALSE;
    }

    HRESULT __stdcall FontCreateTexture(void* device, UINT width, UINT height, UINT levels,
        DWORD usage, DWORD format, DWORD pool, void** texture)
    {
        const bool isFontAtlas = texture == reinterpret_cast<void**>(kFontAtlasPointerAddress) &&
            width == kNativeAtlasWidth && height == kNativeAtlasHeight &&
            levels == 1 && format == 21 && g_packLoaded;
        if (isFontAtlas)
        {
            width *= g_requestedScale;
            height *= g_requestedScale;
        }

        const auto result = g_originalCreateTexture
            ? g_originalCreateTexture(device, width, height, levels, usage, format, pool, texture)
            : E_FAIL;
        if (isFontAtlas && SUCCEEDED(result))
        {
            g_uploadedGlyphs = {};
            g_overlapLogged = false;
        }
        return result;
    }

    bool LockAtlas(void* texture, D3DLockedRect& locked, TextureLockRectFunction& lockRect,
        TextureUnlockRectFunction& unlockRect)
    {
        if (!texture)
            return false;

        auto** vtable = *reinterpret_cast<void***>(texture);
        if (!vtable)
            return false;

        lockRect = reinterpret_cast<TextureLockRectFunction>(vtable[16]);
        unlockRect = reinterpret_cast<TextureUnlockRectFunction>(vtable[17]);
        return lockRect && unlockRect && SUCCEEDED(lockRect(texture, 0, &locked, nullptr, 0)) && locked.bits;
    }

    bool CaptureNativeGlyph(std::int32_t slot, UploadedGlyph& uploaded)
    {
        if (slot < 0 || slot >= kGlyphSlots)
            return false;

        auto* texture = *reinterpret_cast<void**>(kFontAtlasPointerAddress);
        D3DLockedRect locked{};
        TextureLockRectFunction lockRect = nullptr;
        TextureUnlockRectFunction unlockRect = nullptr;
        if (!LockAtlas(texture, locked, lockRect, unlockRect))
            return false;

        const int sourceX = (slot % kGlyphColumns) * kNativeCellWidth;
        const int sourceY = (slot / kGlyphColumns) * kNativeCellHeight;
        auto* pixels = static_cast<std::uint8_t*>(locked.bits);
        const auto pitch = static_cast<std::size_t>(locked.pitch);
        for (int y = 0; y < kNativeCellHeight; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(pixels +
                static_cast<std::size_t>(sourceY + y) * pitch) + sourceX;
            std::copy_n(row, kNativeCellWidth, uploaded.nativePixels.data() + y * kNativeCellWidth);
        }
        unlockRect(texture, 0);
        return true;
    }

    void ApplyGlyphToAtlas(std::int32_t slot)
    {
        if (slot < 0 || slot >= kGlyphSlots)
            return;
        const auto& uploaded = g_uploadedGlyphs[slot];
        if (!uploaded.valid)
            return;

        auto* texture = *reinterpret_cast<void**>(kFontAtlasPointerAddress);
        D3DLockedRect locked{};
        TextureLockRectFunction lockRect = nullptr;
        TextureUnlockRectFunction unlockRect = nullptr;
        if (!LockAtlas(texture, locked, lockRect, unlockRect))
            return;

        const int scale = g_requestedScale;
        const int sourceX = (slot % kGlyphColumns) * kNativeCellWidth;
        const int sourceY = (slot / kGlyphColumns) * kNativeCellHeight;
        const int targetX = sourceX * scale;
        const int targetY = sourceY * scale;
        auto* pixels = static_cast<std::uint8_t*>(locked.bits);
        const auto pitch = static_cast<std::size_t>(locked.pitch);

        const int targetWidth = kNativeCellWidth * scale;
        const int targetHeight = kNativeCellHeight * scale;
        for (int y = 0; y < targetHeight; ++y)
        {
            auto* row = reinterpret_cast<std::uint32_t*>(pixels +
                static_cast<std::size_t>(targetY + y) * pitch) + targetX;
            std::fill_n(row, targetWidth, 0u);
        }

        auto found = uploaded.languageBankId <= 2
            ? g_glyphs.find(MakeGlyphKey(uploaded.languageBankId, uploaded.fontStyleId, uploaded.glyphId))
            : g_glyphs.end();
        const bool exactBankMatch = found != g_glyphs.end();
        if (found == g_glyphs.end())
            found = g_glyphs.find(MakeGlyphKey(0xFFu, uploaded.fontStyleId, uploaded.glyphId));
        if (found != g_glyphs.end())
        {
            LogGlyphLookup(uploaded.languageBankId, uploaded.fontStyleId, uploaded.glyphId,
                exactBankMatch ? "bank" : "shared", (found->first >> 24) & 0xFFu);
            const auto& glyph = found->second;
            for (int y = 0; y < glyph.height * scale; ++y)
            {
                auto* row = reinterpret_cast<std::uint32_t*>(pixels +
                    static_cast<std::size_t>(targetY + y) * pitch) + targetX;
                for (int x = 0; x < glyph.width * scale; ++x)
                {
                    std::uint8_t coverage = 0;
                    if (scale == static_cast<int>(kFontPackCoverageScale))
                        coverage = glyph.coverage[static_cast<std::size_t>(y) * glyph.width * scale + x];
                    else
                    {
                        unsigned sum = 0;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                            {
                                const auto sourceIndex = static_cast<std::size_t>(y * 2 + dy) *
                                    glyph.width * kFontPackCoverageScale + x * 2 + dx;
                                sum += glyph.coverage[sourceIndex];
                            }
                        coverage = static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                    row[x] = 0x00FFFFFFu | (static_cast<std::uint32_t>(coverage) << 24);
                }
            }
        }
        else
        {
            LogGlyphLookup(uploaded.languageBankId, uploaded.fontStyleId, uploaded.glyphId, "missing", 0xFFu);
            for (int y = 0; y < kNativeCellHeight; ++y)
            {
                auto* row = reinterpret_cast<std::uint32_t*>(pixels +
                    static_cast<std::size_t>(targetY + y * scale) * pitch) + targetX;
                for (int x = 0; x < kNativeCellWidth; ++x)
                {
                    const auto pixel = uploaded.nativePixels[static_cast<std::size_t>(y) * kNativeCellWidth + x];
                    std::fill_n(row + x * scale, scale, pixel);
                }
                for (int repeat = 1; repeat < scale; ++repeat)
                    std::copy_n(row, targetWidth, reinterpret_cast<std::uint32_t*>(pixels +
                        static_cast<std::size_t>(targetY + y * scale + repeat) * pitch) + targetX);
            }
        }

        unlockRect(texture, 0);
    }

    bool ScaledCellOverlapsNativeCell(std::int32_t scaledSlot, std::int32_t nativeSlot)
    {
        const int scale = g_requestedScale;
        const int scaledX = (scaledSlot % kGlyphColumns) * kNativeCellWidth * scale;
        const int scaledY = (scaledSlot / kGlyphColumns) * kNativeCellHeight * scale;
        const int nativeX = (nativeSlot % kGlyphColumns) * kNativeCellWidth;
        const int nativeY = (nativeSlot / kGlyphColumns) * kNativeCellHeight;
        return scaledX < nativeX + kNativeCellWidth &&
            scaledX + kNativeCellWidth * scale > nativeX &&
            scaledY < nativeY + kNativeCellHeight &&
            scaledY + kNativeCellHeight * scale > nativeY;
    }

    int __cdecl FontUploadGlyph(std::uint32_t reserved, std::uint32_t width,
        std::uint32_t glyphId, std::uint32_t fontIndex, const void* fontSection, std::int32_t slot,
        std::uintptr_t callerStackContext)
    {
        const auto result = g_originalUploadGlyph
            ? g_originalUploadGlyph(reserved, width, glyphId, fontIndex, fontSection, slot, callerStackContext)
            : 0;
        if (result >= 0 && g_packLoaded && slot >= 0 && slot < kGlyphSlots && CaptureNativeGlyph(slot, g_uploadedGlyphs[slot]))
        {
            auto& uploaded = g_uploadedGlyphs[slot];
            uploaded.glyphId = glyphId;
            uploaded.languageBankId = *reinterpret_cast<const volatile std::uint32_t*>(kFontLanguageIdAddress);
            uploaded.fontStyleId = *reinterpret_cast<const volatile std::uint32_t*>(kFontStyleIdAddress);
            uploaded.valid = true;

            std::array<std::int32_t, kGlyphSlots> repaintSlots{};
            std::size_t repaintCount = 0;
            for (std::int32_t candidate = 0; candidate < kGlyphSlots; ++candidate)
            {
                if (g_uploadedGlyphs[candidate].valid &&
                    (candidate == slot || ScaledCellOverlapsNativeCell(candidate, slot)))
                    repaintSlots[repaintCount++] = candidate;
            }

            for (std::size_t index = 0; index < repaintCount; ++index)
                ApplyGlyphToAtlas(repaintSlots[index]);

            if (!g_overlapLogged && repaintCount > 1)
            {
                g_overlapLogged = true;
                log::Write("INFO", "[Fonts] Repainted overlapping scaled glyph cells after a native atlas upload.");
            }
        }
        return result;
    }

    bool InstallGlyphUploadHook(HMODULE executable)
    {
        const auto pattern = sh3e::BytePattern::Parse("83 EC 24 53 55 8B 6C 24 44");
        if (!pattern)
            return false;

        const auto matches = sh3e::FindPattern(executable, *pattern);
        if (matches.size() != 1)
        {
            log::Write("WARN", "[Fonts] Could not uniquely identify the game's glyph uploader; original font remains active.");
            return false;
        }

        auto* target = reinterpret_cast<std::uint8_t*>(matches.front().address);
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 16,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
            return false;

        std::memcpy(trampoline, target, kUploadFunctionLength);
        if (!detail::EncodeRelativeJump(trampoline + kUploadFunctionLength,
                reinterpret_cast<std::uintptr_t>(trampoline + kUploadFunctionLength),
                target + kUploadFunctionLength))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), trampoline, kUploadFunctionLength + 5);

        std::array<std::uint8_t, kUploadFunctionLength> jump{};
        if (!detail::EncodeRelativeJump(jump.data(), reinterpret_cast<std::uintptr_t>(target),
                reinterpret_cast<const void*>(&FontUploadGlyph)) ||
            !sh3e::WriteMemory(matches.front().address, jump.data(), jump.size()))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        g_originalUploadGlyph = reinterpret_cast<UploadGlyphFunction>(trampoline);
        g_uploadHookInstalled = true;
        log::Write("INFO", "[Fonts] Installed the SH3 Enhanced glyph upload hook at sh3.exe+" + detail::Hex(matches.front().rva) + ".");
        return true;
    }
}

bool InstallHighResolutionFontRuntime(HMODULE executable, const std::wstring& gameDirectory, int scale)
{
    if (scale == 0)
    {
        log::Write("INFO", "[Fonts] FontScale=0; using the original game font path.");
        return true;
    }

    if (!executable || (scale != 2 && scale != 4))
        return false;

    const auto packPath = gameDirectory + L"\\SH3 Enhanced Data\\Fonts\\fontdata.sh3f";
    if (!ReadFontPack(packPath, static_cast<std::uint8_t>(scale)))
        return true;

    g_requestedScale = static_cast<std::uint8_t>(scale);
    if (!InstallGlyphUploadHook(executable))
    {
        g_glyphs.clear();
        g_packLoaded = false;
        return false;
    }

    // Startup can create the D3D8 device before the graphics initializer loads
    // the font pack. In that order the device hook's first attempt is a no-op;
    // attach to the already-created device now so the atlas is enlarged before
    // the game uploads its first glyphs.
    if (g_d3dDevice && !HookHighResolutionFontDevice(g_d3dDevice))
    {
        log::Write("WARN", "[Fonts] Could not attach the font-atlas hook to the existing Direct3D 8 device; original fonts remain active.");
        g_glyphs.clear();
        g_packLoaded = false;
        g_requestedScale = 0;
        return false;
    }
    return true;
}

bool HookHighResolutionFontDevice(void* device)
{
    if (!device)
        return false;
    g_d3dDevice = device;
    if (!g_packLoaded)
        return true;

    if (g_createTextureHooked)
        return true;

    void* original = nullptr;
    if (!ReplaceVtableEntry(device, 0x50, reinterpret_cast<void*>(&FontCreateTexture), &original) || !original)
        return false;

    g_originalCreateTexture = reinterpret_cast<CreateTextureFunction>(original);
    g_createTextureHooked = true;
    log::Write("INFO", "[Fonts] Hooked Direct3D 8 font-atlas creation for " +
        std::to_string(g_requestedScale) + "x glyph coverage.");
    return true;
}
}
