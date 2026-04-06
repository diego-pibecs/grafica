#include "App.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#ifndef APP_ASSET_DIR
#define APP_ASSET_DIR "assets"
#endif

App::App()
    : assetsRoot_(APP_ASSET_DIR)
{
}

App::~App()
{
    Shutdown();
}

int App::Run()
{
    if (!Init())
    {
        return EXIT_FAILURE;
    }

    while (!glfwWindowShouldClose(window_))
    {
        input_.BeginFrame(static_cast<float>(glfwGetTime()));
        glfwPollEvents();
        Update();
        Render();
        glfwSwapBuffers(window_);
        input_.EndFrame();
    }

    return EXIT_SUCCESS;
}

bool App::Init()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window_ = glfwCreateWindow(framebufferWidth_, framebufferHeight_, "Grafica BBB Base", nullptr, nullptr);
    if (window_ == nullptr)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);

    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    glfwSetKeyCallback(window_, KeyCallback);
    glfwSetCursorPosCallback(window_, CursorPosCallback);
    glfwSetScrollCallback(window_, ScrollCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW\n";
        return false;
    }

    glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
    glViewport(0, 0, framebufferWidth_, framebufferHeight_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    try
    {
        scene_ = std::make_unique<BaseScene>(assetsRoot_);
        scene_->Init();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return false;
    }

    SetMouseCaptured(true);
    camera_.SetPlayerAnchor(player_.GetEyePosition());
    camera_.SetOrbitTarget(player_.GetOrbitTarget());
    UpdateWindowTitle();
    return true;
}

void App::Shutdown()
{
    scene_.reset();

    if (window_ != nullptr)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

void App::Update()
{
    if (input_.WasKeyPressed(GLFW_KEY_ESCAPE))
    {
        SetMouseCaptured(!input_.mouseCaptured);
    }

    if (input_.WasKeyPressed(GLFW_KEY_TAB))
    {
        camera_.ToggleMode();
        UpdateWindowTitle();
    }

    camera_.Update(input_);
    player_.Update(
        input_,
        camera_.GetMovementYawDegrees(),
        [this](const glm::vec3& currentPosition, const glm::vec3& desiredPosition, float playerRadius, float playerHeight)
        {
            return scene_->ResolvePlayerMovement(currentPosition, desiredPosition, playerRadius, playerHeight);
        });
    camera_.SetPlayerAnchor(player_.GetEyePosition());
    camera_.SetOrbitTarget(player_.GetOrbitTarget());
    scene_->Update(player_.GetSnapshot(), input_.deltaTime, input_.lastFrameTime);
}

void App::Render()
{
    const float aspectRatio = framebufferHeight_ > 0
        ? static_cast<float>(framebufferWidth_) / static_cast<float>(framebufferHeight_)
        : (16.0f / 9.0f);

    const glm::mat4 projection = glm::perspective(
        glm::radians(camera_.GetFovDegrees()),
        aspectRatio,
        0.1f,
        100.0f);

    glClearColor(0.07f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    scene_->Render(camera_, projection);
}

void App::SetMouseCaptured(bool captured)
{
    input_.mouseCaptured = captured;
    input_.ResetMouseReference();
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void App::UpdateWindowTitle() const
{
    const std::string modeLabel = camera_.GetMode() == CameraMode::Fps ? "FPS" : "Orbit";
    const std::string title = "Grafica BBB Base | " + modeLabel + " | " + scene_->GetActiveModelLabel();
    glfwSetWindowTitle(window_, title.c_str());
}

void App::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
    {
        app->HandleFramebufferSize(width, height);
    }
}

void App::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
    {
        app->HandleKey(key, scancode, action, mods);
    }
}

void App::CursorPosCallback(GLFWwindow* window, double xPos, double yPos)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
    {
        app->HandleCursorPosition(xPos, yPos);
    }
}

void App::ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
    {
        app->HandleScroll(xOffset, yOffset);
    }
}

void App::HandleFramebufferSize(int width, int height)
{
    framebufferWidth_ = width;
    framebufferHeight_ = height;
    glViewport(0, 0, width, height);
}

void App::HandleKey(int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    input_.SetKeyState(key, action);

    if (key == GLFW_KEY_Q && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void App::HandleCursorPosition(double xPos, double yPos)
{
    if (!input_.mouseCaptured)
    {
        return;
    }

    input_.RegisterMousePosition(xPos, yPos);
}

void App::HandleScroll(double xOffset, double yOffset)
{
    (void)xOffset;
    input_.RegisterScroll(yOffset);
}
