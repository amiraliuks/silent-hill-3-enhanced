#include "patches.h"
#include "patch_utils.h"
#include "../logger.h"
#include "../resources/resource_ids.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sh3e::patches
{
namespace
{
    constexpr std::uintptr_t kBindingLabelRendererRva = 0x1EE710;
    constexpr std::size_t kPresentVtableOffset = 15 * sizeof(void*);
    struct BindingCallSite
    {
        std::uintptr_t rva;
        std::array<std::uint8_t, 5> expected;
    };
    constexpr std::array<BindingCallSite, 4> kBindingLabelCalls = {{
        { 0x1ED64F, { 0xE8, 0xBC, 0x10, 0x00, 0x00 } },
        { 0x1EE185, { 0xE8, 0x86, 0x05, 0x00, 0x00 } },
        { 0x1EE287, { 0xE8, 0x84, 0x04, 0x00, 0x00 } },
        { 0x1EE35E, { 0xE8, 0xAD, 0x03, 0x00, 0x00 } },
    }};

    // SH3 pushes format, label text, state, y, then x, so the string is argument four.
    using BindingLabelFunction = void(__cdecl*)(int, int, int, const char*, int, int, int);
    using PresentFunction = HRESULT(WINAPI*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using CreateStateBlockFunction = HRESULT(WINAPI*)(void*, DWORD, DWORD*);
    using ApplyStateBlockFunction = HRESULT(WINAPI*)(void*, DWORD);
    using DeleteStateBlockFunction = HRESULT(WINAPI*)(void*, DWORD);
    using SetRenderStateFunction = HRESULT(WINAPI*)(void*, DWORD, DWORD);
    using SetTextureFunction = HRESULT(WINAPI*)(void*, DWORD, void*);
    using SetTextureStageStateFunction = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD);
    using SetVertexShaderFunction = HRESULT(WINAPI*)(void*, DWORD);
    using SetPixelShaderFunction = HRESULT(WINAPI*)(void*, DWORD);
    using SceneFunction = HRESULT(WINAPI*)(void*);
    using DrawPrimitiveUPFunction = HRESULT(WINAPI*)(void*, DWORD, UINT, const void*, UINT);
    using GetViewportFunction = HRESULT(WINAPI*)(void*, void*);
    using CreateTextureFunction = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
    using LockTextureRectFunction = HRESULT(WINAPI*)(void*, UINT, void*, const RECT*, DWORD);
    using UnlockTextureRectFunction = HRESULT(WINAPI*)(void*, UINT);
    using ReleaseFunction = ULONG(WINAPI*)(void*);

    struct Viewport8
    {
        DWORD x, y, width, height;
        float minZ, maxZ;
    };

    struct Vertex
    {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
    };

    struct BindingRow
    {
        int x = 0;
        int y = 0;
        enum class PromptKind : std::uint8_t
        {
            Button,
            GenericButton,
            LeftStickHorizontal,
            LeftStickVertical,
            RightStickHorizontal,
            RightStickVertical,
            Dpad
        };
        struct Prompt
        {
            PromptKind kind;
            unsigned int button;
        };
        std::array<Prompt, 8> prompts{};
        std::size_t promptCount = 0;
    };

    int g_style = 0;
    BindingLabelFunction g_originalBindingLabel = nullptr;
    PresentFunction g_originalPresent = nullptr;
    std::array<BindingRow, 16> g_rows{};
    std::array<std::string, 32> g_loggedLabels{};
    std::size_t g_loggedLabelCount = 0;
    std::size_t g_rowCount = 0;
    volatile LONG g_presentHookLogWritten = 0;
    volatile LONG g_presentHookReady = 0;
    volatile LONG g_iconAttachInProgress = 0;
    HMODULE g_pluginModule = nullptr;
    void* volatile g_activeDevice = nullptr;
    void* g_iconTexture = nullptr;

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
        InterlockedExchange(reinterpret_cast<volatile LONG*>(slot),
            static_cast<LONG>(reinterpret_cast<std::uintptr_t>(replacement)));
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
        return true;
    }

    bool ParseBindingPrompts(const char* text, BindingRow& row)
    {
        if (!text)
            return false;
        const char* cursor = text;
        bool expectsButtonNumber = false;
        bool hasUnmappedButton = false;
        while (*cursor)
        {
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (!*cursor)
                break;
            const char* token = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t')
                ++cursor;
            const auto length = static_cast<std::size_t>(cursor - token);
            if (length == 0)
                continue;

            if (expectsButtonNumber || _stricmp(std::string(token, length).c_str(), "button") == 0)
            {
                expectsButtonNumber = true;
                if (_stricmp(std::string(token, length).c_str(), "button") == 0)
                    continue;
            }

            const bool buttonToken = expectsButtonNumber ||
                (length >= 4 && _strnicmp(token, "btn", 3) == 0);
            const char* numberText = token + (expectsButtonNumber ? 0 : 3);
            std::size_t numberLength = length - static_cast<std::size_t>(numberText - token);
            if (buttonToken && numberLength != 0 && *numberText == '#')
            {
                ++numberText;
                --numberLength;
            }

            if (buttonToken && numberLength != 0)
            {
                unsigned int value = 0;
                for (std::size_t i = 0; i < numberLength; ++i)
                {
                    if (numberText[i] < '0' || numberText[i] > '9')
                        return false;
                    value = value * 10 + static_cast<unsigned int>(numberText[i] - '0');
                    if (value > 255)
                        return false;
                }

                if (value == 0)
                    return false;
                if (value <= 12)
                {
                    if (row.promptCount >= row.prompts.size())
                        return false;
                    row.prompts[row.promptCount++] = { BindingRow::PromptKind::Button, value };
                }
                else
                    hasUnmappedButton = true;
                expectsButtonNumber = false;
                continue;
            }

            expectsButtonNumber = false;

            auto add = [&row](BindingRow::PromptKind kind)
            {
                if (row.promptCount >= row.prompts.size())
                    return false;
                row.prompts[row.promptCount++] = { kind, 0 };
                return true;
            };
            if (length >= 4 && _strnicmp(token, "pov", 3) == 0)
            {
                for (std::size_t i = 3; i < length; ++i)
                    if (token[i] < '0' || token[i] > '9')
                        return false;
                if (!add(BindingRow::PromptKind::Dpad))
                    return false;
            }
            else if (length == 4 && _strnicmp(token, "ana", 3) == 0)
            {
                BindingRow::PromptKind kind;
                switch (token[3])
                {
                case 'X': case 'x': kind = BindingRow::PromptKind::LeftStickHorizontal; break;
                case 'Y': case 'y': kind = BindingRow::PromptKind::LeftStickVertical; break;
                case 'Z': case 'z': kind = BindingRow::PromptKind::RightStickHorizontal; break;
                default: return false;
                }

                if (!add(kind))
                    return false;
            }
            else if (length == 5 && _strnicmp(token, "anaRZ", 5) == 0)
            {
                if (!add(BindingRow::PromptKind::RightStickVertical))
                    return false;
            }
            else
                return false;
        }

        if (!expectsButtonNumber && row.promptCount == 0 && hasUnmappedButton)
            row.prompts[row.promptCount++] = { BindingRow::PromptKind::GenericButton, 0 };
        return !expectsButtonNumber && row.promptCount != 0;
    }

    void __cdecl BindingLabelHook(int x, int y, int state, const char* text,
        int format, int sixth, int seventh)
    {
        BindingRow row{};
        row.x = x;
        row.y = y;
        const bool hasBinding = g_style != 0 && ParseBindingPrompts(text, row);
        if (g_style != 0 && text)
        {
            bool alreadyLogged = false;
            for (std::size_t i = 0; i < g_loggedLabelCount; ++i)
                if (g_loggedLabels[i] == text)
                {
                    alreadyLogged = true;
                    break;
                }
            if (!alreadyLogged && g_loggedLabelCount < g_loggedLabels.size())
            {
                g_loggedLabels[g_loggedLabelCount++] = text;
                log::Write("INFO", std::string("[Controller icons] Label hook saw '") + text +
                    "' at (" + std::to_string(x) + ", " + std::to_string(y) + "); parsed=" +
                    (hasBinding ? (std::to_string(row.promptCount) + " controller prompt(s)") :
                        "unrecognized text") + ".");
            }
        }

        if (g_style != 0 && hasBinding && InterlockedCompareExchange(&g_presentHookReady, 0, 0) != 0)
        {
            if (g_rowCount < g_rows.size())
                g_rows[g_rowCount++] = row;
            return;
        }

        if (g_originalBindingLabel)
            g_originalBindingLabel(x, y, state, text, format, sixth, seventh);
    }

    std::uint16_t ReadLittleEndian16(const std::uint8_t* value)
    {
        return static_cast<std::uint16_t>(value[0] | (static_cast<std::uint16_t>(value[1]) << 8));
    }

    bool CreateIconTexture(void* device)
    {
        if (!device || !g_pluginModule)
            return false;

        if (g_iconTexture)
        {
            const auto release = reinterpret_cast<ReleaseFunction>(
                (*reinterpret_cast<void***>(g_iconTexture))[2]);
            release(g_iconTexture);
            g_iconTexture = nullptr;
        }

        const auto resource = FindResourceW(g_pluginModule, MAKEINTRESOURCEW(IDR_CONTROLLER_PROMPTS), RT_RCDATA);
        const auto resourceData = resource ? LoadResource(g_pluginModule, resource) : nullptr;
        const auto* tga = resourceData ? static_cast<const std::uint8_t*>(LockResource(resourceData)) : nullptr;
        const auto resourceSize = resource ? SizeofResource(g_pluginModule, resource) : 0;
        if (!tga || resourceSize < 18 || tga[2] != 2 || tga[16] != 32 || (tga[17] & 0x20) == 0)
        {
            log::Write("WARN", "[Controller icons] Embedded Kenney prompt atlas is missing or has an unsupported TGA format.");
            return false;
        }

        const auto width = ReadLittleEndian16(tga + 12);
        const auto height = ReadLittleEndian16(tga + 14);
        const auto dataOffset = 18u + tga[0];
        const auto requiredSize = static_cast<std::size_t>(width) * height * 4;
        if (width != 1024 || height != 512 || resourceSize < dataOffset + requiredSize)
        {
            log::Write("WARN", "[Controller icons] Embedded prompt atlas dimensions or pixel data are invalid.");
            return false;
        }

        auto** deviceVtable = *reinterpret_cast<void***>(device);
        const auto createTexture = reinterpret_cast<CreateTextureFunction>(deviceVtable[20]);
        const auto result = createTexture(device, width, height, 1, 0, 21, 1, &g_iconTexture);
        if (FAILED(result) || !g_iconTexture)
        {
            g_iconTexture = nullptr;
            log::Write("WARN", "[Controller icons] Could not create the Direct3D 8 prompt texture.");
            return false;
        }

        struct LockedRect
        {
            INT pitch;
            void* bits;
        } locked{};
        auto** textureVtable = *reinterpret_cast<void***>(g_iconTexture);
        const auto lockRect = reinterpret_cast<LockTextureRectFunction>(textureVtable[16]);
        const auto unlockRect = reinterpret_cast<UnlockTextureRectFunction>(textureVtable[17]);
        const auto lockResult = lockRect(g_iconTexture, 0, &locked, nullptr, 0);
        if (FAILED(lockResult) || !locked.bits || locked.pitch < static_cast<INT>(width * 4))
        {
            if (SUCCEEDED(lockResult) && locked.bits)
                unlockRect(g_iconTexture, 0);
            const auto release = reinterpret_cast<ReleaseFunction>(textureVtable[2]);
            release(g_iconTexture);
            g_iconTexture = nullptr;
            log::Write("WARN", "[Controller icons] Could not copy the embedded prompt atlas into a Direct3D texture.");
            return false;
        }

        const auto* pixels = tga + dataOffset;
        for (UINT row = 0; row < height; ++row)
            std::memcpy(static_cast<std::uint8_t*>(locked.bits) + static_cast<std::size_t>(row) * locked.pitch,
                pixels + static_cast<std::size_t>(row) * width * 4, static_cast<std::size_t>(width) * 4);
        unlockRect(g_iconTexture, 0);
        return true;
    }

    void AppendTexturedQuad(std::vector<Vertex>& vertices, float left, float top, float size,
        float u0, float v0, float u1, float v1)
    {
        const Vertex a{ left - 0.5f, top - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, u0, v0 };
        const Vertex b{ left + size - 0.5f, top - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, u1, v0 };
        const Vertex c{ left + size - 0.5f, top + size - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, u1, v1 };
        const Vertex d{ left - 0.5f, top + size - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, u0, v1 };
        vertices.insert(vertices.end(), { a, b, c, a, c, d });
    }

    bool DrawButtonIcons(void* device)
    {
        if (g_style == 0 || g_rowCount == 0 || !g_iconTexture)
            return false;

        auto** vtable = *reinterpret_cast<void***>(device);
        const auto createStateBlock = reinterpret_cast<CreateStateBlockFunction>(vtable[57]);
        const auto applyStateBlock = reinterpret_cast<ApplyStateBlockFunction>(vtable[54]);
        const auto deleteStateBlock = reinterpret_cast<DeleteStateBlockFunction>(vtable[56]);
        const auto setRenderState = reinterpret_cast<SetRenderStateFunction>(vtable[50]);
        const auto setTexture = reinterpret_cast<SetTextureFunction>(vtable[61]);
        const auto setTextureStageState = reinterpret_cast<SetTextureStageStateFunction>(vtable[63]);
        const auto setVertexShader = reinterpret_cast<SetVertexShaderFunction>(vtable[76]);
        const auto setPixelShader = reinterpret_cast<SetPixelShaderFunction>(vtable[88]);
        const auto beginScene = reinterpret_cast<SceneFunction>(vtable[34]);
        const auto endScene = reinterpret_cast<SceneFunction>(vtable[35]);
        const auto drawPrimitiveUP = reinterpret_cast<DrawPrimitiveUPFunction>(vtable[72]);
        const auto getViewport = reinterpret_cast<GetViewportFunction>(vtable[41]);
        DWORD stateToken = 0;
        if (FAILED(createStateBlock(device, 1, &stateToken)))
            return false;
        if (FAILED(beginScene(device)))
        {
            deleteStateBlock(device, stateToken);
            return false;
        }

        Viewport8 viewport{};
        if (FAILED(getViewport(device, &viewport)) || viewport.width == 0 || viewport.height == 0)
        {
            deleteStateBlock(device, stateToken);
            endScene(device);
            return false;
        }

        std::vector<Vertex> vertices;
        vertices.reserve(g_rowCount * 8 * 6);
        const float scaleX = static_cast<float>(viewport.width) / 512.0f;
        const float scaleY = static_cast<float>(viewport.height) / 448.0f;
        const float iconSize = 20.0f * (scaleX < scaleY ? scaleX : scaleY);
        for (std::size_t index = 0; index < g_rowCount; ++index)
        {
            const auto& row = g_rows[index];
            const float left = viewport.x + static_cast<float>(row.x) * scaleX;
            // The game text renderer treats y as the row baseline. Place the prompt
            // one text row above that coordinate so it centers on the label line.
            const float top = viewport.y + (static_cast<float>(row.y) - 27.0f) * scaleY;
            const unsigned int atlasStyle = static_cast<unsigned int>(g_style - 1);
            constexpr float halfTexelU = 0.5f / 1024.0f;
            constexpr float halfTexelV = 0.5f / 512.0f;
            for (std::size_t promptIndex = 0; promptIndex < row.promptCount; ++promptIndex)
            {
                const auto& prompt = row.prompts[promptIndex];
                unsigned int atlasColumn = 0;
                unsigned int atlasRow = atlasStyle;
                switch (prompt.kind)
                {
                case BindingRow::PromptKind::Button: atlasColumn = prompt.button - 1; break;
                case BindingRow::PromptKind::GenericButton: atlasColumn = 12; break;
                case BindingRow::PromptKind::LeftStickHorizontal: atlasColumn = 1; atlasRow += 4; break;
                case BindingRow::PromptKind::LeftStickVertical: atlasColumn = 2; atlasRow += 4; break;
                case BindingRow::PromptKind::RightStickHorizontal: atlasColumn = 3; atlasRow += 4; break;
                case BindingRow::PromptKind::RightStickVertical: atlasColumn = 4; atlasRow += 4; break;
                case BindingRow::PromptKind::Dpad: atlasColumn = 5; atlasRow += 4; break;
                }

                const float u0 = static_cast<float>(atlasColumn) / 16.0f + halfTexelU;
                const float u1 = static_cast<float>(atlasColumn + 1) / 16.0f - halfTexelU;
                const float v0 = static_cast<float>(atlasRow) / 8.0f + halfTexelV;
                const float v1 = static_cast<float>(atlasRow + 1) / 8.0f - halfTexelV;
                const float promptLeft = left + static_cast<float>(promptIndex) * (iconSize + 2.0f * scaleX);
                AppendTexturedQuad(vertices, promptLeft, top, iconSize, u0, v0, u1, v1);
            }
        }

        // D3DSBT_ALL restores the game's complete fixed-function/shader state after our overlay.
        setRenderState(device, 7, 0);   // ZENABLE
        setRenderState(device, 14, 0);  // ZWRITEENABLE
        setRenderState(device, 15, 0);  // ALPHATESTENABLE
        setRenderState(device, 22, 1);  // CULLMODE = NONE
        setRenderState(device, 27, 1);  // ALPHABLENDENABLE
        setRenderState(device, 28, 0);  // FOGENABLE
        setRenderState(device, 137, 0); // LIGHTING
        setRenderState(device, 136, 0); // CLIPPING
        setRenderState(device, 19, 5);  // SRCBLEND = SRCALPHA
        setRenderState(device, 20, 6);  // DESTBLEND = INVSRCALPHA
        setTexture(device, 0, g_iconTexture);
        setTextureStageState(device, 0, 1, 2); // COLOROP = SELECTARG1
        setTextureStageState(device, 0, 2, 2); // COLORARG1 = TEXTURE
        setTextureStageState(device, 0, 4, 2); // ALPHAOP = SELECTARG1
        setTextureStageState(device, 0, 5, 2); // ALPHAARG1 = TEXTURE
        setTextureStageState(device, 0, 13, 3); // ADDRESSU = CLAMP
        setTextureStageState(device, 0, 14, 3); // ADDRESSV = CLAMP
        setTextureStageState(device, 0, 16, 2); // MAGFILTER = LINEAR
        setTextureStageState(device, 0, 17, 2); // MINFILTER = LINEAR
        setVertexShader(device, 0x144); // XYZRHW | DIFFUSE | TEX1
        setPixelShader(device, 0);
        const auto triangleCount = static_cast<UINT>(vertices.size() / 3);
        const auto drawResult = drawPrimitiveUP(device, 4, triangleCount, vertices.data(), sizeof(Vertex));
        endScene(device);
        applyStateBlock(device, stateToken);
        deleteStateBlock(device, stateToken);
        return SUCCEEDED(drawResult);
    }

    HRESULT WINAPI PresentHook(void* device, const RECT* source, const RECT* destination,
        HWND overrideWindow, const RGNDATA* dirtyRegion)
    {
        if (g_rowCount != 0)
        {
            DrawButtonIcons(device);
            g_rowCount = 0;
        }

        return g_originalPresent
            ? g_originalPresent(device, source, destination, overrideWindow, dirtyRegion)
            : E_FAIL;
    }

    bool AttachIconRenderer(void* device)
    {
        if (!device || g_style == 0)
            return false;
        if (InterlockedCompareExchange(&g_presentHookReady, 0, 0) != 0 &&
            InterlockedCompareExchangePointer(&g_activeDevice, nullptr, nullptr) == device)
            return true;
        if (InterlockedCompareExchange(&g_iconAttachInProgress, 1, 0) != 0)
            return false;

        bool attached = false;
        if (CreateIconTexture(device))
        {
            void* original = nullptr;
            if (ReplaceVtableEntry(device, kPresentVtableOffset,
                    reinterpret_cast<void*>(&PresentHook), &original))
            {
                if (original)
                    g_originalPresent = reinterpret_cast<PresentFunction>(original);
                if (g_originalPresent)
                {
                    InterlockedExchangePointer(&g_activeDevice, device);
                    InterlockedExchange(&g_presentHookReady, 1);
                    if (InterlockedCompareExchange(&g_presentHookLogWritten, 1, 0) == 0)
                        log::Write("INFO", "[Controller icons] Attached the Kenney prompt renderer to Direct3D 8 Present.");
                    attached = true;
                }
            }

            if (!attached)
                log::Write("WARN", "[Controller icons] Could not hook Direct3D 8 Present.");
        }

        InterlockedExchange(&g_iconAttachInProgress, 0);
        return attached;
    }
}

