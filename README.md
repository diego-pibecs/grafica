# OCD Interactive 3D Experience

An interactive 3D environment developed in **C++17 and OpenGL**, focused on environmental interaction, physics, navigation, and character movement.

<img width="331" height="206" alt="image" src="https://github.com/user-attachments/assets/302b8bdb-eb2d-4465-a95d-302d68e4bf0c" />
<img width="331" height="207" alt="image" src="https://github.com/user-attachments/assets/caf25315-869a-430a-9019-dac09e6b2ea6" />


## Overview

The project presents an explorable indoor 3D environment with interactive elements and systems designed around movement, navigation, and environmental behavior.

Its technical focus is the integration of graphics, physics, dynamic navigation obstacles, and interactive objects within a modular C++ architecture.

## Highlights

* Real-time OpenGL rendering
* Explorable 3D environment
* Kinematic character controller
* Physics integration with ReactPhysics3D
* Static navigation mesh generation
* Dynamic navigation obstacles
* Recast/Detour integration
* Interactive doors
* Hinged and sliding door behavior
* FPS and orbit camera modes
* Assimp-based 3D asset importing
* Physics debug rendering
* Navigation debug visualization

## Interaction

<img width="331" height="208" alt="image" src="https://github.com/user-attachments/assets/25f9649d-da89-4316-ac34-afb2e6b04d79" />
<img width="331" height="208" alt="image" src="https://github.com/user-attachments/assets/a90fd95c-2cf8-4996-b6e3-88c21d2db868" />

The environment includes interactive elements whose state can affect player movement and navigation.

For example, doors and other dynamic objects can modify how the player moves through the environment while navigation and physics systems remain synchronized.

## Technical Focus

### Physics

ReactPhysics3D is used for collision and character-related physics.

### Navigation

Recast and Detour provide navigation mesh generation and runtime navigation functionality.

Dynamic obstacles allow parts of the environment to affect traversable areas.

### Interaction

Interactive objects include different door behaviors and environment elements that react to player input.

### Architecture

The project separates graphics, navigation, physics, importing, and debug rendering into dedicated systems.

```text
src/
├── import/
├── navigation/
├── physics/
├── render/
│   └── debug/
├── BaseScene.*
├── CameraController.*
├── Model.*
├── ShaderProgram.*
└── App.*
```

## Tech Stack

`C++17` · `OpenGL` · `GLFW` · `GLEW` · `GLM` · `Assimp` · `SOIL2` · `Recast/Detour` · `ReactPhysics3D` · `CMake`

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
git checkout ocd-interactive-experience
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run the generated executable from the build output directory.

## What I Worked On

This project gave me hands-on experience with:

* Real-time 3D rendering
* Physics integration
* Character controllers
* Navigation meshes
* Dynamic obstacles
* Interactive environment systems
* Collision handling
* Camera systems
* Debug visualization
* Modular C++ architecture
