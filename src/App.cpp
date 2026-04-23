#include "App.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <chrono>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "DebugLog.h"
#include "import/ImportedGeometry.h"
#include "navigation/RecastNavigationWorld.h"

#ifndef APP_ASSET_DIR
#define APP_ASSET_DIR "assets"
#endif

namespace
{
bool ShouldTraceFrame(std::uint64_t frameIndex)
{
    return frameIndex <= 180u || (frameIndex % 120u) == 0u;
}

void MergeDebugFrame(PhysicsDebugFrame& destination, const PhysicsDebugFrame& source)
{
    destination.points.insert(destination.points.end(), source.points.begin(), source.points.end());
    destination.lines.insert(destination.lines.end(), source.lines.begin(), source.lines.end());
    destination.triangles.insert(destination.triangles.end(), source.triangles.begin(), source.triangles.end());
}
}

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
    DebugLog::Info("App", "Run() begin");
    if (!Init())
    {
        DebugLog::Error("App", "Init() failed");
        return EXIT_FAILURE;
    }

    while (!glfwWindowShouldClose(window_))
    {
        ++frameIndex_;
        traceCurrentFrame_ = ShouldTraceFrame(frameIndex_);
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "BEGIN frame ", frameIndex_);
        }

        input_.BeginFrame(static_cast<float>(glfwGetTime()));
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "glfwPollEvents()");
        }
        glfwPollEvents();
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "Update()");
        }
        Update();
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "Render()");
        }
        Render();
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "glfwSwapBuffers()");
        }
        glfwSwapBuffers(window_);
        input_.EndFrame();

        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "END frame ", frameIndex_, " dt=", input_.deltaTime);
        }
    }

    DebugLog::Info("App", "Run() loop exit after ", frameIndex_, " frame(s)");
    return EXIT_SUCCESS;
}

