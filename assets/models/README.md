Libreria de assets importados desde `../gráfica/assets/modelos`, ahora consolidada directamente en `assets/models/`.

Se conservaron solo archivos utiles para runtime:

- modelos: `.fbx`, `.obj`, `.glb`, `.gltf`
- materiales y buffers: `.mtl`, `.bin`
- texturas: `.png`, `.jpg`, `.jpeg`, `.tga`

La estructura interna de cada asset se preserva para que Assimp pueda resolver rutas relativas como `source/` y `textures/`.

Las carpetas `red_dog/` y `mi_gato/` siguen reservadas para la base jugable y el showroom las ignora.
