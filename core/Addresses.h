#pragma once
#include <cstdint>

// ============================================================
// Addresses.h
//
// Collection of known static addresses used by the game.
// 
// Some values in SH3 are stored at fixed locations in memory,
// so they can be accessed directly without pattern scanning.
// More fragile code hooks are handled through sig scanning
// elsewhere in the project
// 
// Addresses below are based on:
//  sh3.exe CRC32: 372ae792
//  Base address:  0x00400000 (standard PE load address)
//
// Reference:
// Polymega / SilentHillDatabase
// https://github.com/Polymega/SilentHillDatabase/blob/master/SH3/SH3%20PC%20memory%20addresses
// ============================================================

namespace SH3Addr
{
	// Camera / Projection values
	constexpr uintptr_t CameraProjection = 0x00416330; // float
	constexpr uintptr_t CameraProjectionH = 0x00742AB4; // float - horizontal
	constexpr uintptr_t CameraProjectionV = 0x00742AB8; // float - vertical

    // Aspect ratio and render distances
    constexpr uintptr_t ScreenRatioH = 0x00743BC8; // float
    constexpr uintptr_t ScreenRatioV = 0x00743BCC; // float
    constexpr uintptr_t DrawDistanceFront = 0x00743BD0; // float
    constexpr uintptr_t DrawDistanceBack = 0x00743BD4; // float

    // Window resolution
    constexpr uintptr_t WindowWidth = 0x0072C780; // 4 bytes
    constexpr uintptr_t WindowHeight = 0x0072C784; // 4 bytes

    // Internal frame limiter / game speed value
    constexpr uintptr_t InGameSpeed = 0x0072C81A; // byte

    // Fog settings
    constexpr uintptr_t FogDisable = 0x006B8310; // byte  - set 0 to disable fog
    constexpr uintptr_t FogCoverDistance = 0x006B293C; // float - cover-up fog distance
    constexpr uintptr_t FogCoverIntensity = 0x00743B4C; // float - cover-up fog intensity

    // Area lighting / ambient colors
    constexpr uintptr_t LocationColorR = 0x006B2954; // float
    constexpr uintptr_t LocationColorG = 0x006B2958; // float
    constexpr uintptr_t LocationColorB = 0x006B295C; // float

    constexpr uintptr_t LightningColorR = 0x00743B08; // float
    constexpr uintptr_t LightningColorG = 0x00743B0C; // float
    constexpr uintptr_t LightningColorB = 0x00743B10; // float

    constexpr uintptr_t LocationIntensityR = 0x00743B18; // float
    constexpr uintptr_t LocationIntensityG = 0x00743B1C; // float
    constexpr uintptr_t LocationIntensityB = 0x00743B20; // float

    constexpr uintptr_t ColorFilterR = 0x00743B28; // float
    constexpr uintptr_t ColorFilterG = 0x00743B2C; // float
    constexpr uintptr_t ColorFilterB = 0x00743B30; // float

    // Flashlight Geometry
    constexpr uintptr_t FlashlightGeo0 = 0x006B76B0; // float
    constexpr uintptr_t FlashlightGeo1 = 0x006B76D4; // float
    constexpr uintptr_t FlashlightGeo2 = 0x006B76F0; // float
    constexpr uintptr_t FlashlightGeo3 = 0x006B76F4; // float
    constexpr uintptr_t FlashlightGeo4 = 0x006B7724; // float
    constexpr uintptr_t FlashlightGeo5 = 0x006B7734; // float

    // Heather state
    constexpr uintptr_t HeatherHealth = 0x00898660; // float
    constexpr uintptr_t HeatherPosZ = 0x008984E0; // float
    constexpr uintptr_t HeatherPosX = 0x008984E8; // float

    // Room / State
    constexpr uintptr_t CurrentRoomIndex = 0x0072D2C0; // 4 bytes
    constexpr uintptr_t CurrentRoomIndex2 = 0x0712C26C; // 4 bytes (mirror)

    // Stats (end-of-game results)
    constexpr uintptr_t IGT = 0x070E66F4; // float
    constexpr uintptr_t ActionLevel = 0x070E66DE; // byte
    constexpr uintptr_t RiddleLevel = 0x070E66DF; // byte

    // Inventory / ammo
    constexpr uintptr_t WeaponsSum1 = 0x0712CA80; // byte (bitmask)
    constexpr uintptr_t WeaponsSum2 = 0x0712CA81; // byte (bitmask)
    constexpr uintptr_t AmmoArray = 0x0712CAA0; // 2-byte array[13]

    // Camera controls
    constexpr uintptr_t CameraTilt = 0x0711A708; // float
    constexpr uintptr_t CameraPan = 0x0711A70C; // float
    constexpr uintptr_t ScreenRotation = 0x0711A710; // float

    // Elevator / Misc Effects
    constexpr uintptr_t ElevatorShake = 0x0711A634; // byte
    constexpr uintptr_t HPBarTrigger = 0x070E6724; // byte
}