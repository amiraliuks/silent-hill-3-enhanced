#include "patches.h"
#include "../logger.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sh3e::patches
{
namespace
{
    using CreateWindowExAFunction = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int,
        HWND, HMENU, HINSTANCE, LPVOID);
    using ChangeDisplaySettingsAFunction = LONG(WINAPI*)(DEVMODEA*, LPCSTR, DWORD);
    using SetWindowLongAFunction = LONG(WINAPI*)(HWND, int, LONG);
    using SetWindowPosFunction = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
    using WindowProcFunction = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
    using Direct3DCreate8Function = void*(WINAPI*)(UINT);
    using Direct3D8CreateDeviceFunction = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, void*, void**);
    using Direct3DDevice8ResetFunction = HRESULT(WINAPI*)(void*, void*);
    constexpr std::size_t kDirect3DDevice8ResetVtableOffset = 0x38;

    struct Direct3D8PresentParameters
    {
        UINT backBufferWidth;
        UINT backBufferHeight;
        DWORD backBufferFormat;
        UINT backBufferCount;
        DWORD multiSampleType;
        DWORD swapEffect;
        HWND deviceWindow;
        BOOL windowed;
        BOOL enableAutoDepthStencil;
        DWORD autoDepthStencilFormat;
        DWORD flags;
        UINT fullScreenRefreshRate;
        UINT fullScreenPresentationInterval;
    };
    static_assert(offsetof(Direct3D8PresentParameters, windowed) == 0x1C);
    static_assert(offsetof(Direct3D8PresentParameters, fullScreenRefreshRate) == 0x2C);

    CreateWindowExAFunction g_originalCreateWindowExA = nullptr;
    ChangeDisplaySettingsAFunction g_originalChangeDisplaySettingsA = nullptr;
    SetWindowLongAFunction g_originalSetWindowLongA = nullptr;
    SetWindowPosFunction g_originalSetWindowPos = nullptr;
    Direct3DCreate8Function g_originalDirect3DCreate8 = nullptr;
    Direct3D8CreateDeviceFunction g_originalDirect3D8CreateDevice = nullptr;
    Direct3DDevice8ResetFunction g_originalDirect3DDevice8Reset = nullptr;
    bool g_anisotropicTextureFiltering = true;
    int g_windowMode = 0;
    int g_windowWidth = 0;
    int g_windowHeight = 0;
    volatile LONG g_windowStyleLogWritten = 0;
    volatile LONG g_direct3DWindowedLogWritten = 0;
    PVOID volatile g_gameWindow = nullptr;
    volatile LONG g_windowStyleWorkerStarted = 0;
    volatile LONG g_windowFrameInitialized = 0;
    volatile LONG g_windowProcInstallState = 0;
    volatile LONG g_originalWindowProc = 0;
    volatile LONG g_windowProcLogWritten = 0;
    volatile LONG g_d3d8ProviderLogWritten = 0;
    volatile LONG g_gameWindowCreateTraceWritten = 0;
    volatile LONG g_d3dDeviceCreateTraceWritten = 0;
    bool IsMainGameWindow(HWND window);
    bool ApplyWindowStyle(HWND window);
    void StartWindowStyleWorker(HWND window);

    LRESULT CALLBACK WindowModeMainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MAXIMIZE)
        {
            ShowWindow(window, SW_MAXIMIZE);
            return 0;
        }

        const auto previous = static_cast<WindowProcFunction>(reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(InterlockedCompareExchange(&g_originalWindowProc, 0, 0))));
        return previous ? CallWindowProcA(previous, window, message, wParam, lParam)
                        : DefWindowProcA(window, message, wParam, lParam);
    }

    bool InstallMainWindowProc(HWND window)
    {
        if (!IsMainGameWindow(window) || !g_originalSetWindowLongA)
            return false;

        const auto current = GetWindowLongA(window, GWL_WNDPROC);
        const auto replacement = static_cast<LONG>(reinterpret_cast<std::uintptr_t>(&WindowModeMainWindowProc));
        if (current == replacement)
            return true;

        if (InterlockedCompareExchange(&g_windowProcInstallState, 1, 0) != 0)
            return InterlockedCompareExchange(&g_windowProcInstallState, 0, 0) == 2;

        InterlockedExchange(&g_originalWindowProc, current);
        SetLastError(ERROR_SUCCESS);
        const auto previous = g_originalSetWindowLongA(window, GWL_WNDPROC, replacement);

        if (previous == 0 && GetLastError() != ERROR_SUCCESS)
        {
            InterlockedExchange(&g_originalWindowProc, 0);
            InterlockedExchange(&g_windowProcInstallState, 0);
            return false;
        }

        InterlockedExchange(&g_originalWindowProc, previous);
        InterlockedExchange(&g_windowProcInstallState, 2);
        return true;
    }

    bool IsMainGameWindow(HWND window)
    {
        if (!window)
            return false;

        char title[256]{};
        GetWindowTextA(window, title, static_cast<int>(sizeof(title)));
        return lstrcmpiA(title, "SILENT HILL 3") == 0;
    }

    HWND CurrentGameWindow()
    {
        return static_cast<HWND>(InterlockedCompareExchangePointer(&g_gameWindow, nullptr, nullptr));
    }

    void RememberGameWindow(HWND window)
    {
        if (IsMainGameWindow(window))
        {
            InterlockedExchangePointer(&g_gameWindow, window);
            StartWindowStyleWorker(window);
        }
    }

    bool ReplaceVtableEntry(void* instance, std::size_t byteOffset, void* replacement, void** original)
    {
        if (!instance || (byteOffset % sizeof(DWORD)) != 0)
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

        InterlockedExchange(reinterpret_cast<volatile LONG*>(slot), static_cast<LONG>(reinterpret_cast<std::uintptr_t>(replacement)));
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
        return true;
    }

    HRESULT WINAPI WindowModeDirect3DDevice8Reset(void* device, void* parameters)
    {
        if (parameters)
        {
            auto* presentation = static_cast<Direct3D8PresentParameters*>(parameters);
            sh3e::patches::EnforceNoMultisampling(&presentation->multiSampleType);
            presentation->windowed = g_windowMode != 0;
            presentation->fullScreenRefreshRate = 0;
        }

        const auto result = g_originalDirect3DDevice8Reset ? g_originalDirect3DDevice8Reset(device, parameters) : E_FAIL;
        if (SUCCEEDED(result))
            ReapplyAnisotropicFiltering(device);

        return result;
    }

    HRESULT WINAPI WindowModeDirect3D8CreateDevice(void* direct3D, UINT adapter, DWORD deviceType, HWND focusWindow, DWORD behaviorFlags, void* parameters, void** device)
    {
        const bool traceCreateDevice = InterlockedCompareExchange(&g_d3dDeviceCreateTraceWritten, 1, 0) == 0;
        if (traceCreateDevice)
            log::Write("INFO", "[Startup timing] Entered Direct3D 8 CreateDevice.");

        if (focusWindow)
            RememberGameWindow(focusWindow);

        if (parameters)
        {
            auto* presentation = static_cast<Direct3D8PresentParameters*>(parameters);
            sh3e::patches::EnforceNoMultisampling(&presentation->multiSampleType);
        }

        if (parameters && g_windowMode != 0)
        {
            auto* presentation = static_cast<Direct3D8PresentParameters*>(parameters);
            presentation->windowed = TRUE;
            presentation->fullScreenRefreshRate = 0;
            RememberGameWindow(presentation->deviceWindow);
            if (presentation->deviceWindow == CurrentGameWindow())
                ApplyWindowStyle(presentation->deviceWindow);
            else if (focusWindow == CurrentGameWindow())
                ApplyWindowStyle(focusWindow);
        }

        const auto result = g_originalDirect3D8CreateDevice
            ? g_originalDirect3D8CreateDevice(direct3D, adapter, deviceType, focusWindow,
                behaviorFlags, parameters, device)
            : E_FAIL;
        if (traceCreateDevice)
            log::Write("INFO", "[Startup timing] Returned from Direct3D 8 CreateDevice with HRESULT " +
                std::to_string(static_cast<std::uint32_t>(result)) + ".");
        if (SUCCEEDED(result) && device && *device)
        {
            InstallAnisotropicFiltering(*device, g_anisotropicTextureFiltering);
            HookControllerButtonIconsDevice(*device);
            void* originalReset = nullptr;
            if (ReplaceVtableEntry(*device, kDirect3DDevice8ResetVtableOffset,
                    reinterpret_cast<void*>(&WindowModeDirect3DDevice8Reset), &originalReset) && originalReset)
                g_originalDirect3DDevice8Reset = reinterpret_cast<Direct3DDevice8ResetFunction>(originalReset);
            if (InterlockedCompareExchange(&g_direct3DWindowedLogWritten, 1, 0) == 0)
            {
                const char* presentationMode = g_windowMode == 0 ? "fullscreen" : "windowed";
                log::Write("INFO", std::string("[Window mode] Direct3D 8 device created with ") +
                    presentationMode + " presentation enabled.");
            }
        }

        return result;
    }

    void* WINAPI WindowModeDirect3DCreate8(UINT sdkVersion)
    {
        if (InterlockedCompareExchange(&g_d3d8ProviderLogWritten, 1, 0) == 0)
        {
            const auto provider = GetModuleHandleA("d3d8.dll");
            if (provider)
            {
                char modulePath[MAX_PATH]{};
                const auto pathLength = GetModuleFileNameA(provider, modulePath, static_cast<DWORD>(sizeof(modulePath)));
                if (pathLength != 0 && pathLength < sizeof(modulePath))
                    log::Write("INFO", std::string("[D3D8] Loaded d3d8.dll: ") + modulePath);
                else
                    log::Write("WARN", "[D3D8] Could not read the loaded d3d8.dll path.");
            }
            else
                log::Write("WARN", "[D3D8] d3d8.dll was not found in the loaded module list.");
        }

        const auto direct3D = g_originalDirect3DCreate8 ? g_originalDirect3DCreate8(sdkVersion) : nullptr;
        if (direct3D)
        {
            void* originalCreateDevice = nullptr;
            if (ReplaceVtableEntry(direct3D, 0x3C,
                    reinterpret_cast<void*>(&WindowModeDirect3D8CreateDevice), &originalCreateDevice) && originalCreateDevice)
            {
                g_originalDirect3D8CreateDevice = reinterpret_cast<Direct3D8CreateDeviceFunction>(originalCreateDevice);
                log::Write("INFO", "[Direct3D 8] Hooked device creation for display mode, MSAA control, and texture filtering.");
            }
            else
                log::Write("WARN", "[Window mode] Could not hook Direct3D 8 CreateDevice.");
        }

        return direct3D;
    }

    bool ApplyWindowStyle(HWND window)
    {
        if (!window || !IsWindow(window))
            return false;

        auto style = static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE));
        auto extendedStyle = static_cast<DWORD>(GetWindowLongA(window, GWL_EXSTYLE));
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool styleChanged = false;

        if (g_windowMode == 0)
        {
            const auto updatedStyle = (style & ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER |
                WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
            const auto updatedExtendedStyle = extendedStyle & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
                WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
            styleChanged = updatedStyle != style || updatedExtendedStyle != extendedStyle;
            style = updatedStyle;
            extendedStyle = updatedExtendedStyle;
            x = 0;
            y = 0;
            width = g_windowWidth;
            height = g_windowHeight;
        }
        else if (g_windowMode == 2)
        {
            const auto updatedStyle = (style & ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER |
                WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
            const auto updatedExtendedStyle = extendedStyle & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
                WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
            styleChanged = updatedStyle != style || updatedExtendedStyle != extendedStyle;
            style = updatedStyle;
            extendedStyle = updatedExtendedStyle;
            x = 0;
            y = 0;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
        else if (g_windowMode == 1)
        {
            const auto updatedStyle = (style & ~(WS_CHILD | WS_POPUP)) | WS_OVERLAPPEDWINDOW;
            const auto updatedExtendedStyle = extendedStyle & ~WS_EX_TOPMOST;
            styleChanged = updatedStyle != style || updatedExtendedStyle != extendedStyle;
            style = updatedStyle;
            extendedStyle = updatedExtendedStyle;
            if (InterlockedCompareExchange(&g_windowFrameInitialized, 0, 0) == 0)
            {
                RECT bounds{ 0, 0, g_windowWidth, g_windowHeight };
                if (!AdjustWindowRectEx(&bounds, style, FALSE, extendedStyle))
                    return false;
                width = bounds.right - bounds.left;
                height = bounds.bottom - bounds.top;
                x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
                y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
            }
        }
        else
            return true;

        if (styleChanged)
        {
            SetWindowLongA(window, GWL_STYLE, static_cast<LONG>(style));
            SetWindowLongA(window, GWL_EXSTYLE, static_cast<LONG>(extendedStyle));
        }

        const bool preserveWindowBounds = g_windowMode == 1 &&
            InterlockedCompareExchange(&g_windowFrameInitialized, 0, 0) != 0;
        RECT current{};
        const bool hasRect = GetWindowRect(window, &current) != FALSE;
        const bool boundsChanged = !preserveWindowBounds && (!hasRect || current.left != x || current.top != y ||
            current.right - current.left != width || current.bottom - current.top != height);
        UINT positionFlags = SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW;
        if (preserveWindowBounds)
            positionFlags |= SWP_NOMOVE | SWP_NOSIZE;
        if ((styleChanged || boundsChanged) && !SetWindowPos(window, HWND_NOTOPMOST, x, y, width, height,
                positionFlags))
            return false;

        const auto appliedStyle = static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE));
        const auto appliedExtendedStyle = static_cast<DWORD>(GetWindowLongA(window, GWL_EXSTYLE));
        if (g_windowMode == 1 && (appliedStyle & WS_CAPTION) != WS_CAPTION)
            return false;
        if (g_windowMode == 1 && ((appliedStyle & WS_SYSMENU) == 0 || (appliedStyle & WS_THICKFRAME) == 0 ||
            (appliedStyle & WS_MAXIMIZEBOX) == 0 ||
            (appliedExtendedStyle & WS_EX_TOPMOST) != 0))
            return false;
        if ((g_windowMode == 0 || g_windowMode == 2) &&
            ((appliedStyle & WS_POPUP) == 0 || (appliedStyle & WS_CAPTION) != 0))
            return false;
        if ((g_windowMode == 0 || g_windowMode == 2) && (appliedExtendedStyle & WS_EX_TOPMOST) != 0)
            return false;

        if (g_windowMode == 1)
            InterlockedExchange(&g_windowFrameInitialized, 1);
        if (!InstallMainWindowProc(window))
            log::Write("WARN", "[Window mode] Could not install the SH3 window-procedure handler.");
        else if (g_windowMode == 1 && InterlockedCompareExchange(&g_windowProcLogWritten, 1, 0) == 0)
            log::Write("INFO", "[Window mode] SH3 maximize handler installed.");

        return true;
    }

    DWORD WINAPI ReapplyWindowStyle(void* parameter)
    {
        const auto window = static_cast<HWND>(parameter);
        bool applied = false;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            Sleep(100);
            if (!IsWindow(window))
                break;
            if (ApplyWindowStyle(window))
            {
                applied = true;
                if (InterlockedCompareExchange(&g_windowStyleLogWritten, 1, 0) == 0)
                {
                    const char* mode = g_windowMode == 0 ? "fullscreen SH3 window" :
                        (g_windowMode == 1 ? "framed, non-topmost SH3 window" : "non-topmost borderless SH3 window");
                    log::Write("INFO", std::string("[Window mode] Verified ") + mode + ".");
                }
            }
        }

        if (!applied && InterlockedCompareExchange(&g_windowStyleLogWritten, 1, 0) == 0)
            log::Write("WARN", "[Window mode] The game window did not accept the requested style.");
        return 0;
    }

    void StartWindowStyleWorker(HWND window)
    {
        if (InterlockedCompareExchange(&g_windowStyleWorkerStarted, 1, 0) != 0)
            return;
        if (const auto thread = CreateThread(nullptr, 0, ReapplyWindowStyle, window, 0, nullptr))
            CloseHandle(thread);
        else
        {
            InterlockedExchange(&g_windowStyleWorkerStarted, 0);
            log::Write("WARN", "[Window mode] Could not start the game-window style worker.");
        }
    }

    HWND WINAPI WindowModeCreateWindowExA(DWORD extendedStyle, LPCSTR className, LPCSTR windowName,
        DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu,
        HINSTANCE instance, LPVOID parameter)
    {
        const bool isGameWindow = windowName && lstrcmpiA(windowName, "SILENT HILL 3") == 0;
        const bool traceGameWindow = isGameWindow &&
            InterlockedCompareExchange(&g_gameWindowCreateTraceWritten, 1, 0) == 0;
        if (traceGameWindow)
            log::Write("INFO", "[Startup timing] Entered CreateWindowExA for the SH3 main window.");
        if (isGameWindow && g_windowMode == 0)
        {
            style &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
                WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            style |= WS_POPUP;
            extendedStyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
            x = 0;
            y = 0;
            width = g_windowWidth;
            height = g_windowHeight;
        }
        else if (isGameWindow && g_windowMode == 2)
        {
            style &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
                WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            style |= WS_POPUP;
            extendedStyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
            x = 0;
            y = 0;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
        else if (isGameWindow && g_windowMode == 1)
        {
            style &= ~(WS_CHILD | WS_POPUP);
            style |= WS_OVERLAPPEDWINDOW;
            width = g_windowWidth;
            height = g_windowHeight;
            RECT bounds{ 0, 0, width, height };
            if (AdjustWindowRectEx(&bounds, style, menu != nullptr, extendedStyle))
            {
                width = bounds.right - bounds.left;
                height = bounds.bottom - bounds.top;
            }

            x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        }

        if (!g_originalCreateWindowExA)
            return nullptr;

        const auto window = g_originalCreateWindowExA(extendedStyle, className, windowName, style, x, y, width, height,
            parent, menu, instance, parameter);
        if (traceGameWindow)
            log::Write("INFO", std::string("[Startup timing] Returned from SH3 main-window creation; HWND=") +
                std::to_string(reinterpret_cast<std::uintptr_t>(window)) + ".");
        if (window && !parent && IsMainGameWindow(window))
        {
            RememberGameWindow(window);
            ApplyWindowStyle(window);
        }

        return window;
    }

    LONG WINAPI WindowModeSetWindowLongA(HWND window, int index, LONG value)
    {
        if (window == CurrentGameWindow())
        {
            auto style = static_cast<DWORD>(value);
            if (index == GWL_STYLE)
            {
                if (g_windowMode == 1)
                    style = (style & ~(WS_CHILD | WS_POPUP)) | WS_OVERLAPPEDWINDOW;
                else if (g_windowMode == 0)
                    style = (style & ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
                        WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
                else if (g_windowMode == 2)
                    style = (style & ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
                        WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
                value = static_cast<LONG>(style);
            }
            else if (index == GWL_EXSTYLE)
            {
                style &= ~WS_EX_TOPMOST;
                if (g_windowMode == 0 || g_windowMode == 2)
                    style &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
                value = static_cast<LONG>(style);
            }
        }

        return g_originalSetWindowLongA ? g_originalSetWindowLongA(window, index, value) : 0;
    }

    BOOL WINAPI WindowModeSetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags)
    {
        if (window == CurrentGameWindow())
        {
            if ((flags & SWP_NOZORDER) == 0)
                insertAfter = HWND_NOTOPMOST;

            if (g_windowMode == 0)
            {
                if ((flags & SWP_NOMOVE) == 0)
                {
                    x = 0;
                    y = 0;
                }

                if ((flags & SWP_NOSIZE) == 0)
                {
                    width = g_windowWidth;
                    height = g_windowHeight;
                }
            }
            else if (g_windowMode == 2)
            {
                if ((flags & SWP_NOMOVE) == 0)
                {
                    x = 0;
                    y = 0;
                }

                if ((flags & SWP_NOSIZE) == 0)
                {
                    width = GetSystemMetrics(SM_CXSCREEN);
                    height = GetSystemMetrics(SM_CYSCREEN);
                }
            }
            else if (g_windowMode == 1)
            {
                // Keep Windows' requested bounds so users can resize or maximize the window.
            }
        }

        return g_originalSetWindowPos ? g_originalSetWindowPos(window, insertAfter, x, y, width, height, flags) : FALSE;
    }

    LONG WINAPI WindowModeChangeDisplaySettingsA(DEVMODEA* deviceMode, LPCSTR deviceName, DWORD flags)
    {
        if (g_windowMode != 0)
            return DISP_CHANGE_SUCCESSFUL;
        return g_originalChangeDisplaySettingsA
            ? g_originalChangeDisplaySettingsA(deviceMode, deviceName, flags)
            : DISP_CHANGE_FAILED;
    }

    bool ReplaceImportThunk(DWORD* thunk, DWORD replacement, DWORD& original)
    {
        DWORD oldProtection = 0;
        if (!VirtualProtect(thunk, sizeof(*thunk), PAGE_READWRITE, &oldProtection))
            return false;

        original = *thunk;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(thunk), static_cast<LONG>(replacement));

        DWORD ignored = 0;
        VirtualProtect(thunk, sizeof(*thunk), oldProtection, &ignored);
        return true;
    }

    bool InstallImportHooks(HMODULE executable)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(executable);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return false;

        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
            return false;

        DWORD* createWindowSlot = nullptr;
        DWORD* changeDisplaySlot = nullptr;
        DWORD* direct3DCreateSlot = nullptr;
        DWORD* setWindowLongSlot = nullptr;
        DWORD* setWindowPosSlot = nullptr;
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        for (; descriptor->Name != 0; ++descriptor)
        {
            if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0)
                continue;

            auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
            auto* table = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData != 0; ++names, ++table)
            {
                if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
                    continue;
                const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                const auto* name = reinterpret_cast<const char*>(import->Name);
                auto* slot = reinterpret_cast<DWORD*>(&table->u1.Function);
                if (std::strcmp(name, "CreateWindowExA") == 0)
                    createWindowSlot = slot;
                else if (std::strcmp(name, "ChangeDisplaySettingsA") == 0)
                    changeDisplaySlot = slot;
                else if (std::strcmp(name, "Direct3DCreate8") == 0)
                    direct3DCreateSlot = slot;
                else if (std::strcmp(name, "SetWindowLongA") == 0)
                    setWindowLongSlot = slot;
                else if (std::strcmp(name, "SetWindowPos") == 0)
                    setWindowPosSlot = slot;
            }
        }

        if (!createWindowSlot || !changeDisplaySlot || !direct3DCreateSlot || !setWindowLongSlot || !setWindowPosSlot)
            return false;

        DWORD originalCreateWindow = 0;
        DWORD originalDisplaySettings = 0;
        DWORD originalDirect3DCreate = 0;
        DWORD originalSetWindowLong = 0;
        DWORD originalSetWindowPos = 0;

        if (!ReplaceImportThunk(createWindowSlot, static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&WindowModeCreateWindowExA)), originalCreateWindow))
            return false;

        if (!ReplaceImportThunk(changeDisplaySlot, static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&WindowModeChangeDisplaySettingsA)), originalDisplaySettings))
        {
            DWORD ignored = 0;
            ReplaceImportThunk(createWindowSlot, originalCreateWindow, ignored);
            return false;
        }

        if (!ReplaceImportThunk(direct3DCreateSlot, static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&WindowModeDirect3DCreate8)), originalDirect3DCreate))
        {
            DWORD ignored = 0;
            ReplaceImportThunk(changeDisplaySlot, originalDisplaySettings, ignored);
            ReplaceImportThunk(createWindowSlot, originalCreateWindow, ignored);
            return false;
        }

        if (!ReplaceImportThunk(setWindowLongSlot, static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&WindowModeSetWindowLongA)), originalSetWindowLong))
        {
            DWORD ignored = 0;
            ReplaceImportThunk(direct3DCreateSlot, originalDirect3DCreate, ignored);
            ReplaceImportThunk(changeDisplaySlot, originalDisplaySettings, ignored);
            ReplaceImportThunk(createWindowSlot, originalCreateWindow, ignored);
            return false;
        }

        if (!ReplaceImportThunk(setWindowPosSlot, static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&WindowModeSetWindowPos)), originalSetWindowPos))
        {
            DWORD ignored = 0;
            ReplaceImportThunk(setWindowLongSlot, originalSetWindowLong, ignored);
            ReplaceImportThunk(direct3DCreateSlot, originalDirect3DCreate, ignored);
            ReplaceImportThunk(changeDisplaySlot, originalDisplaySettings, ignored);
            ReplaceImportThunk(createWindowSlot, originalCreateWindow, ignored);
            return false;
        }

        g_originalCreateWindowExA = reinterpret_cast<CreateWindowExAFunction>(static_cast<std::uintptr_t>(originalCreateWindow));
        g_originalChangeDisplaySettingsA = reinterpret_cast<ChangeDisplaySettingsAFunction>(static_cast<std::uintptr_t>(originalDisplaySettings));
        g_originalDirect3DCreate8 = reinterpret_cast<Direct3DCreate8Function>(static_cast<std::uintptr_t>(originalDirect3DCreate));
        g_originalSetWindowLongA = reinterpret_cast<SetWindowLongAFunction>(static_cast<std::uintptr_t>(originalSetWindowLong));
        g_originalSetWindowPos = reinterpret_cast<SetWindowPosFunction>(static_cast<std::uintptr_t>(originalSetWindowPos));
        return true;
    }
}

bool InstallWindowModeHooks(HMODULE executable, int mode, int width, int height, bool anisotropicTextureFiltering)
{
    if (!executable || mode < 0 || mode > 2)
        return false;

    g_windowMode = mode;
    g_anisotropicTextureFiltering = anisotropicTextureFiltering;
    g_windowWidth = width > 0 ? width : 1280;
    g_windowHeight = height > 0 ? height : 720;
    g_windowStyleLogWritten = 0;
    g_direct3DWindowedLogWritten = 0;
    g_windowStyleWorkerStarted = 0;
    g_windowFrameInitialized = 0;
    g_windowProcInstallState = 0;
    g_originalWindowProc = 0;
    g_windowProcLogWritten = 0;
    g_gameWindowCreateTraceWritten = 0;
    g_d3dDeviceCreateTraceWritten = 0;
    InterlockedExchangePointer(&g_gameWindow, nullptr);
    if (!InstallImportHooks(executable))
    {
        g_windowMode = 0;
        return false;
    }

    g_d3d8ProviderLogWritten = 0;
    return true;
}
}
