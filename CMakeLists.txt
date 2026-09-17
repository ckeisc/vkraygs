cmake_minimum_required(VERSION 3.15)

project(vkgs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Keep Windows' min/max macros from breaking std::min/std::max
add_compile_definitions(NOMINMAX)

find_package(Vulkan REQUIRED)
add_subdirectory(third_party/VulkanMemoryAllocator EXCLUDE_FROM_ALL)
add_subdirectory(third_party/vulkan_radix_sort EXCLUDE_FROM_ALL)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
add_subdirectory(third_party/glm EXCLUDE_FROM_ALL)
add_subdirectory(third_party/argparse EXCLUDE_FROM_ALL)

# zlib + zstd for the SPZ decoder, built from source so the build is
# portable (no system packages or hardcoded library paths needed)
include(FetchContent)
FetchContent_Declare(zlib
  URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
  URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
  URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
  URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
  SOURCE_SUBDIR build/cmake)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib zstd)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# shaders
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/generated)

# add_shader(TARGET SHADER OUTPUT DEFINE...)
function(add_shader)
  list(POP_FRONT ARGV TARGET SHADER OUTPUT)
  list(TRANSFORM ARGV PREPEND "-D" OUTPUT_VARIABLE DEFINES)

  get_filename_component(SHADER ${SHADER} ABSOLUTE)

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/${OUTPUT}.h
    COMMAND
      ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
      --target-env spirv1.5
      -V
      --vn ${OUTPUT}
      -o ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/${OUTPUT}.h
      ${DEFINES}
      ${SHADER}
    DEPENDS ${SHADER}
    COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/${OUTPUT}.h"
  )

  add_custom_target(${OUTPUT} DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/${OUTPUT}.h)
  add_dependencies(${TARGET} ${OUTPUT})
endfunction()

# vkgs
add_library(vkgs SHARED
  src/vkgs/engine/engine.cc
  src/vkgs/engine/splat_load_thread.cc
  src/vkgs/scene/camera.cc
  src/vkgs/viewer/viewer.cc
  src/vkgs/vulkan/attachment.cc
  src/vkgs/vulkan/buffer.cc
  src/vkgs/vulkan/compute_pipeline.cc
  src/vkgs/vulkan/context.cc
  src/vkgs/vulkan/cpu_buffer.cc
  src/vkgs/vulkan/descriptor_layout.cc
  src/vkgs/vulkan/descriptor.cc
  src/vkgs/vulkan/framebuffer.cc
  src/vkgs/vulkan/graphics_pipeline.cc
  src/vkgs/vulkan/pipeline_layout.cc
  src/vkgs/vulkan/render_pass.cc
  src/vkgs/vulkan/swapchain.cc
  src/vkgs/vulkan/uniform_buffer.cc
  src/vkgs/vulkan/vma_impl.cc
  # imgui
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_vulkan.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  # spz (native .spz loading)
  third_party/spz/src/cc/load-spz.cc
  third_party/spz/src/cc/splat-types.cc
  third_party/spz/src/cc/splat-c-types.cc
)

target_include_directories(vkgs
  PUBLIC include
  PRIVATE
    src
    third_party/imgui
    third_party/imgui/backends
    third_party/spz/src/cc
    third_party/stb
)

target_link_libraries(vkgs
  PUBLIC glm
  PRIVATE Vulkan::Vulkan VulkanMemoryAllocator vk_radix_sort glfw
  zlibstatic libzstd_static
)

add_shader(vkgs src/shader/color.vert color_vert)
add_shader(vkgs src/shader/color.frag color_frag)
add_shader(vkgs src/shader/inverse_index.comp inverse_index_comp)
add_shader(vkgs src/shader/parse_ply.comp parse_ply_comp)
add_shader(vkgs src/shader/projection.comp projection_comp)
add_shader(vkgs src/shader/rank.comp rank_comp)
add_shader(vkgs src/shader/splat.vert splat_vert)
add_shader(vkgs src/shader/splat.frag splat_frag)
add_shader(vkgs src/shader/splat_geom.vert splat_geom_vert)
add_shader(vkgs src/shader/splat_geom.geom splat_geom_geom)

add_shader(vkgs src/shader/raygs_projection.comp raygs_projection_comp)
add_shader(vkgs src/shader/raygs_splat.vert raygs_splat_vert)
add_shader(vkgs src/shader/raygs_splat.frag raygs_splat_frag)

# executable
add_executable(vkgs_viewer examples/vkgs_viewer.cc)
target_link_libraries(vkgs_viewer
  PUBLIC vkgs
  PRIVATE argparse
)
