set(APP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE APP_SRC_FILES CONFIGURE_DEPENDS
  "${APP_SRC_DIR}/*.cpp"
)

add_executable(${PROJECT_NAME} ${APP_SRC_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/framework/include"
  "${PROJECT_SOURCE_DIR}/third_party/stb"
  "${PROJECT_SOURCE_DIR}/third_party/tinygltf"
  "${PROJECT_SOURCE_DIR}/third_party/tinyobj"
  "${PROJECT_SOURCE_DIR}/third_party/plusaes"
  "${PROJECT_SOURCE_DIR}/third_party/sdefl"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  Vulkan::Vulkan
  glfw
  glm::glm
  imgui
)

dtnpc_compile_shaders(${PROJECT_NAME} "${PROJECT_SOURCE_DIR}/shaders")
dtnpc_copy_assets(${PROJECT_NAME} "${PROJECT_SOURCE_DIR}/assets")