bool InstallControllerButtonIcons(HMODULE executable, int style, HMODULE pluginModule)
{
    g_style = (style >= 0 && style <= 4) ? style : 0;
    if (g_style == 0)
        return true;
    if (!executable || !pluginModule)
        return false;
    g_pluginModule = pluginModule;

    auto* image = reinterpret_cast<std::uint8_t*>(executable);
    for (const auto& site : kBindingLabelCalls)
    {
        if (std::memcmp(image + site.rva, site.expected.data(), site.expected.size()) != 0)
        {
            log::Write("WARN", "[Controller icons] A Control Options text-renderer call did not match the supported executable.");
            return false;
        }
    }

    g_originalBindingLabel = reinterpret_cast<BindingLabelFunction>(image + kBindingLabelRendererRva);
    std::size_t patchedCalls = 0;
    for (const auto& site : kBindingLabelCalls)
    {
        auto* call = image + site.rva;
        std::uint8_t replacement[5] = { 0xE8, 0, 0, 0, 0 };
        if (!detail::EncodeRelativeJump(replacement, reinterpret_cast<std::uintptr_t>(call),
                reinterpret_cast<void*>(&BindingLabelHook)))
            break;
        replacement[0] = 0xE8;
        if (!WriteMemory(reinterpret_cast<std::uintptr_t>(call), replacement, sizeof(replacement)))
            break;
        ++patchedCalls;
    }

    if (patchedCalls != kBindingLabelCalls.size())
    {
        for (std::size_t i = 0; i < patchedCalls; ++i)
            WriteMemory(reinterpret_cast<std::uintptr_t>(image + kBindingLabelCalls[i].rva),
                kBindingLabelCalls[i].expected.data(), kBindingLabelCalls[i].expected.size());
        return false;
    }

    constexpr const char* styles[] = { "off", "generic", "Xbox", "PlayStation", "Nintendo" };
    log::Write("INFO", std::string("[Controller icons] Control Options binding labels replaced with ") +
        styles[g_style] + " Kenney prompts across four verified Control Options text call sites.");
    if (const auto device = InterlockedCompareExchangePointer(&g_activeDevice, nullptr, nullptr))
    {
        if (!AttachIconRenderer(device))
        {
            log::Write("WARN", "[Controller icons] Prompt hooks are ready, but the Direct3D overlay could not attach to the active device.");
            return false;
        }
    }

    return true;
}

bool HookControllerButtonIconsDevice(void* device)
{
    if (!device)
        return true;
    InterlockedExchangePointer(&g_activeDevice, device);
    return g_style == 0 || AttachIconRenderer(device);
}
}
