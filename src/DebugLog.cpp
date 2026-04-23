#include "DebugLog.h"

#include <iostream>
#include <mutex>
#include <thread>

namespace
{
std::mutex gLogMutex;
std::chrono::steady_clock::time_point gStartTime = std::chrono::steady_clock::now();
bool gInitialized = false;
}

void DebugLog::Initialize()
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    if (gInitialized)
    {
        return;
    }

    gStartTime = std::chrono::steady_clock::now();
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::clog << std::unitbuf;
    gInitialized = true;
}

void DebugLog::Write(std::string_view level, std::string_view tag, const std::string& message)
{
    Initialize();

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - gStartTime).count();

    std::ostringstream line;
    line
        << "[+" << elapsedMs << " ms]"
        << "[tid " << std::this_thread::get_id() << "]"
        << "[" << level << "]"
        << "[" << tag << "] "
        << message;

    std::lock_guard<std::mutex> lock(gLogMutex);
    std::clog << line.str() << '\n';
}

DebugLog::ScopedTrace::ScopedTrace(std::string_view tag, std::string_view label)
    : tag_(tag)
    , label_(label)
    , begin_(std::chrono::steady_clock::now())
{
    DebugLog::Info(tag_, "BEGIN ", label_);
}

DebugLog::ScopedTrace::~ScopedTrace()
{
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin_).count();
    DebugLog::Info(tag_, "END ", label_, " (", elapsedMs, " ms)");
}
