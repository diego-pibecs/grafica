#include "App.h"
#include "DebugLog.h"

#include <exception>

int main()
{
    DebugLog::Initialize();
    DebugLog::Info("Main", "Process start");

    try
    {
        App app;
        const int exitCode = app.Run();
        DebugLog::Info("Main", "Process exit with code ", exitCode);
        return exitCode;
    }
    catch (const std::exception& error)
    {
        DebugLog::Error("Main", "Unhandled exception: ", error.what());
    }
    catch (...)
    {
        DebugLog::Error("Main", "Unhandled unknown exception");
    }

    return 1;
}
