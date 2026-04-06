# Grafica BBB Base

Plantilla base en OpenGL para macOS pensada como punto de partida común para los dos proyectos del curso. La app arranca con una escena mínima reutilizable: suelo, iluminación puntual, el perro como personaje controlable, el gato como modelo fijo al centro, cámara híbrida (`FPS` + `Orbit`), animación procedural básica y colisión simple por entidades.

## Requisitos

Instala dependencias con Homebrew:

```bash
brew install glfw glew glm assimp
```

## Compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build
./build/bin/grafica_bbb_base
```

## Controles base

- `WASD` o flechas: mover al perro sobre el plano
- Mouse: rotación de cámara
- `Space`: salto
- `Shift`: sprint
- `Tab`: alternar entre `FPS` y `Orbit`
- `Esc`: liberar o capturar cursor
- Rueda del mouse en `Orbit`: zoom
- `Q`: cerrar la ventana

## Comportamiento base

- `RedDog.obj` se usa como personaje controlable.
- `miGato.obj` queda estático en el centro como prop bloqueante.
- El personaje tiene `Idle`, `Walk`, `Run` y `Airborne` por animación procedural de transformación global.
- Las colisiones usan hitboxes simples alineados a ejes para bloquear el movimiento del jugador frente a props.

## Estructura

- `src/`: aplicación base, input, cámara, escena y carga de modelos
- `assets/`: shaders, modelos y texturas reutilizables
- `third_party/SOIL2/`: carga interna de texturas

## Notas

- La escena usa un sistema de entidades configurable para que futuros modelos puedan activar o desactivar movimiento procedural y colisión.
- La base está enfocada primero en macOS, pero el proyecto queda organizado para volver a abrir compatibilidad con Windows más adelante desde `CMake`.
