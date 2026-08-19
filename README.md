# Interactive Computer Graphics Portfolio

Computer graphics projects developed in **C++17 and OpenGL**, focused on
real-time 3D rendering, player interaction, navigation, animation, physics
and gameplay systems.

This repository contains two interactive 3D projects developed as part of
Computer Graphics and Human-Computer Interaction coursework.

## Featured Projects

### ⭐ Kirby's Adventure — 3D Platformer

**Branch:** `kirby-adventure`

A 3D interactive platforming experience inspired by *Kirby's Adventure* and
the Vegetable Valley level.

The project contains three connected gameplay areas, including a forest
environment, an animated portal, a suspended parkour section and a final
encounter with Whispy Woods.

#### Highlights

- Real-time 3D rendering with OpenGL
- Third-person player controller
- FPS and orbit camera systems
- FBX model loading and animations
- Collectible star system
- Player lives, respawn and Game Over states
- Enemy patrol and contact damage
- Animated portal transitions
- Parkour gameplay
- Final boss encounter
- Lighting, textures and shadows
- Navigation mesh using Recast/Detour
- Runtime debug visualization

➡️ View project:
https://github.com/diego-pibecs/computer-graphics-projects/tree/kirby-adventure


---

### 🧠 OCD Interactive Experience

**Branch:** `ocd-interactive-experience`

An educational and artistic interactive 3D experience related to
Obsessive-Compulsive Disorder (OCD).

The project explores a modular architecture where rendering, navigation,
physics and interaction systems are kept separated.

The environment contains an explorable house with interactive doors and a
navigation system capable of handling dynamic obstacles.

#### Highlights

- Interactive 3D environment
- OpenGL rendering pipeline
- Static navigation mesh generation
- Dynamic navigation obstacles
- Recast & Detour integration
- ReactPhysics3D integration
- Kinematic character controller
- Interactive hinged and sliding doors
- Assimp-based geometry importing
- Physics and navigation debug rendering
- FPS and orbit camera modes

➡️ View project:
https://github.com/diego-pibecs/computer-graphics-projects/tree/ocd-interactive-experience


## Tech Stack

| Area | Technologies |
|---|---|
| Language | C++17 |
| Graphics | OpenGL |
| Window / Input | GLFW |
| OpenGL Extensions | GLEW |
| Mathematics | GLM |
| 3D Asset Import | Assimp |
| Textures | SOIL2 |
| Navigation | Recast / Detour |
| Physics | ReactPhysics3D |
| Build System | CMake |

## Architecture

The projects are organized into independent systems for rendering,
navigation, physics and asset importing.

```text
src/
├── import/             # Geometry and model importing
├── navigation/         # NavMesh and navigation world
├── physics/            # Collision and character controllers
├── render/
│   └── debug/          # Debug visualization
├── BaseScene.*         # Scene composition and interactions
├── CameraController.*  # FPS / Orbit camera
├── Model.*             # 3D model handling
├── ShaderProgram.*     # OpenGL shaders
└── App.*               # Application lifecycle
