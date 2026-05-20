#pragma once

#include <filesystem>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "BaseScene.h"
#include "CameraController.h"
#include "InputState.h"
#include "PlayerController.h"
#include "navigation/IWalkableWorld.h"

struct GLFWwindow;
class DebugOverlayRenderer;

class App
{
public:
    App();
    ~App();

    int Run();

private:
    struct TutorialItem
    {
        std::string id;
        std::string label;
        bool required = true;
        bool completed = false;
        float completedTime = 0.0f;
    };

    struct PendingNavigationBuild
    {
        std::future<std::unique_ptr<IWalkableWorld>> future;
    };

    GLFWwindow* window_ = nullptr;
    std::filesystem::path assetsRoot_;
    InputState input_;
    CameraController camera_;
    std::unique_ptr<BaseScene> scene_;
    std::unique_ptr<DebugOverlayRenderer> debugOverlayRenderer_;
    std::unique_ptr<IWalkableWorld> walkableWorld_;
    int framebufferWidth_ = 1280;
    int framebufferHeight_ = 720;
    bool physicsDebugEnabled_ = false;
    bool performanceTitleEnabled_ = false;
    bool paused_ = false;
    bool tutorialVisible_ = true;
    float tutorialAllCompletedTime_ = -1.0f;
    std::vector<TutorialItem> tutorialItems_;
    double displayedFps_ = 0.0;
    std::uint64_t frameIndex_ = 0;
    bool traceCurrentFrame_ = false;
    std::unique_ptr<PendingNavigationBuild> pendingNavigationBuild_;

    bool Init();
    void Shutdown();
    void Update();
    void Render();
    void InitializeTutorial();
    void MarkTutorialItemCompleted(const std::string& id);
    void UpdateTutorial(bool interacted);
    void RenderTutorial() const;
    void SetMouseCaptured(bool captured);
    void UpdateWindowTitle() const;

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xPos, double yPos);
    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    void HandleFramebufferSize(int width, int height);
    void HandleKey(int key, int scancode, int action, int mods);
    void HandleCursorPosition(double xPos, double yPos);
    void HandleScroll(double xOffset, double yOffset);

    PlayerController player_;
};
