#pragma once
#include "../pattern.h"
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace sh3e::patches::detail
{
    inline std::string Hex(std::uintptr_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << std::uppercase << value;
        return out.str();
    }

    inline bool EncodeRelativeJump(std::uint8_t* bytes, std::uintptr_t sourceAddress, const void* destination)
    {
        const auto destinationAddress = reinterpret_cast<std::intptr_t>(destination);
        const auto displacement = destinationAddress - (static_cast<std::intptr_t>(sourceAddress) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
            return false;

        const auto relative = static_cast<std::int32_t>(displacement);
        bytes[0] = 0xE9;
        std::memcpy(bytes + 1, &relative, sizeof(relative));
        return true;
    }

    inline bool RedirectFloatOperand(const PatternMatch& match, const float* value)
    {
        const auto pointer = reinterpret_cast<std::uintptr_t>(value);
        if (pointer > UINT32_MAX)
            return false;

        const auto pointer32 = static_cast<std::uint32_t>(pointer);
        return WriteMemory(match.address + 2, &pointer32, sizeof(pointer32));
    }
}