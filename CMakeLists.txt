cmake_minimum_required(VERSION 3.21)
project(falling_sand_prototype
  VERSION 0.0.1
  LANGUAGES CXX
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/glfw)
add_subdirectory(external/glad/cmake glad_cmake)

glad_add_library(
    glad_gl_core_33
    REPRODUCIBLE
    API gl:core=3.3
)

find_package(OpenGL REQUIRED)

add_executable(falling_sand_prototype src/main.cpp)

target_link_libraries(falling_sand_prototype 
PRIVATE 
glfw
OpenGL::GL
glad_gl_core_33
)