#pragma once

#include <array>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

struct InputState
{
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    std::array<bool, GLFW_KEY_LAST + 1> previousKeys{};
    bool mouseCaptured = true;
    bool firstMouseSample = true;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    double mouseDeltaX = 0.0;
    double mouseDeltaY = 0.0;
    double scrollDeltaY = 0.0;
    float deltaTime = 0.0f;
    float lastFrameTime = 0.0f;

    void BeginFrame(float currentFrameTime)
    {
        deltaTime = (lastFrameTime == 0.0f) ? 0.0f : currentFrameTime - lastFrameTime;
        if (deltaTime < 0.0f)
        {
            deltaTime = 0.0f;
        }

        lastFrameTime = currentFrameTime;
        mouseDeltaX = 0.0;
        mouseDeltaY = 0.0;
        scrollDeltaY = 0.0;
    }

    void EndFrame()
    {
        previousKeys = keys;
    }

    void SetKeyState(int key, int action)
    {
        if (key < 0 || key > GLFW_KEY_LAST)
        {
            return;
        }

        if (action == GLFW_PRESS)
        {
            keys[static_cast<std::size_t>(key)] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            keys[static_cast<std::size_t>(key)] = false;
        }
    }

    bool IsKeyDown(int key) const
    {
        return key >= 0 && key <= GLFW_KEY_LAST && keys[static_cast<std::size_t>(key)];
    }

    bool WasKeyPressed(int key) const
    {
        return key >= 0 && key <= GLFW_KEY_LAST &&
               keys[static_cast<std::size_t>(key)] &&
               !previousKeys[static_cast<std::size_t>(key)];
    }

    void ResetMouseReference()
    {
        firstMouseSample = true;
        mouseDeltaX = 0.0;
        mouseDeltaY = 0.0;
    }

    void RegisterMousePosition(double xPos, double yPos)
    {
        if (firstMouseSample)
        {
            lastMouseX = xPos;
            lastMouseY = yPos;
            firstMouseSample = false;
            return;
        }

        mouseDeltaX += xPos - lastMouseX;
        mouseDeltaY += lastMouseY - yPos;
        lastMouseX = xPos;
        lastMouseY = yPos;
    }

    void RegisterScroll(double yOffset)
    {
        scrollDeltaY += yOffset;
    }
};
