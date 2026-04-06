#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "BaseScene.h"
#include "CameraController.h"
#include "InputState.h"
#include "PlayerController.h"

struct GLFWwindow;

class App
{
public:
    App();
    ~App();

    int Run();

private:
    GLFWwindow* window_ = nullptr;
    std::filesystem::path assetsRoot_;
    InputState input_;
    CameraController camera_;
    std::unique_ptr<BaseScene> scene_;
    int framebufferWidth_ = 1280;
    int framebufferHeight_ = 720;

    bool Init();
    void Shutdown();
    void Update();
    void Render();
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
