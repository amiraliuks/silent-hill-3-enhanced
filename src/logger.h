#pragma once
#include <string>

namespace sh3e::log
{
    void SetStartupStart();
    void Initialize(const std::wstring& directory);
    void Write(const char* level, const std::string& message);
}