bool App::Init()
{
    DebugLog::ScopedTrace trace("App", "Init");
    const auto startupBegin = std::chrono::steady_clock::now();
    DebugLog::Info("App", "Calling glfwInit()");
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        DebugLog::Error("App", "glfwInit() failed");
        return false;
    }
    DebugLog::Info("App", "GLFW initialized");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    DebugLog::Info("App", "Creating GLFW window ", framebufferWidth_, "x", framebufferHeight_);
    window_ = glfwCreateWindow(framebufferWidth_, framebufferHeight_, "Grafica BBB Base", nullptr, nullptr);
    if (window_ == nullptr)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        DebugLog::Error("App", "glfwCreateWindow() returned null");
        return false;
    }
    DebugLog::Info("App", "Window created");

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    DebugLog::Info("App", "OpenGL context current and swap interval set");

    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    glfwSetKeyCallback(window_, KeyCallback);
    glfwSetCursorPosCallback(window_, CursorPosCallback);
    glfwSetScrollCallback(window_, ScrollCallback);

    glewExperimental = GL_TRUE;
    DebugLog::Info("App", "Calling glewInit()");
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW\n";
        DebugLog::Error("App", "glewInit() failed");
        return false;
    }
    DebugLog::Info("App", "GLEW initialized");

    glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
    glViewport(0, 0, framebufferWidth_, framebufferHeight_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    try
    {
        const auto sceneInitBegin = std::chrono::steady_clock::now();
        DebugLog::Info("App", "Constructing BaseScene");
        scene_ = std::make_unique<BaseScene>(assetsRoot_);
        DebugLog::Info("App", "Initializing BaseScene");
        scene_->Init();
        const auto sceneInitEnd = std::chrono::steady_clock::now();
        DebugLog::Info("App", "BaseScene ready");

        DebugLog::Info("App", "Queueing async navmesh build");
        std::vector<SceneCollisionSource> collisionSources = scene_->GetStaticCollisionSources();
        std::vector<StaticRegionDesc> staticRegions;
        staticRegions.push_back(scene_->BuildFloorCollisionRegion());
        pendingNavigationBuild_ = std::make_unique<PendingNavigationBuild>();
        pendingNavigationBuild_->future = std::async(
            std::launch::async,
            [collisionSources = std::move(collisionSources), staticRegions = std::move(staticRegions)]() mutable -> std::unique_ptr<IWalkableWorld>
            {
                DebugLog::ScopedTrace navTrace("AsyncNav", "BuildWalkableWorld");
                std::vector<ImportedModelAsset> importedAssets;
                importedAssets.reserve(collisionSources.size());
                for (const SceneCollisionSource& source : collisionSources)
                {
                    DebugLog::Info("AsyncNav", "Importing ", source.sourcePath.string());
                    importedAssets.push_back(ImportModelAsset(source.sourcePath, source.transform));
                }

                auto walkableWorld = std::make_unique<RecastNavigationWorld>();
                WalkableBuildSettings settings;
                settings.agentHeight = 1.80f;
                settings.agentRadius = 0.35f;
                settings.agentMaxClimb = 0.30f;
                settings.agentMaxSlopeDegrees = 45.0f;
                settings.cellSize = 0.12f;
                settings.cellHeight = 0.08f;
                if (!walkableWorld->Build(importedAssets, staticRegions, settings))
                {
                    DebugLog::Error("AsyncNav", "Navmesh build failed");
                    return nullptr;
                }

                DebugLog::Info("AsyncNav", "Navmesh world ready");
                return walkableWorld;
            });

        DebugLog::Info("App", "Binding temporary walkable world state to player");
        player_.SetWalkableWorld(nullptr);
        DebugLog::Info("App", "Setting player spawn");
        player_.SetSpawn(scene_->GetSuggestedPlayerSpawnPosition(), scene_->GetSuggestedPlayerSpawnYawDegrees());
        scene_->SetPhysicsDebugEnabled(physicsDebugEnabled_);

        const auto startupEnd = std::chrono::steady_clock::now();
        const auto sceneInitMs = std::chrono::duration_cast<std::chrono::milliseconds>(sceneInitEnd - sceneInitBegin).count();
        const auto totalInitMs = std::chrono::duration_cast<std::chrono::milliseconds>(startupEnd - startupBegin).count();
        std::cout
            << "Startup: scene init " << sceneInitMs << " ms, navmesh queued "
            << (pendingNavigationBuild_ != nullptr ? 1 : 0)
            << " job(s), total init " << totalInitMs << " ms.\n";
        DebugLog::Info(
            "App",
            "Startup summary sceneInitMs=", sceneInitMs,
            " totalInitMs=", totalInitMs,
            " pendingNavigationBuild=", (pendingNavigationBuild_ != nullptr));
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        DebugLog::Error("App", "Exception during Init(): ", error.what());
        return false;
    }

    DebugLog::Info("App", "Capturing mouse");
    SetMouseCaptured(true);
    DebugLog::Info("App", "Seeding camera anchors");
    camera_.SetPlayerAnchor(player_.GetEyePosition());
    camera_.SetOrbitTarget(player_.GetOrbitTarget());
    camera_.SetOrbitBounds(scene_->GetSceneBoundsMin(), scene_->GetSceneBoundsMax());
    UpdateWindowTitle();
    DebugLog::Info("App", "Init() complete");
    return true;
}

