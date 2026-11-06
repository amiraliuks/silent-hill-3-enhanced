#pragma once

#include <string>

namespace sh3e
{
    struct GameState
    {
        int locationGroup = -1;
        int locationId = -1;
        float healthPercent = 0.0f;
        bool hasHealth = false;
        bool isMainMenu = false;
        std::string location;
        std::string health;
    };

    GameState ReadGameState();
}