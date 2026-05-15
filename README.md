# Kirby's Adventure - Proyecto Final

Proyecto final de Laboratorio de Computacion Grafica e Interaccion Humano-Computadora.

Es una escena 3D interactiva inspirada en Vegetable Valley de Kirby's Adventure. El recorrido incluye una zona inicial tipo bosque, un portal, una seccion de parkour suspendido, enemigos, estrellas coleccionables, HUD de vidas y una zona final con Whispy Woods.

El desarrollo y las pruebas se hicieron en macOS, por lo que esa es la plataforma recomendada para compilarlo y ejecutarlo.

## Requisitos

En macOS, instala las dependencias principales con Homebrew:

```bash
brew install glfw glew glm assimp cmake
```

El proyecto ya incluye algunas dependencias dentro de `third_party/`, como SOIL2 y Recast/Detour.

## Compilacion

Desde la carpeta del proyecto:

```bash
cmake -S . -B build
cmake --build build --target grafica_bbb_base -j 8
```

## Ejecucion

```bash
./build/bin/grafica_bbb_base
```

Si el programa no encuentra algun asset, revisa que se este ejecutando desde la raiz del repositorio.

## Controles

- `WASD`: mover a Kirby.
- Mouse: controlar la vista.
- Flechas: mover la vista sin mouse.
- `TAB`: cambiar entre camara en primera persona y camara orbital.
- `Space`: saltar.
- `Shift`: correr.
- `E`: interactuar, recoger estrellas, activar el portal o atacar a Whispy.
- `K`: activar la animacion de la manzana.
- `ESC`: pausar o reanudar.
- `Q`: cerrar el programa.
- `F3`: mostrar informacion de debug.
- `F6`: cambiar modos de diagnostico de Kirby.

## Que incluye

- Tres zonas conectadas: entrada, parkour suspendido y cima final.
- Kirby visible con modelo FBX y animaciones de idle, caminar y correr.
- Portal animado que transporta al jugador a la zona de parkour.
- Estrellas coleccionables y mensajes en pantalla.
- Sistema de vidas, respawn y Game Over.
- Camara en primera persona y camara orbital.
- Enemigos patrullando con dano por contacto.
- Murcielagos animados en la zona de parkour.
- Whispy Woods como jefe final, con animacion de derrota.
- Sombras, texturas, modelos importados y luces de apoyo.

## Notas

El proyecto fue preparado y probado principalmente en macOS. Puede compilar en otros sistemas si las dependencias estan disponibles, pero no fue el entorno principal de desarrollo.

No se usa ReactPhysics3D en la implementacion actual.
