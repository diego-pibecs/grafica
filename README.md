# Kirby Vegetable Valley - Laboratorio P1

Demo 3D interactiva en C++/OpenGL moderno inspirada en Vegetable Valley de Kirby's Adventure. La rama `laboratorio` contiene una version P1: zona inicial tipo bosque/valle con portal interactivo, segunda zona suspendida con parkour de plataformas, cima final con Whispy Woods desde asset, camaras primera/tercera persona, Kirby visible, HUD, inventario simple, keyframes, texturas, iluminacion y navmesh.

## Requisitos

En macOS, instala las dependencias principales con Homebrew:

```bash
brew install glfw glew glm assimp
```

`SOIL2` y `Recast/Detour` estan vendorizados en `third_party/`.

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
- `E`: interactuar por raycast. Sirve para recoger estrellas, activar el portal e interactuar con Whispy.
- `K`: reproducir la animacion por keyframes de la manzana de Whispy.
- `Y`: alternar entre Whispy Woods FBX y fallback procedural.
- `Space`: salto.
- `Shift`: sprint.
- `F3`: debug de navmesh, bloqueadores y actor.
- `ESC` o `Q`: cerrar.

## Demo esperada

1. Abrir el programa.
2. Ver el HUD arriba a la izquierda con estrellas y vidas; las instrucciones iniciales aparecen en varias lineas al lado izquierdo.
3. Recorrer la zona 1: pradera/bosque con suelo, paredes, vegetacion densa, pollo decorativo, estrella escondida y camino libre hacia el portal.
   Los arboles, bushes, rocas y tronco grande funcionan como obstaculos; grass, flores y mushrooms no bloquean.
4. Recoger la estrella del portal con `E`; esta en `X=2`, `Z=-13`.
5. Activar el portal con `E`; al terminar la animacion se teletransporta a la zona 2.
6. Cambiar a tercera persona con `TAB` y ver a Kirby.
7. Avanzar por la zona 2: plataforma suspendida con vacio, salto libre entre plataformas moradas y una luz guia dinamica que marca el siguiente paso.
8. Si se cae al vacio, aparece "INTENTALO DE NUEVO", baja una vida y se regresa a zona 1; al perder todas las vidas aparece "GAME OVER" y se reinicia el flujo.
9. Ver el murcielago decorativo en zona 2, subir hasta la cima, que funciona como entrada a la zona 3, y ver Whispy Woods.
10. Tocar al pollo, murcielago o Whispy resta una vida y regresa a zona 1; el portal queda reutilizable si ya se habia recogido su estrella.
11. Apuntar a Whispy y usar `E`, o usar `K`, para ver la manzana animada por keyframes fuera del tronco.
12. Con tres estrellas, interactuar con Whispy para activar su derrota: rota, reduce escala y desaparece.
13. Cerrar sin errores.

## Funcionalidades P1

- Escena activa en `src/BaseScene.*`.
- Camara y movimiento en `src/CameraController.*` y `src/PlayerController.*`.
- Carga de modelos por Assimp en `src/Model.*`.
- Navegacion con Recast/Detour en zona 1 y sistema de superficies caminables propio en zona 2.
- Obstaculos asociados a props grandes: arboles, bushes, rocas y tronco caido aportan footprints/colliders ligados a su posicion visual.
- Tres zonas: entrada con portal, parkour suspendido con plataformas y cima final con Whispy.
- HUD persistente con estrellas y vidas.
- Instrucciones iniciales multilínea en el lado izquierdo; mensajes contextuales a la derecha; eventos importantes al centro.
- Inventario simple: estrellas, vidas, estrella del portal y estado del portal.
- Teletransporte claro desde el portal de zona 1 hacia la zona 2; el portal requiere recoger primero la estrella.
- Caida real en zona 2 mediante prueba vertical de soporte sobre plataformas.
- Respawn desde zona 2 hacia zona 1 si el jugador cae al vacio; con cero vidas se reinicia la partida.
- Dano por contacto con Whispy, pollo y murcielago, con cooldown para evitar multiples golpes por frame.
- En zona 2 se ocultan el skybox y los elementos de zona 1 para que el parkour se perciba como un espacio separado.
- Luces filtradas por zona: en zona 2/3 no se envia la luz del portal ni objetos brillantes de zona 1.
- Camara tercera persona con colision tipo boom contra paredes, suelo, plataformas y Whispy.
- Portal procedural jerarquico: nodo raiz, piezas hijas del aro, runas hijas y chispas como hijos de las runas.
- Animacion por keyframes: apertura del portal y caida de manzana desde Whispy, ajustada para caer delante del tronco.
- Animacion de derrota de Whispy: giro, reduccion de escala y ocultamiento al completarse.
- Whispy Woods FBX es el modo principal; el procedural queda como fallback.
- Sombras direccionales sobre modelos principales; las paredes limitadoras no reciben sombras.
- La zona 2 atenúa la luz direccional del sol y usa una luz guia dinamica que se mueve hacia la siguiente plataforma segun la superficie actual del jugador.
- Estrellas verticales con material amarillo autoiluminado, sin bolita de luz externa: estrella del portal, estrella escondida en zona 1 y estrella de la cima.
- Props tematicos importados: pollo en zona 1 y murcielago en zona 2. Assimp registra animaciones y bones cuando existen; por ahora se usa animacion procedural visible para Kirby, pollo y murcielago, sin skinning esqueletal en runtime.
- Parkour extendido en zona 2 con plataformas de distintos tamanos y alturas: `zone-two-step-f`, `zone-two-step-g`, `zone-two-final-approach` y `zone-two-summit`.

## Carpetas importantes

- `assets/models/kirby/`: modelo y textura de Kirby.
- `assets/models/whispy-woods/`: modelo FBX de Whispy Woods.
- `assets/models/nuevos/star.obj`: estrellas coleccionables y marcador final.
- `assets/models/nature/`: vegetacion decorativa de zona 1; los props grandes aportan obstaculos y los pequenos no bloquean.
- `assets/models/Black_Chick/`: pollo decorativo de zona 1.
- `assets/models/animated-halloween-bat/`: murcielago decorativo de zona 2.
- `assets/textures/grass/`, `assets/textures/dirt/`, `assets/textures/skybox/`: texturas del escenario.
- `src/navigation/`: navmesh Recast/Detour.
- `src/PlayerController.*`: movimiento, salto y soporte de plataformas en zona 2.
- `src/render/debug/DebugOverlayRenderer.*`: texto de HUD y debug.
