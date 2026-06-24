cmake_minimum_required(VERSION 3.21)

project(DungeonTavernNPC LANGUAGES CXX)

# --------------------
# Project layout
# --------------------
set(MAIN_DIR source)
set(SRC_DIR ${MAIN_DIR}/src)
set(INCLUDE_DIR ${MAIN_DIR}/include)
set(EXTERNAL_DIR ${MAIN_DIR}/external)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")


add_executable(${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE "${INCLUDE_DIR}" "${EXTERNAL_DIR}")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

set_source_files_properties(${SRC_DIR}/Libs.cpp PROPERTIES COMPILE_OPTIONS "-g0")

# --------------------
# Vulkan
# --------------------
find_package(Vulkan REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

# --------------------
# Dependencies: GLFW + GLM via FetchContent
# --------------------
include(FetchContent)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
# Deactivate non-essential components of GLFW to speed up the build
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM (header-only)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw glm::glm)

# Dear ImGui (used for the dialogue/shop/menu UI). Vendored in-tree under
# source/external/imgui (version 1.91.9b) instead of fetched from GitHub, so the
# project builds with no network access. GLFW and GLM above still use
# FetchContent, matching the course skeleton's own CMakeLists.
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/external/imgui")

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

target_link_libraries(${PROJECT_NAME} PRIVATE imgui)

# --------------------
# Shader compilation (glslc)
# --------------------
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders")
set(SHADER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/spv")
set(SHADER_OUT_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders")
file(GLOB_RECURSE SHADERS CONFIGURE_DEPENDS
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
)
set(SPIRV_OUTPUTS "")
set(SPIRV_COPIED_STAMPS "")
foreach(SHADER IN LISTS SHADERS)
  file(RELATIVE_PATH SHADER_REL_PATH "${SHADER_SRC_DIR}" "${SHADER}")
  get_filename_component(SHADER_REL_DIR "${SHADER_REL_PATH}" DIRECTORY)
  get_filename_component(SHADER_FILE_NAME "${SHADER}" NAME)
  set(SPIRV "${SHADER_BUILD_DIR}/${SHADER_REL_PATH}.spv")
  set(COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shader_copy_stamps/${SHADER_REL_PATH}.stamp")
  set(FINAL_SHADER_DIR "${SHADER_OUT_DIR}/${SHADER_REL_DIR}")
  add_custom_command(
          OUTPUT "${SPIRV}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BUILD_DIR}/${SHADER_REL_DIR}"
          COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -o "${SPIRV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compilo shader: ${SHADER_REL_PATH}"
          VERBATIM
  )
  add_custom_command(
          OUTPUT "${COPY_STAMP}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${FINAL_SHADER_DIR}"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPIRV}" "${FINAL_SHADER_DIR}/"
          COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_CURRENT_BINARY_DIR}/shader_copy_stamps/${SHADER_REL_DIR}"
          COMMAND ${CMAKE_COMMAND} -E touch "${COPY_STAMP}"
          DEPENDS "${SPIRV}"
          COMMENT "Copio SPIR-V: ${SHADER_REL_PATH} -> ${FINAL_SHADER_DIR}"
          VERBATIM
  )
  list(APPEND SPIRV_OUTPUTS "${SPIRV}")
  list(APPEND SPIRV_COPIED_STAMPS "${COPY_STAMP}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPIRV_COPIED_STAMPS})
add_dependencies(${PROJECT_NAME} shaders)

# --------------------
# Copy assets
# --------------------
set(ASSET_SRC_DIR "${CMAKE_SOURCE_DIR}/source/assets")
set(ASSET_DST_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets")

# Always-run target (not POST_BUILD): asset-only edits (scene.json, models)
# must reach the build dir even when no source file changed and the
# executable is not relinked.
add_custom_target(copy_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${ASSET_SRC_DIR}"
    "${ASSET_DST_DIR}"
  COMMENT "Copio asset in: ${ASSET_DST_DIR}"
)
add_dependencies(copy_assets ${PROJECT_NAME})