void App::Shutdown()
{
    DebugLog::ScopedTrace trace("App", "Shutdown");
    pendingNavigationBuild_.reset();
    walkableWorld_.reset();
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
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "Begin update navPending=", (pendingNavigationBuild_ != nullptr));
    }

    if (pendingNavigationBuild_ != nullptr
        && pendingNavigationBuild_->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        try
        {
            if (traceCurrentFrame_)
            {
                DebugLog::Info("Update", "Navigation future ready, consuming world");
            }
            std::unique_ptr<IWalkableWorld> builtWorld = pendingNavigationBuild_->future.get();
            if (builtWorld != nullptr && builtWorld->IsReady())
            {
                walkableWorld_ = std::move(builtWorld);
                player_.SetWalkableWorld(walkableWorld_.get());
                DebugLog::Info("Update", "Walkable world activated");
            }
            else
            {
                DebugLog::Error("Update", "Walkable world build completed but returned null or not ready");
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "Navigation build failed: " << error.what() << '\n';
            DebugLog::Error("Update", "Navigation build failed: ", error.what());
        }
        pendingNavigationBuild_.reset();
    }

    if (input_.WasKeyPressed(GLFW_KEY_ESCAPE))
    {
        DebugLog::Info("Update", "ESC pressed, toggling mouse capture to ", !input_.mouseCaptured);
        SetMouseCaptured(!input_.mouseCaptured);
    }

    if (input_.WasKeyPressed(GLFW_KEY_TAB))
    {
        DebugLog::Info("Update", "TAB pressed, toggling camera mode");
        camera_.ToggleMode();
        UpdateWindowTitle();
    }

    if (input_.WasKeyPressed(GLFW_KEY_F3))
    {
        physicsDebugEnabled_ = !physicsDebugEnabled_;
        scene_->SetPhysicsDebugEnabled(physicsDebugEnabled_);
        DebugLog::Info("Update", "F3 pressed, physicsDebugEnabled=", physicsDebugEnabled_);
    }

    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "camera_.Update()");
    }
    camera_.Update(input_);
    if (input_.WasKeyPressed(GLFW_KEY_E))
    {
        const bool interacted = scene_->TryInteract(camera_.GetPosition(), camera_.GetForward(), player_.GetPosition());
        DebugLog::Info("Update", "E pressed, doorInteraction=", interacted);
    }
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "scene_->Update()");
    }
    scene_->Update(player_.GetSnapshot(), input_.lastFrameTime, input_.deltaTime);
    if (walkableWorld_ != nullptr)
    {
        walkableWorld_->SetDynamicBlockers(scene_->BuildWalkableBlockers());
    }
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "player_.Update()");
    }
    player_.Update(input_, camera_.GetMovementYawDegrees());
    camera_.SetPlayerAnchor(player_.GetEyePosition());
    camera_.SetOrbitTarget(player_.GetOrbitTarget());

    if (scene_ != nullptr)
    {
        PhysicsDebugFrame frame;
        if (physicsDebugEnabled_ && walkableWorld_ != nullptr)
        {
            if (traceCurrentFrame_)
            {
                DebugLog::Info("Update", "Building walkable debug frame");
            }
            frame = walkableWorld_->BuildDebugFrame();
            MergeDebugFrame(frame, player_.GetPhysicsDebugFrame());
        }
        scene_->SetPhysicsDebugFrame(std::move(frame));
    }

    if (traceCurrentFrame_)
    {
        DebugLog::Info(
            "Update",
            "End update playerPos=(",
            player_.GetPosition().x, ", ",
            player_.GetPosition().y, ", ",
            player_.GetPosition().z, ")");
    }
}

void App::Render()
{
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Render", "Begin render");
    }
    const float aspectRatio = framebufferHeight_ > 0
        ? static_cast<float>(framebufferWidth_) / static_cast<float>(framebufferHeight_)
        : (16.0f / 9.0f);

    const glm::mat4 projection = glm::perspective(
        glm::radians(camera_.GetFovDegrees()),
        aspectRatio,
        0.25f,
        100.0f);

    glClearColor(0.07f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    scene_->Render(camera_, projection);
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Render", "End render");
    }
}

void App::SetMouseCaptured(bool captured)
{
    input_.mouseCaptured = captured;
    input_.ResetMouseReference();
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    DebugLog::Info("App", "Mouse capture set to ", captured ? "true" : "false");
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
    if (action == GLFW_PRESS)
    {
        DebugLog::Info("Input", "Key press ", key);
    }

    if (key == GLFW_KEY_Q && action == GLFW_PRESS)
    {
        DebugLog::Info("Input", "Q pressed, requesting window close");
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
    DebugLog::Info("Input", "Scroll y=", yOffset);
}
