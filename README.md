# Grafica BBB Base

Plantilla base en OpenGL para macOS con escena visual desacoplada del sistema de navegación y movimiento. El render queda en `OpenGL`; la importación geométrica, la construcción de navmesh, el controlador del actor y el debug viven en módulos separados.

## Requisitos

Instala dependencias con Homebrew:

```bash
brew install glfw glew glm assimp
```

`Recast Navigation`, `Detour` y `ReactPhysics3D` ya vienen vendorizados en `third_party/`, así que no hace falta instalarlos aparte.

## Compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build
./build/bin/grafica_bbb_base
```

## Controles

- `WASD` o flechas: mover actor
- Mouse: rotación de cámara
- `Space`: salto
- `Shift`: sprint
- `E`: interactuar con la puerta apuntada por la cámara
- `Tab`: alternar entre `FPS` y `Orbit`
- `Esc`: liberar o capturar cursor
- Rueda del mouse en `Orbit`: zoom
- `F3`: debug de navmesh, bloqueadores dinámicos y actor
- `Q`: cerrar la ventana

## Arquitectura actual

- `src/import/`: importación desacoplada de Assimp a `ImportedModelAsset`
- `src/navigation/`: mundo caminable basado en Recast/Detour, navmesh y bloqueadores dinámicos
- `src/physics/controller/`: controlador cinemático del actor montado sobre el navmesh
- `src/render/debug/`: renderer OpenGL para líneas, puntos y triángulos de debug
- `src/BaseScene.*`: escena visual, placements, puertas interactivas y fuentes de navegación

## Notas

- La geometría de navegación no sale del renderer ni de buffers OpenGL.
- La casa genera una navmesh estática; las puertas no se meten en esa navmesh.
- Las puertas son obstáculos dinámicos: cerradas bloquean, abiertas liberan paso.
- `garage_door.fbx` y `kitchen_door.fbx` abren con bisagra; `living-room_door.fbx` es corrediza.
- Si una puerta abre hacia el lado incorrecto, ajusta `openAngleDegrees`, `localSlideDirection` o el lado de bisagra en `BaseScene`.
