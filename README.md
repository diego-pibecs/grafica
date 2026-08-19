# Kirby's Adventure — 3D Platformer

A third-person 3D platforming experience developed in **C++17 and OpenGL**.

The project focuses on gameplay programming, character control, animation, navigation, interactive environments, and real-time rendering.

<img width="1710" height="1107" alt="Screenshot 2026-08-19 at 14 32 02" src="https://github.com/user-attachments/assets/d8e80071-6704-43ff-a63d-0ed4bae376d4" />

## Overview

The experience is divided into multiple connected gameplay areas and includes platforming, enemies, collectibles, scene transitions, and a final boss encounter.

The goal of the project was to combine core computer graphics concepts with gameplay-oriented systems inside a complete interactive 3D environment.

## Highlights

* Third-person character controller
* Orbit and FPS camera modes
* Multiple connected gameplay areas
* Collectible system
* Player lives and respawn states
* Enemy patrol behavior
* Contact damage
* Animated scene transitions
* Parkour sections
* Boss encounter
* Character and model animations
* Lighting, textures, and shadows
* Navigation mesh integration with Recast/Detour
* Runtime debug visualization

## Gameplay

<img width="1710" height="1107" alt="Screenshot 2026-08-19 at 14 29 10" src="https://github.com/user-attachments/assets/f715dc14-fdb2-410d-a2a0-148bf18344d8" />

The project includes:

* Exploration and platforming
* Collectible stars
* Enemy encounters
* Environmental obstacles
* Portal transitions
* Vertical parkour sections
* Final encounter with Whispy Woods

## Technical Focus

### Graphics

The rendering layer uses OpenGL together with GLFW, GLEW, GLM, Assimp, and SOIL2.

### Navigation

Recast and Detour are used for navigation mesh generation and navigation-related systems.

### Gameplay Systems

Gameplay logic includes player states, movement, damage, respawning, collectibles, enemy behavior, transitions, and encounter logic.

### Project Structure

The codebase separates responsibilities across systems such as:

```text
src/
├── import/
├── navigation/
├── render/
├── BaseScene.*
├── CameraController.*
├── Model.*
├── ShaderProgram.*
└── App.*
```

## Tech Stack

`C++17` · `OpenGL` · `GLFW` · `GLEW` · `GLM` · `Assimp` · `SOIL2` · `Recast/Detour` · `CMake`

## Build

### Requirements

On macOS:

```bash
brew install cmake glfw glew glm assimp
```

Clone the repository and switch to this branch:

```bash
git clone https://github.com/diego-pibecs/computer-graphics-projects.git
cd computer-graphics-projects
git checkout kirby-adventure
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run the generated executable from the build output directory.

## What I Worked On

This project gave me hands-on experience with:

* Real-time graphics programming
* Gameplay architecture
* Camera systems
* Character movement
* Animation integration
* Scene management
* Collision handling
* Navigation meshes
* Enemy behavior
* Interactive 3D environments
