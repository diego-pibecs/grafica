# Kirby Vegetable Valley - Laboratorio P1

Demo 3D interactiva en C++/OpenGL moderno inspirada en Vegetable Valley de Kirby's Adventure. La rama `laboratorio` contiene una version P1 base: zona inicial corta con suelo y paredes, portal interactivo, segunda zona suspendida con parkour de crates, cima final con Whispy Woods desde asset, camaras primera/tercera persona, Kirby visible, keyframes, texturas, iluminacion y navmesh.

## Requisitos

En macOS, instala las dependencias principales con Homebrew:

```bash
brew install glfw glew glm assimp
```

`SOIL2`, `Recast/Detour` y `ReactPhysics3D` estan vendorizados en `third_party/`.

## Compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build --target grafica_bbb_base -j 8
./build/bin/grafica_bbb_base
```

## Controles

- `WASD`: mover a Kirby por el escenario.
- Mouse: mirar alrededor.
- Flechas: mirar alrededor sin mouse.
- `TAB`: cambiar entre primera persona y tercera persona.
- `E`: interactuar por raycast. Apunta a la estrella dummy del portal para abrirlo y viajar a la zona 2; apunta a Whispy para activar la manzana/luz final.
- `K`: reproducir la animacion por keyframes de la manzana de Whispy.
- `Y`: alternar entre Whispy Woods FBX y fallback procedural.
- `Space`: salto.
- `Shift`: sprint.
- `F3`: debug de navmesh, bloqueadores y actor.
- `ESC` o `Q`: cerrar.

## Demo esperada

1. Abrir el programa.
2. Recorrer la zona 1: pradera inicial con suelo, paredes y camino hacia el portal.
3. Apuntar a la estrella dummy y usar `E` para abrir el portal con keyframes; al terminar la animacion se teletransporta a la zona 2.
4. Cambiar a tercera persona con `TAB` y ver a Kirby.
5. Avanzar por la zona 2: plataforma suspendida con vacio, crates/props de parkour, salto libre entre plataformas y luces puntuales que guian el camino.
6. Subir hasta la cima, que funciona como entrada a la zona 3, y ver Whispy Woods.
7. Apuntar a Whispy y usar `E`, o usar `K`, para ver la manzana animada por keyframes.
8. Cerrar sin errores.

## Funcionalidades P1

- Escena activa en `src/BaseScene.*`.
- Camara y movimiento en `src/CameraController.*` y `src/PlayerController.*`.
- Carga de modelos por Assimp en `src/Model.*`.
- Navegacion restringida por Recast/Detour y controlador de personaje.
- Tres zonas: entrada con portal, parkour suspendido con crates y cima final con Whispy.
- Teletransporte claro desde el portal de zona 1 hacia la zona 2.
- Reinicio desde zona 2 hacia zona 1 si el jugador queda fuera del volumen seguro del parkour o cae bajo el umbral de vacio.
- En zona 2 se ocultan el skybox y los elementos de zona 1 para que el parkour se perciba como un espacio separado.
- Portal procedural jerarquico: nodo raiz, piezas hijas del aro, runas hijas y chispas como hijos de las runas.
- Animacion por keyframes: apertura del portal y caida de manzana desde Whispy.
- Whispy Woods FBX es el modo principal; el procedural queda como fallback.
- Sombras direccionales sobre modelos principales; las paredes limitadoras no reciben sombras.
- La zona 2 atenúa la luz direccional del sol y depende principalmente de luces puntuales pequenas para marcar la ruta.

## Carpetas importantes

- `assets/models/kirby/`: modelo y textura de Kirby.
- `assets/models/whispy-woods/`: modelo FBX de Whispy Woods.
- `assets/models/nuevos/crate/` y `assets/models/nuevos/star.obj`: props de zona media y portal.
- `assets/textures/grass/`, `assets/textures/dirt/`, `assets/textures/skybox/`: texturas del escenario.
- `src/navigation/`: navmesh Recast/Detour.
- `src/physics/`: controlador de personaje.
