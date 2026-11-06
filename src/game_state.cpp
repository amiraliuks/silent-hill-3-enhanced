#include "game_state.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sh3e
{
namespace
{
    constexpr std::uintptr_t kLocationGroupOffset = 0x32D284;
    constexpr std::uintptr_t kLocationIdOffset = 0x7577E8;
    constexpr std::uintptr_t kHealthOffset = 0x498668;

    struct LocationEntry
    {
        int group;
        int id;
        const char* name;
    };

    constexpr std::array kLocations{
        LocationEntry{128, 8, "Central Square Shopping Center"},
        LocationEntry{170, 11, "Central Square Shopping Center"},
        LocationEntry{170, 12, "Central Square Shopping Center"},
        LocationEntry{128, 9, "Happy Burger"},
        LocationEntry{220, 8, "Happy Burger"},
        LocationEntry{150, 8, "Hazel Street Subway"},
        LocationEntry{150, 13, "Hazel Street Subway"},
        LocationEntry{90, 15, "In the Train"},
        LocationEntry{160, 15, "In the Train"},
        LocationEntry{160, 16, "Sewers"},
        LocationEntry{160, 17, "Sewers"},
        LocationEntry{170, 18, "Construction Site"},
        LocationEntry{160, 19, "Hilltop Center"},
        LocationEntry{160, 20, "Hilltop Center"},
        LocationEntry{160, 21, "Hilltop Center"},
        LocationEntry{190, 22, "Hilltop Center (Otherworld)"},
        LocationEntry{190, 23, "Hilltop Center (Otherworld)"},
        LocationEntry{190, 24, "Hilltop Center (Otherworld)"},
        LocationEntry{190, 25, "Hilltop Center (Otherworld)"},
        LocationEntry{170, 26, "Bergen Street"},
        LocationEntry{170, 27, "Daisy Villa Apartments"},
        LocationEntry{34, 28, "Driving to Silent Hill"},
        LocationEntry{180, 29, "Jack's Inn - Room 106"},
        LocationEntry{160, 30, "Roaming through Silent Hill"},
        LocationEntry{180, 31, "Heaven's Night"},
        LocationEntry{160, 32, "Brookhaven Hospital"},
        LocationEntry{160, 33, "Brookhaven Hospital"},
        LocationEntry{160, 34, "Brookhaven Hospital"},
        LocationEntry{160, 35, "Brookhaven Hospital"},
        LocationEntry{200, 36, "Brookhaven Hospital"},
        LocationEntry{180, 37, "Brookhaven Hospital (Otherworld)"},
        LocationEntry{180, 38, "Brookhaven Hospital (Otherworld)"},
        LocationEntry{180, 39, "Brookhaven Hospital (Otherworld)"},
        LocationEntry{200, 39, "Brookhaven Hospital (Otherworld)"},
        LocationEntry{220, 40, "Lakeside Amusement Park"},
        LocationEntry{220, 41, "Lakeside Amusement Park"},
        LocationEntry{220, 42, "Lakeside Amusement Park"},
        LocationEntry{180, 42, "Lakeside Amusement Park"},
        LocationEntry{128, 43, "Exploring the Chapel"},
        LocationEntry{190, 44, "Exploring the Chapel"},
        LocationEntry{190, 45, "Exploring the Chapel"},
        LocationEntry{220, 44, "Chapel (Otherworld)"},
        LocationEntry{220, 45, "Chapel (Otherworld)"},
        LocationEntry{220, 46, "Chapel (Otherworld)"},
    };

    bool ReadMemory(std::uintptr_t address, void* value, std::size_t size)
    {
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            value, size, &bytesRead) != FALSE && bytesRead == size;
    }

    bool IsMainMenu(int group, int id)
    {
        return (group == 0 && id == 0) ||
            (group == 220 && (id == 0 || id == 5 || id == 7));
    }

    bool IsLoading(int group, int id)
    {
        return (group == 96 && id == 40) ||
            (group == 128 && (id == 5 || id == 15 || id == 18 || id == 19 ||
                id == 20 || id == 23 || id == 25 || id == 32 || id == 36 || id == 37 || id == 38)) ||
            (group == 220 && (id == 30 || id == 32 || id == 37));
    }

    const char* GetLocationName(int group, int id)
    {
        if (IsMainMenu(group, id))
            return "Main Menu";
        if (IsLoading(group, id))
            return "Passing Through the Fog...";

        for (const auto& entry : kLocations)
        {
            if (entry.group == group && entry.id == id)
                return entry.name;
        }

        return "Exploring Silent Hill";
    }

    const char* GetHealthDescription(float health)
    {
        if (health <= 25.0f)
            return "Danger";

        if (health <= 60.0f)
            return "Caution";

        return "Fine";
    }
}

GameState ReadGameState()
{
    GameState state{};
    const auto executable = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (executable == 0)
        return state;

    if (!ReadMemory(executable + kLocationGroupOffset, &state.locationGroup, sizeof(state.locationGroup)) ||
        !ReadMemory(executable + kLocationIdOffset, &state.locationId, sizeof(state.locationId)))
        return state;

    state.isMainMenu = IsMainMenu(state.locationGroup, state.locationId);
    state.location = GetLocationName(state.locationGroup, state.locationId);

    if (!state.isMainMenu && ReadMemory(executable + kHealthOffset, &state.healthPercent, sizeof(state.healthPercent)) &&
        std::isfinite(state.healthPercent) && state.healthPercent >= 0.0f && state.healthPercent <= 100.0f)
    {
        char healthText[48]{};
        sprintf_s(healthText, "Health: %s (%.0f%%)", GetHealthDescription(state.healthPercent), state.healthPercent);
        state.health = healthText;
        state.hasHealth = true;
    }

    return state;
}
}