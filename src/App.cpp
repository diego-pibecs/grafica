#include "App.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "DebugLog.h"
#include "import/ImportedGeometry.h"
#include "navigation/RecastNavigationWorld.h"
#include "render/debug/DebugOverlayRenderer.h"

#ifndef APP_ASSET_DIR
#define APP_ASSET_DIR "assets"
#endif

namespace
{
constexpr double kTargetFrameMs = 1000.0 / 60.0;
constexpr double kLimiterSpinWaitMs = 0.9;

bool ShouldTraceFrame(std::uint64_t frameIndex)
{
    return frameIndex <= 12u || (frameIndex % 600u) == 0u;
}

double MillisecondsSince(const std::chrono::steady_clock::time_point& begin)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
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

    std::uint64_t perfWindowFrames = 0;
    double perfWindowSeconds = 0.0;
    double perfWorstFrameSeconds = 0.0;
    double perfPollMs = 0.0;
    double perfUpdateMs = 0.0;
    double perfRenderMs = 0.0;
    double perfSwapMs = 0.0;
    double perfLimiterSleepMs = 0.0;
    double perfFrameWallMs = 0.0;
    double perfWorstPollMs = 0.0;
    double perfWorstUpdateMs = 0.0;
    double perfWorstRenderMs = 0.0;
    double perfWorstSwapMs = 0.0;
    double perfWorstLimiterSleepMs = 0.0;
    double perfWorstFrameWallMs = 0.0;

