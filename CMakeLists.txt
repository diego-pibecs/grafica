cmake_minimum_required(VERSION 3.24)
project(ExperienciaTOC LANGUAGES C CXX)

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

add_executable(ExperienciaTOC
  src/main.cpp
  src/App.cpp
  src/BaseScene.cpp
  src/CameraController.cpp
  src/Mesh.cpp
  src/Model.cpp
  src/PlayerController.cpp
  src/ShaderProgram.cpp
  src/TextureUtils.cpp
)

target_include_directories(ExperienciaTOC PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(ExperienciaTOC PRIVATE
  APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(APPLE)
  target_compile_definitions(ExperienciaTOC PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_link_libraries(ExperienciaTOC PRIVATE
  OpenGL::GL
  ${GRAFICA_BBB_GLFW_TARGET}
  ${GRAFICA_BBB_GLEW_TARGET}
  ${GRAFICA_BBB_GLM_TARGET}
  ${GRAFICA_BBB_ASSIMP_TARGET}
  soil2
)
