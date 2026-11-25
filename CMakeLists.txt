cmake_minimum_required(VERSION 3.24)
project(vulkan_shader_kit LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
  GIT_TAG 16.6.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glslang)

add_library(vksk
  src/compile.cpp
  src/include_resolver.cpp
  src/reflect.cpp
  src/cache.cpp
  src/watch.cpp)
target_include_directories(vksk PUBLIC include)
target_link_libraries(vksk PRIVATE glslang glslang-default-resource-limits)
if(NOT MSVC)
  target_compile_options(vksk PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vksk_cli src/main.cpp)
set_target_properties(vksk_cli PROPERTIES OUTPUT_NAME vksk)
target_link_libraries(vksk_cli PRIVATE vksk)

enable_testing()
add_executable(vksk_tests tests/tests.cpp)
target_link_libraries(vksk_tests PRIVATE vksk)
target_compile_definitions(vksk_tests PRIVATE VKSK_TEST_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders")
add_test(NAME vksk_tests COMMAND vksk_tests)
