#include "pattern.h"
#include <cstring>
#include <sstream>

std::optional<sh3e::BytePattern> sh3e::BytePattern::Parse(const std::string& text)
{
    BytePattern result;
    std::istringstream stream(text);
    std::string token;

    while (stream >> token)
    {
        if (token == "?" || token == "??") { result.bytes.push_back(-1); continue; }
        if (token.size() != 2) return std::nullopt;
        try { result.bytes.push_back(std::stoi(token, nullptr, 16)); }
        catch (...) { return std::nullopt; }
    }
    
    if (result.bytes.empty()) return std::nullopt;
    return result;
}

std::vector<sh3e::PatternMatch> sh3e::FindPattern(HMODULE module, const BytePattern& pattern)
{
    std::vector<PatternMatch> result;
    if (!module || pattern.bytes.empty()) return result;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return result;
    
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index)
    {
        const auto* section = IMAGE_FIRST_SECTION(nt) + index;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const auto length = static_cast<std::size_t>(section->Misc.VirtualSize);
        if (length < pattern.bytes.size()) continue;
        const auto* begin = base + section->VirtualAddress;

        for (std::size_t offset = 0; offset <= length - pattern.bytes.size(); ++offset)
        {
            bool match = true;
            for (std::size_t i = 0; i < pattern.bytes.size(); ++i)
                if (pattern.bytes[i] >= 0 && begin[offset + i] != static_cast<std::uint8_t>(pattern.bytes[i])) { match = false; break; }
            if (match) result.push_back({reinterpret_cast<std::uintptr_t>(begin + offset), section->VirtualAddress + offset});
        }
    }
    
    return result;
}

bool sh3e::WriteMemory(std::uintptr_t address, const void* data, std::size_t size)
{
    if (!address || !data || size == 0) return false;
    auto* target = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    std::memcpy(target, data, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    return VirtualProtect(target, size, oldProtection, &ignored) != FALSE;
}