cmake_minimum_required(VERSION 3.24)
project(grafica_bbb_base LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(assimp REQUIRED)

if(TARGET glfw)
  set(GRAFICA_BBB_GLFW_TARGET glfw)
elseif(TARGET glfw3::glfw)
  set(GRAFICA_BBB_GLFW_TARGET glfw3::glfw)
else()
  message(FATAL_ERROR "Unable to resolve a GLFW target")
endif()

if(TARGET GLEW::GLEW)
  set(GRAFICA_BBB_GLEW_TARGET GLEW::GLEW)
elseif(TARGET GLEW::glew)
  set(GRAFICA_BBB_GLEW_TARGET GLEW::glew)
else()
  message(FATAL_ERROR "Unable to resolve a GLEW target")
endif()

if(TARGET glm::glm)
  set(GRAFICA_BBB_GLM_TARGET glm::glm)
elseif(TARGET glm)
  set(GRAFICA_BBB_GLM_TARGET glm)
else()
  message(FATAL_ERROR "Unable to resolve a GLM target")
endif()

if(TARGET assimp::assimp)
  set(GRAFICA_BBB_ASSIMP_TARGET assimp::assimp)
elseif(TARGET assimp)
  set(GRAFICA_BBB_ASSIMP_TARGET assimp)
else()
  message(FATAL_ERROR "Unable to resolve an Assimp target")
endif()

add_library(soil2 STATIC
  third_party/SOIL2/SOIL2.c
  third_party/SOIL2/image_helper.c
  third_party/SOIL2/image_DXT.c
  third_party/SOIL2/etc1_utils.c
)
target_include_directories(soil2 PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
if(APPLE)
  target_compile_definitions(soil2 PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(soil2 PUBLIC "-framework CoreFoundation")
endif()

add_library(recast STATIC
  third_party/recastnavigation/Recast/Source/Recast.cpp
  third_party/recastnavigation/Recast/Source/RecastAlloc.cpp
  third_party/recastnavigation/Recast/Source/RecastArea.cpp
  third_party/recastnavigation/Recast/Source/RecastAssert.cpp
  third_party/recastnavigation/Recast/Source/RecastContour.cpp
  third_party/recastnavigation/Recast/Source/RecastFilter.cpp
  third_party/recastnavigation/Recast/Source/RecastLayers.cpp
  third_party/recastnavigation/Recast/Source/RecastMesh.cpp
  third_party/recastnavigation/Recast/Source/RecastMeshDetail.cpp
  third_party/recastnavigation/Recast/Source/RecastRasterization.cpp
  third_party/recastnavigation/Recast/Source/RecastRegion.cpp
)
target_include_directories(recast PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/recastnavigation/Recast/Include"
)

add_library(detour STATIC
  third_party/recastnavigation/Detour/Source/DetourAlloc.cpp
  third_party/recastnavigation/Detour/Source/DetourAssert.cpp
  third_party/recastnavigation/Detour/Source/DetourCommon.cpp
  third_party/recastnavigation/Detour/Source/DetourNavMesh.cpp
  third_party/recastnavigation/Detour/Source/DetourNavMeshBuilder.cpp
  third_party/recastnavigation/Detour/Source/DetourNavMeshQuery.cpp
  third_party/recastnavigation/Detour/Source/DetourNode.cpp
)
target_include_directories(detour PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/recastnavigation/Detour/Include"
)

add_executable(grafica_bbb_base
  src/DebugLog.cpp
  src/main.cpp
  src/App.cpp
  src/BaseScene.cpp
  src/CameraController.cpp
  src/Mesh.cpp
  src/Model.cpp
  src/PlayerController.cpp
  src/ShaderProgram.cpp
  src/TextureUtils.cpp
  src/import/ImportedGeometry.cpp
  src/navigation/RecastNavigationWorld.cpp
  src/physics/assets/CollisionAsset.cpp
  src/physics/controller/CharacterController.cpp
  src/render/debug/DebugOverlayRenderer.cpp
  src/render/debug/PhysicsDebugRenderer.cpp
)

target_include_directories(grafica_bbb_base PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(grafica_bbb_base PRIVATE
  APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(APPLE)
  target_compile_definitions(grafica_bbb_base PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_link_libraries(grafica_bbb_base PRIVATE
  OpenGL::GL
  ${GRAFICA_BBB_GLFW_TARGET}
  ${GRAFICA_BBB_GLEW_TARGET}
  ${GRAFICA_BBB_GLM_TARGET}
  ${GRAFICA_BBB_ASSIMP_TARGET}
  soil2
  recast
  detour
)