    while (!glfwWindowShouldClose(window_))
    {
        const auto frameBegin = std::chrono::steady_clock::now();
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
        const auto pollBegin = std::chrono::steady_clock::now();
        glfwPollEvents();
        const double pollMs = MillisecondsSince(pollBegin);
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "Update()");
        }
        const auto updateBegin = std::chrono::steady_clock::now();
        Update();
        const double updateMs = MillisecondsSince(updateBegin);
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "Render()");
        }
        const auto renderBegin = std::chrono::steady_clock::now();
        Render();
        const double renderMs = MillisecondsSince(renderBegin);
        if (traceCurrentFrame_)
        {
            DebugLog::Info("Frame", "glfwSwapBuffers()");
        }
        const auto swapBegin = std::chrono::steady_clock::now();
        glfwSwapBuffers(window_);
        const double swapMs = MillisecondsSince(swapBegin);
        input_.EndFrame();

        double limiterSleepMs = 0.0;
        const double activeFrameMs = MillisecondsSince(frameBegin);
        if (activeFrameMs < kTargetFrameMs)
        {
            const auto sleepBegin = std::chrono::steady_clock::now();
            const double coarseSleepMs = kTargetFrameMs - activeFrameMs - kLimiterSpinWaitMs;
            if (coarseSleepMs > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(coarseSleepMs));
            }
            while (MillisecondsSince(frameBegin) < kTargetFrameMs)
            {
                std::this_thread::yield();
            }
            limiterSleepMs = MillisecondsSince(sleepBegin);
        }
        const double frameWallMs = MillisecondsSince(frameBegin);
        if (input_.deltaTime > 0.0001f)
        {
            const double instantaneousFps = 1.0 / static_cast<double>(input_.deltaTime);
            displayedFps_ = displayedFps_ <= 0.0
                ? instantaneousFps
                : (displayedFps_ * 0.90) + (instantaneousFps * 0.10);
        }

        perfWindowFrames += 1u;
        perfWindowSeconds += static_cast<double>(input_.deltaTime);
        perfWorstFrameSeconds = std::max(perfWorstFrameSeconds, static_cast<double>(input_.deltaTime));
        perfPollMs += pollMs;
        perfUpdateMs += updateMs;
        perfRenderMs += renderMs;
        perfSwapMs += swapMs;
        perfLimiterSleepMs += limiterSleepMs;
        perfFrameWallMs += frameWallMs;
        perfWorstPollMs = std::max(perfWorstPollMs, pollMs);
        perfWorstUpdateMs = std::max(perfWorstUpdateMs, updateMs);
        perfWorstRenderMs = std::max(perfWorstRenderMs, renderMs);
        perfWorstSwapMs = std::max(perfWorstSwapMs, swapMs);
        perfWorstLimiterSleepMs = std::max(perfWorstLimiterSleepMs, limiterSleepMs);
        perfWorstFrameWallMs = std::max(perfWorstFrameWallMs, frameWallMs);

        if (perfWindowFrames >= 120u || input_.deltaTime >= 0.03f)
        {
            const double averageFrameSeconds = perfWindowFrames > 0u
                ? perfWindowSeconds / static_cast<double>(perfWindowFrames)
                : 0.0;
            const double averageFps = averageFrameSeconds > 0.0 ? 1.0 / averageFrameSeconds : 0.0;
            const double worstFps = perfWorstFrameSeconds > 0.0 ? 1.0 / perfWorstFrameSeconds : 0.0;
            DebugLog::Info(
                "Perf",
                "Frame avgFps=", averageFps,
                " avgMs=", averageFrameSeconds * 1000.0,
                " worstMs=", perfWorstFrameSeconds * 1000.0,
                " worstFps=", worstFps,
                " wallAvgMs=", perfFrameWallMs / static_cast<double>(perfWindowFrames),
                " wallMaxMs=", perfWorstFrameWallMs,
                " pollAvgMs=", perfPollMs / static_cast<double>(perfWindowFrames),
                " pollMaxMs=", perfWorstPollMs,
                " updateAvgMs=", perfUpdateMs / static_cast<double>(perfWindowFrames),
                " updateMaxMs=", perfWorstUpdateMs,
                " renderAvgMs=", perfRenderMs / static_cast<double>(perfWindowFrames),
                " renderMaxMs=", perfWorstRenderMs,
                " swapAvgMs=", perfSwapMs / static_cast<double>(perfWindowFrames),
                " swapMaxMs=", perfWorstSwapMs,
                " limiterSleepAvgMs=", perfLimiterSleepMs / static_cast<double>(perfWindowFrames),
                " limiterSleepMaxMs=", perfWorstLimiterSleepMs,
                " camera=(", camera_.GetPosition().x, ", ", camera_.GetPosition().y, ", ", camera_.GetPosition().z, ")",
                " mouseCaptured=", input_.mouseCaptured,
                " navPending=", (pendingNavigationBuild_ != nullptr));

            perfWindowFrames = 0u;
            perfWindowSeconds = 0.0;
            perfWorstFrameSeconds = 0.0;
            perfPollMs = 0.0;
            perfUpdateMs = 0.0;
            perfRenderMs = 0.0;
            perfSwapMs = 0.0;
            perfLimiterSleepMs = 0.0;
            perfFrameWallMs = 0.0;
            perfWorstPollMs = 0.0;
            perfWorstUpdateMs = 0.0;
            perfWorstRenderMs = 0.0;
            perfWorstSwapMs = 0.0;
            perfWorstLimiterSleepMs = 0.0;
            perfWorstFrameWallMs = 0.0;
        }

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    DebugLog::Info("App", "Creating GLFW window ", framebufferWidth_, "x", framebufferHeight_);
    window_ = glfwCreateWindow(framebufferWidth_, framebufferHeight_, "Kirby Vegetable Valley - Laboratorio P1", nullptr, nullptr);
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
    DebugLog::Info("App", "Frame limiter targetMs=", kTargetFrameMs, " targetFps=60");

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

    const auto glString = [](GLenum name) -> const char*
    {
        const GLubyte* value = glGetString(name);
        return value != nullptr ? reinterpret_cast<const char*>(value) : "unknown";
    };
    GLint maxTextureUnits = 0;
    GLint maxCubeMapSize = 0;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapSize);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    DebugLog::Info(
        "GL",
        "vendor=", glString(GL_VENDOR),
        " renderer=", glString(GL_RENDERER),
        " version=", glString(GL_VERSION),
        " glsl=", glString(GL_SHADING_LANGUAGE_VERSION),
        " maxTextureUnits=", maxTextureUnits,
        " maxTextureSize=", maxTextureSize,
        " maxCubeMapSize=", maxCubeMapSize);

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
        debugOverlayRenderer_ = std::make_unique<DebugOverlayRenderer>(assetsRoot_);
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
                settings.agentRadius = 0.22f;
                settings.agentMaxClimb = 0.40f;
                settings.agentMaxSlopeDegrees = 45.0f;
                settings.cellSize = 0.12f;
                settings.cellHeight = 0.06f;
                DebugLog::Info(
                    "AsyncNav",
                    "Settings agentHeight=", settings.agentHeight,
                    " agentRadius=", settings.agentRadius,
                    " agentMaxClimb=", settings.agentMaxClimb,
                    " maxSlope=", settings.agentMaxSlopeDegrees,
                    " cellSize=", settings.cellSize,
                    " cellHeight=", settings.cellHeight);
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
    camera_.SetCollisionColliders(scene_->BuildCameraSolidColliders());
    player_.SetZoneTwoWalkableSurfaces(scene_->BuildZoneTwoWalkableSurfaces());
    UpdateWindowTitle();
    DebugLog::Info("App", "Init() complete");
    return true;
}

