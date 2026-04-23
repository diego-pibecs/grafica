#pragma once

#include <chrono>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace DebugLog
{
void Initialize();
void Write(std::string_view level, std::string_view tag, const std::string& message);

template <typename... Args>
std::string BuildMessage(Args&&... args)
{
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return stream.str();
}

template <typename... Args>
void Info(std::string_view tag, Args&&... args)
{
    Write("INFO", tag, BuildMessage(std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::string_view tag, Args&&... args)
{
    Write("ERROR", tag, BuildMessage(std::forward<Args>(args)...));
}

class ScopedTrace
{
public:
    explicit ScopedTrace(std::string_view tag, std::string_view label);
    ~ScopedTrace();

private:
    std::string tag_;
    std::string label_;
    std::chrono::steady_clock::time_point begin_;
};
}
