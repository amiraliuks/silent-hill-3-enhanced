#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include "resource.h"
#include "config.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace
{
    constexpr UINT WM_SHOW_HELP = WM_APP + 1;
    constexpr int kPageIds[] = { IDD_DISPLAY_PAGE, IDD_GRAPHICS_PAGE, IDD_SYSTEM_PAGE, IDD_CONTENT_PAGE };
    constexpr wchar_t kTabNames[][16] = { L"Display", L"Graphics", L"System", L"Content" };

    HWND g_main{};
    HWND g_tabs{};
    HWND g_pages[4]{};
    HINSTANCE g_instance{};
    std::vector<std::wstring> g_executables;
    std::wstring g_executable;
    std::wstring g_iniPath;
    sh3e::Config g_config;

    struct HelpEntry { int id; const wchar_t* title; const wchar_t* description; };
    constexpr HelpEntry kHelp[] = {
        { IDC_WINDOW_MODE, L"Window mode", L"Choose fullscreen, a resizable window, or borderless fullscreen." },
        { IDC_RESOLUTION_X, L"Output resolution", L"Sets the game's output/backbuffer size. 0 x 0 follows the current Windows desktop resolution (which may differ from your monitor's maximum). Positive values request a custom output size. Fullscreen mode may switch the display to that size. Restart the game after changing it." },
        { IDC_RENDER_X, L"Render resolution", L"Sets the internal 3D render-target size, not the output or monitor resolution. 0 x 0 keeps the game's own render size; it does not mean native display resolution. Very large values can reduce performance or cause rendering problems." },
        { IDC_WINDOWED_WIDTH, L"Window size", L"Set the client size used in windowed mode." },
        { IDC_FRAME_RATE, L"Frame rate", L"Choose unlocked, 30 FPS, or 60 FPS timing." },
        { IDC_MIRROR_ROOM_FIX, L"Mirror Room frame rate", L"At 60 FPS, the mirror and lighting effects in Brookhaven Hospital's storeroom can render incorrectly. This temporarily caps the game at 30 FPS in that room, then restores your selected frame rate when you leave. Requires 60 FPS mode. Enabled by default." },
        { IDC_FMV_MODE, L"FMV aspect", L"Choose the framing used for full-motion video." },
        { IDC_REMOVE_CUTSCENE_BORDERS, L"Cutscene borders", L"Remove the horizontal borders added to in-engine cutscenes." },
        { IDC_FULLSCREEN_PAUSE, L"Fullscreen pause menu", L"Expand the in-game pause screen to widescreen." },
        { IDC_DOF_RES, L"Depth of field resolution", L"Set the depth-of-field and otherworld-corrosion target size. 0 keeps the game default." },
        { IDC_SHADOW_RES, L"Shadow resolution", L"Set the size used for dynamic shadow maps. 0 keeps the game default." },
        { IDC_INVENTORY_RES, L"Inventory background size (experimental)", L"Matches the PC Fix coordinate patch: changes the horizontal bounds of one pause-menu quad. It does not change texture dimensions or sharpen item icons." },
        { IDC_STATUS_RES, L"Status-screen resolution", L"Set the resolution used for the image on the Status tab." },
        { IDC_FOG_COMPLEXITY, L"Fog complexity", L"Set the fog-detail coefficient. The executable default is 25; 0 leaves the game's value unchanged." },
        { IDC_ANISOTROPIC, L"Anisotropic filtering", L"Use the highest anisotropic texture-filtering level supported by the device." },
        { IDC_DISABLE_MSAA_DOF, L"MSAA and depth of field", L"Stop multisample anti-aliasing requests that can corrupt depth of field. This setting does not add FXAA." },
        { IDC_REGISTRY_REDIRECT, L"Registry redirect", L"Use per-user registry paths for the running game folder." },
        { IDC_DISABLE_SAFE_MODE, L"Disable safe-mode reset", L"Skip the startup fallback that resets graphics settings and rewrites disp.ini after an abnormal exit." },
        { IDC_WHITE_BORDER, L"White border fix", L"Remove the bright extended window edge that can appear on modern Windows." },
        { IDC_CPU_AFFINITY, L"CPU affinity", L"0 lets Windows schedule normally. Enter a hexadecimal mask such as 0x3 to allow CPU 0 and CPU 1." },
        { IDC_EXCEPTION_HANDLER, L"Crash minidumps", L"Write crash dump files into crash_dumps beside the game executable." },
        { IDC_FIX_MUTED_AUDIO, L"Muted audio recovery", L"Clear a persisted mute on SH3's Windows audio session after the game starts. The check runs once when the game's audio session becomes available; it does not keep overriding volume changes during play." },
        { IDC_ENABLE_LOGGING, L"Plugin log", L"Write plugin startup and patch details to SH3Enhanced.log." },
        { IDC_ENABLE_RICH_PRESENCE, L"Discord Rich Presence", L"Show your current Silent Hill 3 area and health in Discord. Requires a valid Discord application ID in SH3Enhanced.ini and the bundled Game SDK. When disabled, the SDK is not checked or loaded." },
        { IDC_BUTTON_ICON_STYLE, L"Controller prompts", L"Choose which controller prompt family to show in Control Options. Off keeps the game's original labels." },
        { IDC_FIX_WISH_HOUSE, L"Wish House text fix", L"Correct English references to Hope House in the message archive." },
        { IDC_FIX_TYPOS, L"English typo fixes", L"Correct the identified subway, apartment, and hospital text errors." },
        { IDC_RESTORE_SEWER_TEXT, L"Missing sewer text", L"Restore the missing English text at the sewer wine rack." },
        { IDC_UNLOCK_SH2, L"Silent Hill 2 references", L"Show the Silent Hill 2 references without requiring a cleared SH2 save." },
    };

    int HelpTargetForControl(int id)
    {
        switch (id)
        {
        case IDC_LABEL_WINDOW_MODE: return IDC_WINDOW_MODE;
        case IDC_LABEL_OUTPUT_RES: return IDC_RESOLUTION_X;
        case IDC_LABEL_RENDER_RES: return IDC_RENDER_X;
        case IDC_LABEL_WINDOW_SIZE: return IDC_WINDOWED_WIDTH;
        case IDC_LABEL_FRAME_RATE: return IDC_FRAME_RATE;
        case IDC_LABEL_FMV_ASPECT: return IDC_FMV_MODE;
        case IDC_LABEL_DOF_RES: return IDC_DOF_RES;
        case IDC_LABEL_SHADOW_RES: return IDC_SHADOW_RES;
        case IDC_LABEL_INVENTORY_RES: return IDC_INVENTORY_RES;
        case IDC_LABEL_STATUS_RES: return IDC_STATUS_RES;
        case IDC_LABEL_FOG: return IDC_FOG_COMPLEXITY;
        case IDC_LABEL_CPU_MASK: return IDC_CPU_AFFINITY;
        default: return id;
        }
    }

    std::wstring ModuleFolder()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        PathRemoveFileSpecW(path);
        return path;
    }

    std::wstring GameFolder()
    {
        if (!g_executable.empty())
        {
            wchar_t folder[MAX_PATH]{};
            lstrcpynW(folder, g_executable.c_str(), MAX_PATH);
            PathRemoveFileSpecW(folder);
            return folder;
        }
        return {};
    }

    void AddExecutable(const std::wstring& path)
    {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        for (const auto& existing : g_executables)
            if (_wcsicmp(existing.c_str(), path.c_str()) == 0) return;
        g_executables.push_back(path);
    }

    void ShowDescription(int id)
    {
        for (const auto& entry : kHelp)
        {
            if (entry.id == id)
            {
                SetDlgItemTextW(g_main, IDC_HELP_TITLE, entry.title);
                SetDlgItemTextW(g_main, IDC_HELP_BODY, entry.description);
                return;
            }
        }
        SetDlgItemTextW(g_main, IDC_HELP_TITLE, L"Configuration help");
        SetDlgItemTextW(g_main, IDC_HELP_BODY, L"Hover over a setting or its label to see what it does.");
    }

    LRESULT CALLBACK ControlHoverProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        (void)subclassId;
        HWND page = reinterpret_cast<HWND>(referenceData);
        if (message == WM_MOUSEMOVE)
        {
            TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, control, 0 };
            TrackMouseEvent(&tracking);
            PostMessageW(g_main, WM_SHOW_HELP, static_cast<WPARAM>(HelpTargetForControl(GetDlgCtrlID(control))), 0);
        }
        else if (message == WM_SETFOCUS)
        {
            PostMessageW(g_main, WM_SHOW_HELP, static_cast<WPARAM>(HelpTargetForControl(GetDlgCtrlID(control))), 0);
        }
        else if (message == WM_MOUSELEAVE)
        {
            PostMessageW(g_main, WM_SHOW_HELP, 0, 0);
        }
        else if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(control, ControlHoverProc, 1);
        }
        return DefSubclassProc(control, message, wParam, lParam);
    }

    BOOL CALLBACK AttachHoverHandler(HWND control, LPARAM pageParam)
    {
        SetWindowSubclass(control, ControlHoverProc, 1, static_cast<DWORD_PTR>(pageParam));
        return TRUE;
    }

    bool IsChecked(HWND page, int id)
    {
        return IsDlgButtonChecked(page, id) == BST_CHECKED;
    }

    void SetCheck(HWND page, int id, bool value)
    {
        CheckDlgButton(page, id, value ? BST_CHECKED : BST_UNCHECKED);
    }

    void SetNumber(HWND page, int id, int value)
    {
        wchar_t text[32]{};
        swprintf_s(text, L"%d", value);
        SetDlgItemTextW(page, id, text);
    }

    int ReadNumber(HWND page, int id)
    {
        BOOL valid = FALSE;
        const UINT value = GetDlgItemInt(page, id, &valid, FALSE);
        return valid ? static_cast<int>(value) : 0;
    }

    void RefreshPages()
    {
        HWND page = g_pages[0];
        if (page)
        {
            SendDlgItemMessageW(page, IDC_WINDOW_MODE, CB_SETCURSEL, g_config.windowMode, 0);
            SetNumber(page, IDC_RESOLUTION_X, g_config.resolutionX); SetNumber(page, IDC_RESOLUTION_Y, g_config.resolutionY);
            SetNumber(page, IDC_RENDER_X, g_config.renderResolutionX); SetNumber(page, IDC_RENDER_Y, g_config.renderResolutionY);
            SetNumber(page, IDC_WINDOWED_WIDTH, g_config.windowedWidth); SetNumber(page, IDC_WINDOWED_HEIGHT, g_config.windowedHeight);
            const int fps = g_config.frameRateMode == 30 ? 1 : g_config.frameRateMode == 60 ? 2 : 0;
            SendDlgItemMessageW(page, IDC_FRAME_RATE, CB_SETCURSEL, fps, 0);
            SetCheck(page, IDC_MIRROR_ROOM_FIX, g_config.mirrorRoomFix);
            SendDlgItemMessageW(page, IDC_FMV_MODE, CB_SETCURSEL, g_config.fmvWidescreenMode, 0);
            SetCheck(page, IDC_REMOVE_CUTSCENE_BORDERS, g_config.removeCutsceneBorders);
            SetCheck(page, IDC_FULLSCREEN_PAUSE, g_config.fullscreenPauseMenu);
        }
        page = g_pages[1];
        if (page)
        {
            SetNumber(page, IDC_DOF_RES, g_config.dofResolution); SetNumber(page, IDC_SHADOW_RES, g_config.shadowsResolution);
            SetNumber(page, IDC_INVENTORY_RES, g_config.inventoryResolution); SetNumber(page, IDC_STATUS_RES, g_config.statusScreenResolution);
            SetNumber(page, IDC_FOG_COMPLEXITY, g_config.fogComplexity);
            SetCheck(page, IDC_ANISOTROPIC, g_config.anisotropicTextureFiltering); SetCheck(page, IDC_DISABLE_MSAA_DOF, g_config.disableMsaaForDof);
        }
        page = g_pages[2];
        if (page)
        {
            SetCheck(page, IDC_REGISTRY_REDIRECT, g_config.registryRedirect); SetCheck(page, IDC_DISABLE_SAFE_MODE, g_config.disableSafeMode);
            SetCheck(page, IDC_WHITE_BORDER, g_config.fixWhiteBorder);
            SetCheck(page, IDC_FIX_MUTED_AUDIO, g_config.fixMutedAudio);
            wchar_t affinity[16]{}; swprintf_s(affinity, L"0x%X", g_config.cpuAffinityMask); SetDlgItemTextW(page, IDC_CPU_AFFINITY, affinity);
            SetCheck(page, IDC_EXCEPTION_HANDLER, g_config.exceptionHandler); SetCheck(page, IDC_ENABLE_LOGGING, g_config.enableLogging);
            SetCheck(page, IDC_ENABLE_RICH_PRESENCE, g_config.enableRichPresence);
            SendDlgItemMessageW(page, IDC_BUTTON_ICON_STYLE, CB_SETCURSEL, g_config.replaceButtonText, 0);
        }
        page = g_pages[3];
        if (page)
        {
            SetCheck(page, IDC_FIX_WISH_HOUSE, g_config.fixWishHouse); SetCheck(page, IDC_FIX_TYPOS, g_config.fixEnglishTypos);
            SetCheck(page, IDC_RESTORE_SEWER_TEXT, g_config.restoreMissingSewerText); SetCheck(page, IDC_UNLOCK_SH2, g_config.unlockSH2EasterEggs);
        }
    }

    void LoadSelectedGame()
    {
        const int index = static_cast<int>(SendDlgItemMessageW(g_main, IDC_LAUNCH_EXE, CB_GETCURSEL, 0, 0));
        if (index < 0 || index >= static_cast<int>(g_executables.size())) return;
        const std::wstring selected = g_executables[index];
        if (_wcsicmp(PathFindFileNameW(selected.c_str()), L"sh3.exe") != 0)
        {
            MessageBoxW(g_main, L"Select sh3.exe from the Silent Hill 3 game folder.", L"Not a game executable", MB_OK | MB_ICONINFORMATION);
            return;
        }
        g_executable = selected;
        g_iniPath = GameFolder() + L"\\SH3Enhanced.ini";
        g_config = {};
        if (GetFileAttributesW(g_iniPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            g_config = sh3e::LoadConfig(g_iniPath);
        RefreshPages();
        SetDlgItemTextW(g_main, IDC_HELP_TITLE, L"Configuration");
        SetDlgItemTextW(g_main, IDC_HELP_BODY, L"Settings are read from and saved to SH3Enhanced.ini beside sh3.exe. Restart the game for changes to take effect.");
        EnableWindow(GetDlgItem(g_main, IDC_SAVE), TRUE);
        EnableWindow(GetDlgItem(g_main, IDC_SAVE_LAUNCH), TRUE);
    }

    bool ReadPages()
    {
        HWND page = g_pages[0];
        g_config.windowMode = static_cast<int>(SendDlgItemMessageW(page, IDC_WINDOW_MODE, CB_GETCURSEL, 0, 0));
        g_config.resolutionX = ReadNumber(page, IDC_RESOLUTION_X); g_config.resolutionY = ReadNumber(page, IDC_RESOLUTION_Y);
        g_config.renderResolutionX = ReadNumber(page, IDC_RENDER_X); g_config.renderResolutionY = ReadNumber(page, IDC_RENDER_Y);
        g_config.windowedWidth = ReadNumber(page, IDC_WINDOWED_WIDTH); g_config.windowedHeight = ReadNumber(page, IDC_WINDOWED_HEIGHT);
        const int fps = static_cast<int>(SendDlgItemMessageW(page, IDC_FRAME_RATE, CB_GETCURSEL, 0, 0));
        g_config.frameRateMode = fps == 1 ? 30 : fps == 2 ? 60 : 0;
        g_config.mirrorRoomFix = IsChecked(page, IDC_MIRROR_ROOM_FIX);
        g_config.fmvWidescreenMode = static_cast<int>(SendDlgItemMessageW(page, IDC_FMV_MODE, CB_GETCURSEL, 0, 0));
        g_config.removeCutsceneBorders = IsChecked(page, IDC_REMOVE_CUTSCENE_BORDERS);
        g_config.fullscreenPauseMenu = IsChecked(page, IDC_FULLSCREEN_PAUSE);

        page = g_pages[1];
        g_config.dofResolution = ReadNumber(page, IDC_DOF_RES); g_config.shadowsResolution = ReadNumber(page, IDC_SHADOW_RES);
        g_config.inventoryResolution = ReadNumber(page, IDC_INVENTORY_RES); g_config.statusScreenResolution = ReadNumber(page, IDC_STATUS_RES);
        g_config.fogComplexity = ReadNumber(page, IDC_FOG_COMPLEXITY);
        g_config.anisotropicTextureFiltering = IsChecked(page, IDC_ANISOTROPIC); g_config.disableMsaaForDof = IsChecked(page, IDC_DISABLE_MSAA_DOF);

        page = g_pages[2];
        g_config.registryRedirect = IsChecked(page, IDC_REGISTRY_REDIRECT); g_config.disableSafeMode = IsChecked(page, IDC_DISABLE_SAFE_MODE);
        g_config.fixWhiteBorder = IsChecked(page, IDC_WHITE_BORDER); g_config.exceptionHandler = IsChecked(page, IDC_EXCEPTION_HANDLER);
        g_config.fixMutedAudio = IsChecked(page, IDC_FIX_MUTED_AUDIO);
        g_config.enableLogging = IsChecked(page, IDC_ENABLE_LOGGING);
        g_config.enableRichPresence = IsChecked(page, IDC_ENABLE_RICH_PRESENCE);
        g_config.replaceButtonText = static_cast<int>(SendDlgItemMessageW(page, IDC_BUTTON_ICON_STYLE, CB_GETCURSEL, 0, 0));
        wchar_t mask[64]{}; GetDlgItemTextW(page, IDC_CPU_AFFINITY, mask, static_cast<int>(std::size(mask)));
        wchar_t* end{}; const unsigned long parsedMask = wcstoul(mask, &end, 0);
        if (end == mask || *end != L'\0')
        {
            MessageBoxW(g_main, L"CPU affinity mask must be a number such as 0 or 0x3.", L"Invalid setting", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(page, IDC_CPU_AFFINITY)); return false;
        }
        g_config.cpuAffinityMask = static_cast<std::uint32_t>(parsedMask);

        page = g_pages[3];
        g_config.fixWishHouse = IsChecked(page, IDC_FIX_WISH_HOUSE); g_config.fixEnglishTypos = IsChecked(page, IDC_FIX_TYPOS);
        g_config.restoreMissingSewerText = IsChecked(page, IDC_RESTORE_SEWER_TEXT); g_config.unlockSH2EasterEggs = IsChecked(page, IDC_UNLOCK_SH2);

        const auto validDimension = [](int n, int max) { return n >= 0 && n <= max; };
        if (!validDimension(g_config.resolutionX, 16384) || !validDimension(g_config.resolutionY, 16384) ||
            !validDimension(g_config.renderResolutionX, 16384) || !validDimension(g_config.renderResolutionY, 16384) ||
            !validDimension(g_config.dofResolution, 4096) || !validDimension(g_config.shadowsResolution, 4096) ||
            !validDimension(g_config.inventoryResolution, 1024) || !validDimension(g_config.statusScreenResolution, 4096) ||
            !validDimension(g_config.fogComplexity, 4096) || g_config.windowedWidth < 320 || g_config.windowedHeight < 320)
        {
            MessageBoxW(g_main, L"One or more values are outside the supported range.", L"Invalid setting", MB_OK | MB_ICONWARNING);
            return false;
        }
        if ((g_config.resolutionX == 0) != (g_config.resolutionY == 0) || (g_config.renderResolutionX == 0) != (g_config.renderResolutionY == 0))
        {
            MessageBoxW(g_main, L"For each resolution pair, set both dimensions to 0 or both to a positive value.", L"Invalid resolution", MB_OK | MB_ICONWARNING);
            return false;
        }
        return true;
    }

    void SaveAndLaunch(bool launch)
    {
        if (!ReadPages()) return;
        if (!sh3e::SaveConfig(g_iniPath, g_config))
        {
            MessageBoxW(g_main, L"Could not save SH3Enhanced.ini. Check that the game folder is writable.", L"Save failed", MB_OK | MB_ICONERROR);
            return;
        }
        SetDlgItemTextW(g_main, IDC_HELP_TITLE, L"Configuration saved");
        SetDlgItemTextW(g_main, IDC_HELP_BODY, L"Settings saved. Restart the game for the changes to take effect.");
        if (launch)
        {
            const std::wstring folder = GameFolder();
            SHELLEXECUTEINFOW info{ sizeof(info) }; info.fMask = SEE_MASK_NOASYNC; info.hwnd = g_main;
            info.lpVerb = L"open"; info.lpFile = g_executable.c_str(); info.lpDirectory = folder.c_str(); info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) MessageBoxW(g_main, L"Could not launch sh3.exe.", L"Launch failed", MB_OK | MB_ICONERROR);
        }
    }

    void BrowseForGame()
    {
        wchar_t path[MAX_PATH] = L"sh3.exe";
        OPENFILENAMEW dialog{ sizeof(dialog) };
        dialog.hwndOwner = g_main; dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH;
        dialog.lpstrFilter = L"Silent Hill 3 executable (sh3.exe)\0sh3.exe\0Executable files (*.exe)\0*.exe\0\0";
        dialog.lpstrTitle = L"Select Silent Hill 3 executable"; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        size_t selected = g_executables.size();
        for (size_t i = 0; i < g_executables.size(); ++i)
            if (_wcsicmp(g_executables[i].c_str(), path) == 0) selected = i;
        if (selected == g_executables.size())
        {
            g_executables.emplace_back(path);
            const wchar_t* filename = PathFindFileNameW(path);
            SendDlgItemMessageW(g_main, IDC_LAUNCH_EXE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(filename));
            selected = g_executables.size() - 1;
        }
        SendDlgItemMessageW(g_main, IDC_LAUNCH_EXE, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        LoadSelectedGame();
    }

    void ActivatePage(int selected)
    {
        if (selected < 0 || selected >= 4) return;
        for (int i = 0; i < 4; ++i) ShowWindow(g_pages[i], i == selected ? SW_SHOW : SW_HIDE);
        SetFocus(g_pages[selected]);
    }

    RECT ControlRect(HWND parent, int id)
    {
        RECT rect{};
        HWND control = GetDlgItem(parent, id);
        if (control)
        {
            GetWindowRect(control, &rect);
            MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
        }
        return rect;
    }

    void MoveControl(HWND parent, int id, int x, int y, int width, int height)
    {
        if (HWND control = GetDlgItem(parent, id))
            MoveWindow(control, x, y, width, height, TRUE);
    }

    void LayoutMain(HWND window)
    {
        if (!g_tabs || !g_pages[0]) return;
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right;
        const int height = client.bottom;
        const int helpTitleY = height - 154;
        MoveControl(window, IDC_TABS, 4, 4, width - 8, helpTitleY - 10);

        RECT tabRect{};
        GetWindowRect(g_tabs, &tabRect);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&tabRect), 2);
        RECT pageRect{ 0, 0, tabRect.right - tabRect.left, tabRect.bottom - tabRect.top };
        TabCtrl_AdjustRect(g_tabs, FALSE, &pageRect);
        for (HWND page : g_pages)
            if (page)
                SetWindowPos(page, HWND_TOP, tabRect.left + pageRect.left, tabRect.top + pageRect.top,
                    pageRect.right - pageRect.left, pageRect.bottom - pageRect.top, SWP_NOACTIVATE);

        MoveControl(window, IDC_HELP_TITLE, 12, helpTitleY, width - 24, 22);
        MoveControl(window, IDC_HELP_RULE, 12, height - 126, width - 24, 2);

        const RECT closeRect = ControlRect(window, IDC_CLOSE);
        const RECT defaultsRect = ControlRect(window, IDC_DEFAULTS);
        const RECT launchLabelRect = ControlRect(window, IDC_LAUNCH_LABEL);
        const RECT comboRect = ControlRect(window, IDC_LAUNCH_EXE);
        const RECT browseRect = ControlRect(window, IDC_BROWSE_EXE);
        const RECT saveRect = ControlRect(window, IDC_SAVE);
        const RECT launchRect = ControlRect(window, IDC_SAVE_LAUNCH);
        const int buttonHeight = max(24, closeRect.bottom - closeRect.top);
        const int buttonY = height - buttonHeight - 8;
        const int closeWidth = max(64, closeRect.right - closeRect.left);
        const int defaultsWidth = max(90, defaultsRect.right - defaultsRect.left);
        const int launchLabelWidth = max(82, launchLabelRect.right - launchLabelRect.left);
        const int comboWidth = max(94, comboRect.right - comboRect.left);
        const int browseWidth = max(24, browseRect.right - browseRect.left);
        const int saveWidth = max(54, saveRect.right - saveRect.left);
        const int saveLaunchWidth = max(126, launchRect.right - launchRect.left);
        constexpr int gap = 6;
        const int launchGroupWidth = launchLabelWidth + gap + comboWidth + gap + browseWidth + gap + saveWidth + gap + saveLaunchWidth;
        int x = width - 8 - launchGroupWidth;
        MoveControl(window, IDC_LAUNCH_LABEL, x, buttonY, launchLabelWidth, buttonHeight);
        MoveControl(window, IDC_LAUNCH_EXE, x + launchLabelWidth + gap, buttonY, comboWidth, buttonHeight);
        MoveControl(window, IDC_BROWSE_EXE, x + launchLabelWidth + gap + comboWidth + gap, buttonY, browseWidth, buttonHeight);
        MoveControl(window, IDC_SAVE, x + launchLabelWidth + gap + comboWidth + gap + browseWidth + gap, buttonY, saveWidth, buttonHeight);
        MoveControl(window, IDC_SAVE_LAUNCH, width - 8 - saveLaunchWidth, buttonY, saveLaunchWidth, buttonHeight);
        MoveControl(window, IDC_DEFAULTS, 8 + closeWidth + gap, buttonY, defaultsWidth, buttonHeight);
        MoveControl(window, IDC_CLOSE, 8, buttonY, closeWidth, buttonHeight);
        MoveControl(window, IDC_HELP_BODY, 12, height - 118, width - 24, max(42, buttonY - (height - 118) - 10));
    }

    INT_PTR CALLBACK PageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INITDIALOG)
        {
            EnumChildWindows(page, AttachHoverHandler, reinterpret_cast<LPARAM>(page));
            const auto addOptions = [page](int id, const wchar_t* const* options, int count)
            {
                HWND combo = GetDlgItem(page, id);
                for (int i = 0; i < count; ++i)
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(options[i]));
            };
            constexpr const wchar_t* windowModes[] = { L"Fullscreen", L"Windowed", L"Borderless fullscreen" };
            constexpr const wchar_t* frameRates[] = { L"Unlocked", L"30 FPS", L"60 FPS" };
            constexpr const wchar_t* fmvModes[] = { L"4:3", L"16:9 (crop)", L"16:9 source" };
            constexpr const wchar_t* buttonStyles[] = { L"Off", L"Generic", L"Xbox", L"PlayStation", L"Nintendo" };
            addOptions(IDC_WINDOW_MODE, windowModes, static_cast<int>(std::size(windowModes)));
            addOptions(IDC_FRAME_RATE, frameRates, static_cast<int>(std::size(frameRates)));
            addOptions(IDC_FMV_MODE, fmvModes, static_cast<int>(std::size(fmvModes)));
            addOptions(IDC_BUTTON_ICON_STYLE, buttonStyles, static_cast<int>(std::size(buttonStyles)));
            return TRUE;
        }
        if (message == WM_COMMAND && HIWORD(wParam) != EN_CHANGE)
            PostMessageW(g_main, WM_SHOW_HELP,
                static_cast<WPARAM>(HelpTargetForControl(LOWORD(wParam))), 0);
        return FALSE;
    }

    INT_PTR CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_INITDIALOG:
        {
            g_main = window; g_tabs = GetDlgItem(window, IDC_TABS);
            HICON appIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_CONFIG_TOOL));
            SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
            SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
            HWND fontControl = GetDlgItem(window, IDC_HELP_TITLE);
            SendMessageW(fontControl, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

            for (int i = 0; i < 4; ++i)
            {
                TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(kTabNames[i]);
                TabCtrl_InsertItem(g_tabs, i, &item);
                g_pages[i] = CreateDialogParamW(g_instance, MAKEINTRESOURCEW(kPageIds[i]), window, PageProc, 0);
            }
            LayoutMain(window);

            const std::wstring besideTool = ModuleFolder() + L"\\sh3.exe";
            AddExecutable(besideTool);
            if (g_executables.empty())
            {
                HKEY key{};
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\KONAMI\\SILENT HILL 3", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
                {
                    wchar_t install[MAX_PATH]{}; DWORD bytes = sizeof(install);
                    if (RegQueryValueExW(key, L"installdir", nullptr, nullptr, reinterpret_cast<BYTE*>(install), &bytes) == ERROR_SUCCESS)
                        AddExecutable(std::wstring(install) + L"\\sh3.exe");
                    RegCloseKey(key);
                }
            }
            for (const auto& exe : g_executables)
                SendDlgItemMessageW(window, IDC_LAUNCH_EXE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(PathFindFileNameW(exe.c_str())));
            SendDlgItemMessageW(window, IDC_LAUNCH_EXE, CB_SETCURSEL, g_executables.empty() ? -1 : 0, 0);
            EnableWindow(GetDlgItem(window, IDC_SAVE), !g_executables.empty());
            EnableWindow(GetDlgItem(window, IDC_SAVE_LAUNCH), !g_executables.empty());
            if (!g_executables.empty()) LoadSelectedGame();
            else
            {
                SetDlgItemTextW(window, IDC_HELP_TITLE, L"Select the game");
                SetDlgItemTextW(window, IDC_HELP_BODY, L"Use the ... button to select sh3.exe. Settings are saved beside that executable.");
            }
            ActivatePage(0);
            return TRUE;
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_TABS && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
                ActivatePage(TabCtrl_GetCurSel(g_tabs));
            return FALSE;
        case WM_SIZE:
            LayoutMain(window);
            return TRUE;
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = 620;
            limits->ptMinTrackSize.y = 500;
            return TRUE;
        }
        case WM_SHOW_HELP:
            ShowDescription(static_cast<int>(wParam)); return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_CLOSE: EndDialog(window, 0); return TRUE;
            case IDC_DEFAULTS:
                if (MessageBoxW(window, L"Reset the displayed settings to their defaults? Click Save to write them to the INI file.", L"Default settings", MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    g_config = {}; RefreshPages();
                    SetDlgItemTextW(window, IDC_HELP_TITLE, L"Default settings");
                    SetDlgItemTextW(window, IDC_HELP_BODY, L"Defaults are loaded. Click Save to write them to SH3Enhanced.ini.");
                }
                return TRUE;
            case IDC_SAVE: SaveAndLaunch(false); return TRUE;
            case IDC_SAVE_LAUNCH: SaveAndLaunch(true); return TRUE;
            case IDC_BROWSE_EXE: BrowseForGame(); return TRUE;
            case IDC_LAUNCH_EXE:
                if (HIWORD(wParam) == CBN_SELCHANGE) LoadSelectedGame();
                return TRUE;
            }
            return FALSE;
        case WM_CLOSE: EndDialog(window, 0); return TRUE;
        }
        return FALSE;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    g_instance = instance;
    return static_cast<int>(DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainProc, 0));
}
