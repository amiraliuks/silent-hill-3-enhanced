#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sh3e
{
    struct BytePattern
    {
        std::vector<int> bytes; // -1 is a wildcard.
        static std::optional<BytePattern> Parse(const std::string& text);
    };
    struct PatternMatch { std::uintptr_t address = 0; std::uintptr_t rva = 0; };
    std::vector<PatternMatch> FindPattern(HMODULE module, const BytePattern& pattern);
    bool WriteMemory(std::uintptr_t address, const void* data, std::size_t size);
}