void App::Shutdown()
{
    DebugLog::ScopedTrace trace("App", "Shutdown");
    pendingNavigationBuild_.reset();
    walkableWorld_.reset();
    debugOverlayRenderer_.reset();
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

    if (input_.WasKeyPressed(GLFW_KEY_Q))
    {
        DebugLog::Info("Update", "Q pressed, requesting window close");
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }

    if (input_.WasKeyPressed(GLFW_KEY_ESCAPE))
    {
        paused_ = !paused_;
        SetMouseCaptured(!paused_);
        UpdateWindowTitle();
        DebugLog::Info("Update", "ESC pressed, paused=", paused_);
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
        UpdateWindowTitle();
        DebugLog::Info(
            "Update",
            "F3 pressed, physicsDebugEnabled=", physicsDebugEnabled_);
    }
    if (input_.WasKeyPressed(GLFW_KEY_F6))
    {
        scene_->CycleKirbyDebugMode();
        DebugLog::Info("Update", "F6 pressed, Kirby debug mode cycled");
    }

    if (paused_)
    {
        if (scene_ != nullptr)
        {
            scene_->SetKirbyCameraFacing(camera_.GetForward(), camera_.GetMode());
        }
        return;
    }

    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "camera_.Update()");
    }
    camera_.Update(input_);
    scene_->SetKirbyCameraFacing(camera_.GetForward(), camera_.GetMode());
    if (input_.WasKeyPressed(GLFW_KEY_E))
    {
        const bool interacted = scene_->TryInteract(camera_.GetPosition(), camera_.GetForward(), player_.GetPosition());
        DebugLog::Info("Update", "E pressed, sceneInteraction=", interacted);
    }
    if (input_.WasKeyPressed(GLFW_KEY_K))
    {
        scene_->TriggerKeyframeAnimation();
        DebugLog::Info("Update", "K pressed, scene keyframe animation triggered");
    }
    if (input_.WasKeyPressed(GLFW_KEY_Y))
    {
        scene_->ToggleWhispyVariant();
        DebugLog::Info("Update", "Y pressed, Whispy variant toggle requested");
    }
    if (traceCurrentFrame_)
    {
        DebugLog::Info("Update", "scene_->Update()");
    }
    scene_->Update(player_.GetSnapshot(), input_.lastFrameTime, input_.deltaTime);
    glm::vec3 teleportPosition;
    float teleportYawDegrees = 0.0f;
    if (scene_->ConsumePendingTeleport(teleportPosition, teleportYawDegrees))
    {
        player_.SetSpawn(teleportPosition, teleportYawDegrees);
        camera_.SetPlayerAnchor(player_.GetEyePosition());
        camera_.SetOrbitTarget(player_.GetOrbitTarget());
    }
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
    camera_.SetCollisionColliders(scene_->BuildCameraSolidColliders());
    player_.SetZoneTwoWalkableSurfaces(scene_->BuildZoneTwoWalkableSurfaces());

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
    if (framebufferWidth_ <= 0 || framebufferHeight_ <= 0)
    {
        return;
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
    if (debugOverlayRenderer_ != nullptr)
    {
        const float damageFlashAlpha = scene_->GetDamageFlashAlpha();
        if (damageFlashAlpha > 0.001f)
        {
            debugOverlayRenderer_->RenderFullscreenTint(
                framebufferWidth_,
                framebufferHeight_,
                glm::vec3(1.0f, 0.02f, 0.02f),
                damageFlashAlpha);
        }
        debugOverlayRenderer_->RenderAt(
            scene_->BuildHudLines(),
            framebufferWidth_,
            framebufferHeight_,
            glm::vec2(18.0f, 18.0f),
            3.0f,
            glm::vec3(1.0f, 0.94f, 0.55f),
            0.58f);
        debugOverlayRenderer_->RenderAt(
            scene_->BuildInstructionLines(),
            framebufferWidth_,
            framebufferHeight_,
            glm::vec2(22.0f, static_cast<float>(framebufferHeight_) * 0.58f),
            2.55f,
            glm::vec3(0.92f, 0.97f, 1.0f),
            0.52f);
        debugOverlayRenderer_->RenderAt(
            scene_->BuildContextMessageLines(),
            framebufferWidth_,
            framebufferHeight_,
            glm::vec2(static_cast<float>(framebufferWidth_) * 0.58f, static_cast<float>(framebufferHeight_) * 0.46f),
            2.75f,
            glm::vec3(1.0f, 0.88f, 0.34f),
            0.56f);
        std::vector<std::string> centerLines = paused_
            ? std::vector<std::string> { "PAUSA" }
            : scene_->BuildCenterMessageLines();
        debugOverlayRenderer_->RenderAt(
            centerLines,
            framebufferWidth_,
            framebufferHeight_,
            glm::vec2(0.0f, static_cast<float>(framebufferHeight_) * 0.42f),
            3.7f,
            glm::vec3(1.0f, 0.98f, 0.80f),
            0.64f,
            true);
        if (physicsDebugEnabled_)
        {
        std::vector<std::string> overlayLines;
        const PlayerSnapshot& playerSnapshot = player_.GetSnapshot();
        const glm::vec3 cameraPosition = camera_.GetPosition();
        const char* cameraMode = camera_.GetMode() == CameraMode::Fps ? "FPS" : "THIRD";

        auto formatVec3 = [](const char* label, const glm::vec3& value)
        {
            std::ostringstream stream;
            stream << label
                   << " X " << std::fixed << std::setprecision(2) << value.x
                   << " Y " << value.y
                   << " Z " << value.z;
            return stream.str();
        };

        overlayLines.push_back("");
        std::ostringstream fpsStream;
        fpsStream << "FPS " << std::fixed << std::setprecision(1) << displayedFps_;
        overlayLines.push_back(fpsStream.str());
        overlayLines.push_back(formatVec3("XYZ", playerSnapshot.position));
        overlayLines.push_back(formatVec3("CAM", cameraPosition));
        overlayLines.push_back(std::string("CAMERA ") + cameraMode);
        std::vector<std::string> playerDebugLines = player_.BuildDebugLines();
        overlayLines.insert(overlayLines.end(), playerDebugLines.begin(), playerDebugLines.end());
        debugOverlayRenderer_->RenderAt(
            overlayLines,
            framebufferWidth_,
            framebufferHeight_,
            glm::vec2(18.0f, 92.0f),
            2.45f,
            glm::vec3(0.74f, 1.0f, 0.74f),
            0.50f);
        }
    }
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
    glfwSetWindowTitle(window_, paused_
        ? "Kirby Vegetable Valley - Laboratorio P1 [PAUSA]"
        : "Kirby Vegetable Valley - Laboratorio P1");
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
    framebufferWidth_ = std::max(0, width);
    framebufferHeight_ = std::max(0, height);
    glViewport(0, 0, std::max(1, width), std::max(1, height));
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
